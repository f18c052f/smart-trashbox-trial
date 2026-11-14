// 組む順番 F・G-1・G-2: つまみ（VR1）でドライバ U1 の出力の強さを変える。エンコーダのカウントも表示する。
// つなぐもの: F まで（U1・C7・C10。J2 には LED 治具をつなぐか、何もつながない）。G ではモータ M0 とエンコーダ。
// ボード: esp32_bluepad32 の ESP32 Dev Module（A〜H すべて同じ）
// シリアルモニタ: 115200 bps。行末は「改行」にする
//
// 動き:
//   - 書き込み・リセットの直後は何も出さない。つまみを一度いちばん下（0）に戻すと「準備よし」になる。
//     つまみを回したまま電源を入れても、急に動かない。
//   - つまみの量で出力の強さ（デューティ 0〜100%）が変わる。いちばん上で 100%（IN1 か IN2 が H のまま）。
//   - 強くするときは、0 から 100% まで 1 秒かけて上げる（急に大きな電流が流れないように）。弱めるときはすぐ。
//   - 前進: IN1 に PWM（20kHz）、IN2 は L。後進: IN2 に PWM、IN1 は L。
//     PWM が切れている間は IN1・IN2 とも L で、ドライバの出力が開く（モータは惰性で回る）。
//
// シリアルモニタから送る文字:
//   f  前進にする      b  後進にする
//      向きを変えるのは、つまみが 0 で、出力が 0 になってから 1 秒たち、エンコーダも止まっているときだけ。
//   s  すぐ止める。つまみを 0 に戻すまで出さない
//   r  count を 0 に戻す
//
// 表示（0.1 秒ごと）:
//   pot   つまみ（0〜100%）
//   duty  出している強さ（%。+ が前進、− が後進）
//   count エンコーダの積算、cps 1 秒あたりのカウント
//   ready 準備よし（1）か、つまみを 0 に戻すのを待っている（0）か
//
// GPIO19・GPIO23（ドライバの IN1・IN2）を動かすのは F・G のプログラムだけ。

#include <driver/pcnt.h>

// ---- C の結果をここに入れる ----
// 正転（軸側から見て時計回り）で count が増えたら true（黄が A相）、減ったら false（緑が A相）。
const bool YELLOW_IS_A = true;

// ---- ピン ----
const int IN1_PIN = 19;     // U1 IN1（前進）
const int IN2_PIN = 23;     // U1 IN2（後進）
const int POT_PIN = 33;     // BENCH_POT
const int YELLOW_PIN = 4;   // ENC0_A_IN（黄、R10 経由）
const int GREEN_PIN = 13;   // ENC0_B_IN（緑、R11 経由）
const int ENC_A_PIN = YELLOW_IS_A ? YELLOW_PIN : GREEN_PIN;
const int ENC_B_PIN = YELLOW_IS_A ? GREEN_PIN : YELLOW_PIN;

// ---- PWM ----
const int CH_IN1 = 0;
const int CH_IN2 = 1;
const uint32_t PWM_FREQ = 20000;               // 20kHz（耳に聞こえない）
const uint8_t PWM_BITS = 10;
const uint32_t PWM_MAX = (1 << PWM_BITS) - 1;  // 1023。ledcWrite はこの値のとき H のままにする

// ---- つまみ ----
const int RAW_ZERO = 100;   // これ以下を 0 とみなす（ADC は 0V 付近を読めない）
const int RAW_FULL = 4000;  // これ以上を 100% とみなす

// ---- 止まっているとみなす条件（向きを変えるとき） ----
const uint32_t STOP_WAIT_MS = 1000;  // 出力が 0 になってから待つ時間
const int32_t STOP_CPS = 20;         // エンコーダがこれより遅ければ止まっている

// ---- エンコーダ（c_encoder と同じ。4逓倍、A が B より先に変わる向きで増える） ----
const pcnt_unit_t UNIT = PCNT_UNIT_0;
const int16_t LIMIT = 30000;

int16_t lastRaw = 0;
int32_t total = 0;
int32_t cps = 0;

// ---- 状態 ----
bool ready = false;        // つまみを一度 0 に戻したか
int dir = +1;              // +1 前進、−1 後進
int out = 0;               // 出している強さ（0〜1000、‰）
uint32_t lastNonZero = 0;  // 出力が最後に 0 でなかった時刻
uint32_t lastStep = 0;

void setupEncoder() {
  pcnt_config_t ch0 = {};
  ch0.pulse_gpio_num = ENC_A_PIN;
  ch0.ctrl_gpio_num = ENC_B_PIN;
  ch0.lctrl_mode = PCNT_MODE_REVERSE;
  ch0.hctrl_mode = PCNT_MODE_KEEP;
  ch0.pos_mode = PCNT_COUNT_DEC;
  ch0.neg_mode = PCNT_COUNT_INC;
  ch0.counter_h_lim = LIMIT;
  ch0.counter_l_lim = -LIMIT;
  ch0.unit = UNIT;
  ch0.channel = PCNT_CHANNEL_0;
  pcnt_unit_config(&ch0);

  pcnt_config_t ch1 = ch0;
  ch1.pulse_gpio_num = ENC_B_PIN;
  ch1.ctrl_gpio_num = ENC_A_PIN;
  ch1.pos_mode = PCNT_COUNT_INC;
  ch1.neg_mode = PCNT_COUNT_DEC;
  ch1.channel = PCNT_CHANNEL_1;
  pcnt_unit_config(&ch1);

  gpio_pullup_en((gpio_num_t)ENC_A_PIN);
  gpio_pullup_en((gpio_num_t)ENC_B_PIN);

  pcnt_set_filter_value(UNIT, 1000);  // 12.5µs より短いひげを無視する
  pcnt_filter_enable(UNIT);

  pcnt_counter_pause(UNIT);
  pcnt_counter_clear(UNIT);
  pcnt_counter_resume(UNIT);
}

void updateEncoder() {
  int16_t raw = 0;
  pcnt_get_counter_value(UNIT, &raw);
  int32_t d = (int32_t)raw - lastRaw;
  if (d < -LIMIT / 2) d += LIMIT;
  if (d > LIMIT / 2) d -= LIMIT;
  total += d;
  lastRaw = raw;
}

// つまみを 0〜1000（‰）で返す。8回読んで平均する
int readPot() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(POT_PIN);
  int raw = sum / 8;
  if (raw <= RAW_ZERO) return 0;
  if (raw >= RAW_FULL) return 1000;
  return (int32_t)(raw - RAW_ZERO) * 1000 / (RAW_FULL - RAW_ZERO);
}

// 向きと強さ（0〜1000）を IN1・IN2 に出す。使わない側を先に L にする
void applyOutput(int d, int permille) {
  uint32_t duty = (uint32_t)permille * PWM_MAX / 1000;  // 1000 → 1023（H のまま）
  if (d > 0) {
    ledcWrite(CH_IN2, 0);
    ledcWrite(CH_IN1, duty);
  } else {
    ledcWrite(CH_IN1, 0);
    ledcWrite(CH_IN2, duty);
  }
}

void requestDir(int d, int pot, uint32_t now) {
  if (d == dir) {
    Serial.println(d > 0 ? "すでに前進" : "すでに後進");
    return;
  }
  bool stopped = (out == 0) && (pot == 0) && (now - lastNonZero >= STOP_WAIT_MS) && (abs(cps) < STOP_CPS);
  if (!stopped) {
    Serial.println("向きは変えない: つまみを 0 に戻し、止まってから 1 秒待って、もう一度送る");
    return;
  }
  dir = d;
  applyOutput(dir, 0);
  Serial.println(dir > 0 ? "前進にした（IN1 に PWM）" : "後進にした（IN2 に PWM）");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // 先に PWM を 0 で用意してからピンにつなぐ（つないだ瞬間に H が出ないように）
  ledcSetup(CH_IN1, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_IN2, PWM_FREQ, PWM_BITS);
  ledcWrite(CH_IN1, 0);
  ledcWrite(CH_IN2, 0);
  ledcAttachPin(IN1_PIN, CH_IN1);
  ledcAttachPin(IN2_PIN, CH_IN2);

  analogReadResolution(12);
  analogSetPinAttenuation(POT_PIN, ADC_11db);

  setupEncoder();

  lastStep = millis();
  Serial.println("f_bench: f 前進 / b 後進 / s 止める / r count を 0 に");
  Serial.printf("エンコーダの A相: %s\n", YELLOW_IS_A ? "黄（GPIO4）" : "緑（GPIO13）");
  Serial.println("いまは前進。つまみを 0 に戻すと準備よしになる");
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t lastNag = 0;
  static int32_t lastTotal = 0;

  uint32_t now = millis();
  updateEncoder();
  int pot = readPot();

  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 's':
      case 'S':
        ready = false;
        out = 0;
        applyOutput(dir, 0);
        Serial.println("止めた。つまみを 0 に戻すまで出さない");
        break;
      case 'r':
      case 'R':
        total = 0;
        lastTotal = 0;
        Serial.println("count を 0 に戻した");
        break;
      case 'f':
      case 'F':
        requestDir(+1, pot, now);
        break;
      case 'b':
      case 'B':
        requestDir(-1, pot, now);
        break;
      default:
        break;  // 改行など
    }
  }

  if (!ready) {
    if (pot == 0) {
      ready = true;
      Serial.println("準備よし（つまみを上げると出力する）");
    } else if (now - lastNag >= 2000) {
      Serial.println("つまみを 0 に戻すまで出力しない");
      lastNag = now;
    }
  }

  // 強くするときは 1ms あたり 1‰（1 秒で 0→100%）、弱めるときはすぐ
  int target = ready ? pot : 0;
  int dt = (int)(now - lastStep);
  lastStep = now;
  if (target > out) {
    out = min(target, out + dt);
  } else {
    out = target;
  }
  applyOutput(dir, out);
  if (out > 0) lastNonZero = now;

  if (now - lastPrint >= 100) {
    cps = (total - lastTotal) * 1000 / (int32_t)(now - lastPrint);
    Serial.printf("pot:%d,duty:%d,count:%ld,cps:%ld,ready:%d\n", pot / 10, dir * out / 10, (long)total, (long)cps,
                  ready ? 1 : 0);
    lastTotal = total;
    lastPrint = now;
  }
  delay(10);
}
