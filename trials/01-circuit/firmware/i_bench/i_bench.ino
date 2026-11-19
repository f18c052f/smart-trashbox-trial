// 段階3 の組む順番 I・J: つまみ（VR1）で、選んだ輪のドライバの出力の強さを変える。3輪ぶんのエンコーダのカウントも表示する。
// f_bench を3輪に広げたもの。動きは f_bench と同じで、出す輪を選べるようにした。
// つなぐもの: 段階2の H までに、I で輪1（U2・C8・C11、R12・R13、J5・J6）、J で輪2（U3・C9・C12・C2、R14・R15、J7・J8）を足したもの。
// ボード: esp32_bluepad32 の ESP32 Dev Module（段階2と同じ）
// シリアルモニタ: 115200 bps。行末は「改行」にする
//
// 動き:
//   - 書き込み・リセットの直後は何も出さない。つまみを一度いちばん下（0）に戻すと「準備よし」になる。
//   - 出すのは選んだ輪だけ。ほかの輪の IN1・IN2 はずっと L（ドライバの出力が開いている）。最初は輪0。
//     「全部」を選ぶと、3輪に同じ強さを出す（K で、3輪を同じ強さで比べ、回しながら TP6・TP7 を測るため）。
//   - つまみの量で出力の強さ（デューティ 0〜100%）が変わる。いちばん上で 100%（IN1 か IN2 が H のまま）。
//   - 強くするときは、0 から 100% まで 1 秒かけて上げる。弱めるときはすぐ。
//   - 前進: IN1 に PWM（20kHz）、IN2 は L。後進: IN2 に PWM、IN1 は L。
//     PWM が切れている間は IN1・IN2 とも L で、ドライバの出力が開く（モータは惰性で回る）。
//
// シリアルモニタから送る文字:
//   0 1 2  出す輪を選ぶ（輪0 = U1・M0、輪1 = U2・M1、輪2 = U3・M2）。a で全部。選び直すと前進に戻る
//   f  前進にする      b  後進にする
//      輪を選ぶ・向きを変えるのは、つまみが 0 で、出力が 0 になってから 1 秒たち、選んでいる輪のエンコーダも止まっているときだけ。
//   s  すぐ止める。つまみを 0 に戻すまで出さない
//   r  3輪の count を 0 に戻す
//
// 表示（0.1 秒ごと）:
//   wheel 選んでいる輪（0・1・2・all）
//   pot   つまみ（0〜100%）
//   duty  選んだ輪に出している強さ（%。+ が前進、− が後進。全部のときは3輪とも同じ）
//   count 3輪のエンコーダの積算（輪0/輪1/輪2）
//   cps   3輪の 1 秒あたりのカウント（輪0/輪1/輪2）
//   ready 準備よし（1）か、つまみを 0 に戻すのを待っている（0）か
//
// モータにつながる GPIO（19・23・21・25・22・26）を動かすのは f_bench・g_teleop・i_bench・k_teleop だけ。

#include <driver/pcnt.h>

// ---- エンコーダの A相（輪ごと） ----
// 正転（軸側から見て時計回り）で count が増えたら true（黄が A相）、減ったら false（緑が A相）。
// 輪0 は段階2の C で確かめた（true）。輪1 は I、輪2 は J で確かめる。k_teleop の同じ表も合わせる。
const bool YELLOW_IS_A[3] = {true, true, true};

// ---- 輪ごとのピン ----
struct Wheel {
  int in1Pin;     // ドライバの IN1（前進）
  int in2Pin;     // ドライバの IN2（後進）
  int chIn1;      // PWM のチャネル
  int chIn2;
  int yellowPin;  // エンコーダの黄（R10・R12・R14 経由）
  int greenPin;   // エンコーダの緑（R11・R13・R15 経由）
  pcnt_unit_t unit;
  int16_t lastRaw;
  int32_t total;
  int32_t lastTotal;
  int32_t cps;
};

Wheel wheels[3] = {
    {19, 23, 0, 1, 4, 13, PCNT_UNIT_0, 0, 0, 0, 0},   // 輪0: U1（M0_IN1・M0_IN2）、ENC0_A_IN・ENC0_B_IN
    {21, 25, 2, 3, 14, 16, PCNT_UNIT_1, 0, 0, 0, 0},  // 輪1: U2（M1_IN1・M1_IN2）、ENC1_A_IN・ENC1_B_IN
    {22, 26, 4, 5, 17, 18, PCNT_UNIT_2, 0, 0, 0, 0},  // 輪2: U3（M2_IN1・M2_IN2）、ENC2_A_IN・ENC2_B_IN
};

const int POT_PIN = 33;  // BENCH_POT

// ---- PWM（f_bench と同じ） ----
const uint32_t PWM_FREQ = 20000;               // 20kHz（耳に聞こえない）
const uint8_t PWM_BITS = 10;
const uint32_t PWM_MAX = (1 << PWM_BITS) - 1;  // 1023。ledcWrite はこの値のとき H のままにする

// ---- つまみ（f_bench と同じ） ----
const int RAW_ZERO = 100;   // これ以下を 0 とみなす（ADC は 0V 付近を読めない）
const int RAW_FULL = 4000;  // これ以上を 100% とみなす

// ---- 止まっているとみなす条件（輪を選ぶ・向きを変えるとき） ----
const uint32_t STOP_WAIT_MS = 1000;  // 出力が 0 になってから待つ時間
const int32_t STOP_CPS = 20;         // エンコーダがこれより遅ければ止まっている

// ---- エンコーダ（c_encoder・f_bench と同じ。4逓倍、A が B より先に変わる向きで増える） ----
const int16_t LIMIT = 30000;

// ---- 状態 ----
const int SEL_ALL = 3;
int sel = 0;               // 出す輪（0・1・2、SEL_ALL は全部）
bool ready = false;        // つまみを一度 0 に戻したか
int dir = +1;              // +1 前進、−1 後進
int out = 0;               // 出している強さ（0〜1000、‰）
uint32_t lastNonZero = 0;  // 出力が最後に 0 でなかった時刻
uint32_t lastStep = 0;

void setupEncoder(int k) {
  Wheel& w = wheels[k];
  int a = YELLOW_IS_A[k] ? w.yellowPin : w.greenPin;
  int b = YELLOW_IS_A[k] ? w.greenPin : w.yellowPin;

  pcnt_config_t ch0 = {};
  ch0.pulse_gpio_num = a;
  ch0.ctrl_gpio_num = b;
  ch0.lctrl_mode = PCNT_MODE_REVERSE;
  ch0.hctrl_mode = PCNT_MODE_KEEP;
  ch0.pos_mode = PCNT_COUNT_DEC;
  ch0.neg_mode = PCNT_COUNT_INC;
  ch0.counter_h_lim = LIMIT;
  ch0.counter_l_lim = -LIMIT;
  ch0.unit = w.unit;
  ch0.channel = PCNT_CHANNEL_0;
  pcnt_unit_config(&ch0);

  pcnt_config_t ch1 = ch0;
  ch1.pulse_gpio_num = b;
  ch1.ctrl_gpio_num = a;
  ch1.pos_mode = PCNT_COUNT_INC;
  ch1.neg_mode = PCNT_COUNT_DEC;
  ch1.channel = PCNT_CHANNEL_1;
  pcnt_unit_config(&ch1);

  gpio_pullup_en((gpio_num_t)a);
  gpio_pullup_en((gpio_num_t)b);

  pcnt_set_filter_value(w.unit, 1000);  // 12.5µs より短いひげを無視する
  pcnt_filter_enable(w.unit);

  pcnt_counter_pause(w.unit);
  pcnt_counter_clear(w.unit);
  pcnt_counter_resume(w.unit);
}

void updateEncoder(Wheel& w) {
  int16_t raw = 0;
  pcnt_get_counter_value(w.unit, &raw);
  int32_t d = (int32_t)raw - w.lastRaw;
  if (d < -LIMIT / 2) d += LIMIT;
  if (d > LIMIT / 2) d -= LIMIT;
  w.total += d;
  w.lastRaw = raw;
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

// 向きと強さ（0〜1000）を、その輪の IN1・IN2 に出す。使わない側を先に L にする
void applyOutput(const Wheel& w, int d, int permille) {
  uint32_t duty = (uint32_t)permille * PWM_MAX / 1000;  // 1000 → 1023（H のまま）
  if (d > 0) {
    ledcWrite(w.chIn2, 0);
    ledcWrite(w.chIn1, duty);
  } else {
    ledcWrite(w.chIn1, 0);
    ledcWrite(w.chIn2, duty);
  }
}

bool selected(int k) { return sel == SEL_ALL || sel == k; }

const char* selName(int s) {
  switch (s) {
    case 0:
      return "0";
    case 1:
      return "1";
    case 2:
      return "2";
    default:
      return "all";
  }
}

// 選んでいる輪（全部なら3輪）に出す
void applySelected(int d, int permille) {
  for (int k = 0; k < 3; k++) {
    if (selected(k)) applyOutput(wheels[k], d, permille);
  }
}

bool stoppedNow(int pot, uint32_t now) {
  if (out != 0 || pot != 0 || now - lastNonZero < STOP_WAIT_MS) return false;
  for (int k = 0; k < 3; k++) {
    if (selected(k) && abs(wheels[k].cps) >= STOP_CPS) return false;
  }
  return true;
}

void requestDir(int d, int pot, uint32_t now) {
  if (d == dir) {
    Serial.println(d > 0 ? "すでに前進" : "すでに後進");
    return;
  }
  if (!stoppedNow(pot, now)) {
    Serial.println("向きは変えない: つまみを 0 に戻し、止まってから 1 秒待って、もう一度送る");
    return;
  }
  dir = d;
  applySelected(dir, 0);
  Serial.println(dir > 0 ? "前進にした（IN1 に PWM）" : "後進にした（IN2 に PWM）");
}

void requestWheel(int k, int pot, uint32_t now) {
  if (k == sel) {
    Serial.printf("すでに %s\n", k == SEL_ALL ? "全部" : (k == 0 ? "輪0" : (k == 1 ? "輪1" : "輪2")));
    return;
  }
  if (!stoppedNow(pot, now)) {
    Serial.println("輪は変えない: つまみを 0 に戻し、止まってから 1 秒待って、もう一度送る");
    return;
  }
  for (int i = 0; i < 3; i++) applyOutput(wheels[i], +1, 0);  // 3輪とも IN1・IN2 を L にしてから選び直す
  sel = k;
  dir = +1;
  if (sel == SEL_ALL) {
    Serial.println("全部を選んだ（3輪に同じ強さを出す。前進）");
  } else {
    Serial.printf("輪%d を選んだ（前進）\n", sel);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // 先に PWM を 0 で用意してからピンにつなぐ（つないだ瞬間に H が出ないように）。3輪ぶん
  for (int k = 0; k < 3; k++) {
    Wheel& w = wheels[k];
    ledcSetup(w.chIn1, PWM_FREQ, PWM_BITS);
    ledcSetup(w.chIn2, PWM_FREQ, PWM_BITS);
    ledcWrite(w.chIn1, 0);
    ledcWrite(w.chIn2, 0);
    ledcAttachPin(w.in1Pin, w.chIn1);
    ledcAttachPin(w.in2Pin, w.chIn2);
  }

  analogReadResolution(12);
  analogSetPinAttenuation(POT_PIN, ADC_11db);

  for (int k = 0; k < 3; k++) setupEncoder(k);

  lastStep = millis();
  Serial.println("i_bench: 0/1/2 輪を選ぶ / a 全部 / f 前進 / b 後進 / s 止める / r count を 0 に");
  for (int k = 0; k < 3; k++) {
    Serial.printf("輪%d のエンコーダの A相: %s（GPIO%d）\n", k, YELLOW_IS_A[k] ? "黄" : "緑",
                  YELLOW_IS_A[k] ? wheels[k].yellowPin : wheels[k].greenPin);
  }
  Serial.println("いまは輪0・前進。つまみを 0 に戻すと準備よしになる");
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t lastNag = 0;

  uint32_t now = millis();
  for (int k = 0; k < 3; k++) updateEncoder(wheels[k]);
  int pot = readPot();

  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 's':
      case 'S':
        ready = false;
        out = 0;
        applySelected(dir, 0);
        Serial.println("止めた。つまみを 0 に戻すまで出さない");
        break;
      case 'r':
      case 'R':
        for (int k = 0; k < 3; k++) {
          wheels[k].total = 0;
          wheels[k].lastTotal = 0;
        }
        Serial.println("3輪の count を 0 に戻した");
        break;
      case 'f':
      case 'F':
        requestDir(+1, pot, now);
        break;
      case 'b':
      case 'B':
        requestDir(-1, pot, now);
        break;
      case '0':
      case '1':
      case '2':
        requestWheel(c - '0', pot, now);
        break;
      case 'a':
      case 'A':
        requestWheel(SEL_ALL, pot, now);
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
  applySelected(dir, out);
  if (out > 0) lastNonZero = now;

  if (now - lastPrint >= 100) {
    int32_t span = (int32_t)(now - lastPrint);
    for (int k = 0; k < 3; k++) {
      Wheel& w = wheels[k];
      w.cps = (w.total - w.lastTotal) * 1000 / span;
      w.lastTotal = w.total;
    }
    Serial.printf("wheel:%s,pot:%d,duty:%d,count:%ld/%ld/%ld,cps:%ld/%ld/%ld,ready:%d\n", selName(sel), pot / 10,
                  dir * out / 10, (long)wheels[0].total, (long)wheels[1].total, (long)wheels[2].total,
                  (long)wheels[0].cps, (long)wheels[1].cps, (long)wheels[2].cps, ready ? 1 : 0);
    lastPrint = now;
  }
  delay(10);
}
