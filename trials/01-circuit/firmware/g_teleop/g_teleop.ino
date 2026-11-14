// 組む順番 G-3: DualSense で輪1個（モータ M0）を回す（E-6）。エンコーダのカウントも表示する。
// つなぐもの: G-2 までと同じ（D3・R10・R11 入り、J2 にモータ、J3 にエンコーダ）。VR1 は使わない（つないだままでよい）。
// ボード: esp32_bluepad32 の ESP32 Dev Module（A〜H すべて同じ）
// シリアルモニタ: 115200 bps。行末は「改行」にする
//
// 操作（DualSense）:
//   - L1 を押しているあいだだけ動く（デッドマン）。離すとすぐ止まる。
//   - 左スティックの上下で前進・後進。倒した量で強さが変わる。いちばん倒して「上限」（最初は 30%）。
//   - 十字キーの上・下で上限を 10% ずつ変える（10〜100%）。
//   - 速度の制御はしない（開ループ）。スティックの量をそのまま PWM のデューティにする。
//
// 止める仕組み（どれも出力をすぐ 0 にする）:
//   - L1 を離す。
//   - DualSense の値が WATCHDOG_MS より長く届かない（指令ウォッチドッグ）。DualSense が切れる。
//   - 出しているのにエンコーダが回らない（ロック保護。軸が止められている、またはエンコーダが数えていない）。
//   - 前進を出しているのにエンコーダが後ろへ回る、またはその逆（エンコーダの向きの見張り）。
//   - シリアルモニタから s。
//   L1 を離したとき以外は、スティックを中央に戻すまで出さない。書き込み・リセットの直後、つないだ直後も同じ。
//   ライトバーは、出せるとき緑、スティックを中央に戻すのを待っているとき赤。
//
// 強くするときは 0 から 100% まで 1 秒かけて上げる。弱めるときはすぐ。
// 向きを変えるとき（スティックを逆に倒したとき）は、出力を 0 にして（IN1・IN2 とも L、モータは惰性で回る）、
// 0 になってから STOP_WAIT_MS たち、エンコーダも止まってから逆へ出す（回生で VM が上がらないように）。
//
// シリアルモニタから送る文字:
//   s  すぐ止める。スティックを中央に戻すまで出さない
//   w  DualSense の値を 1 秒間読まない（途切れたことにして、ウォッチドッグで止まるかを試す）
//   r  count を 0 に戻す
//
// 表示（0.1 秒ごと）:
//   L1    L1 を押しているか（1/0）
//   stick スティック（%。+ が上 = 前進）。上限を掛ける前の値
//   max   上限（%）
//   duty  出している強さ（%。+ が前進、− が後進）
//   count エンコーダの積算、cps 1 秒あたりのカウント
//   gap   直前 1 秒で、DualSense の値が届く間隔のいちばん長いもの（ms）
//   ready 出せる（1）か、スティックを中央に戻すのを待っている（0）か
//
// GPIO19・GPIO23（ドライバの IN1・IN2）を動かすのは F・G のプログラムだけ。

#include <Bluepad32.h>
#include <driver/pcnt.h>

// ---- C の結果をここに入れる（f_bench と同じ） ----
// 正転（軸側から見て時計回り）で count が増えたら true（黄が A相）、減ったら false（緑が A相）。
const bool YELLOW_IS_A = true;

// ---- ピン ----
const int IN1_PIN = 19;     // U1 IN1（前進）
const int IN2_PIN = 23;     // U1 IN2（後進）
const int YELLOW_PIN = 4;   // ENC0_A_IN（黄、R10 経由）
const int GREEN_PIN = 13;   // ENC0_B_IN（緑、R11 経由）
const int ENC_A_PIN = YELLOW_IS_A ? YELLOW_PIN : GREEN_PIN;
const int ENC_B_PIN = YELLOW_IS_A ? GREEN_PIN : YELLOW_PIN;

// ---- PWM（f_bench と同じ） ----
const int CH_IN1 = 0;
const int CH_IN2 = 1;
const uint32_t PWM_FREQ = 20000;               // 20kHz（耳に聞こえない）
const uint8_t PWM_BITS = 10;
const uint32_t PWM_MAX = (1 << PWM_BITS) - 1;  // 1023。ledcWrite はこの値のとき H のままにする

// ---- スティック ----
const int STICK_DEADZONE = 40;  // 中央から ±40（全体の約 8%）は 0 とみなす。スティックは -512〜511
const int MAX_START = 30;       // 上限の最初の値（%）
const int MAX_STEP = 10;        // 十字キーで変える幅（%）

// ---- 止める仕組み ----
const uint32_t WATCHDOG_MS = 250;   // DualSense の値がこれより長く届かなければ止める
const uint32_t MUTE_MS = 1000;      // w を送ったとき、値を読まない時間
const uint32_t STOP_WAIT_MS = 500;  // 向きを変える前に、出力 0 のまま待つ時間
const int32_t STOP_CPS = 20;        // エンコーダがこれより遅ければ止まっている
const int LOCK_DUTY = 200;          // ロック保護: これ（‰）以上を出していて
const int32_t LOCK_CPS = 20;        //   エンコーダがこれより遅い状態が
const uint32_t LOCK_MS = 1000;      //   これだけ続いたら止める
const int32_t REVERSE_CPS = 100;    // 向きの見張り: 出している向きと逆にこれより速く回る状態が
const uint32_t REVERSE_MS = 300;    //   これだけ続いたら止める

// ---- エンコーダ（c_encoder・f_bench と同じ。4逓倍、A が B より先に変わる向きで増える） ----
const pcnt_unit_t UNIT = PCNT_UNIT_0;
const int16_t LIMIT = 30000;

int16_t lastRaw = 0;
int32_t total = 0;
int32_t cps = 0;

// ---- DualSense ----
ControllerPtr pad = nullptr;
bool haveData = false;    // つないでから値が一度でも届いたか（届くまではスティックの位置が分からない）
uint32_t lastData = 0;    // 最後に値が届いた時刻
uint32_t muteUntil = 0;   // w を送ったとき、この時刻まで値を読まない
bool muting = false;
int stick = 0;            // スティック（‰。+ が前進）
bool l1 = false;
uint8_t lastDpad = 0;
uint32_t gapThisWindow = 0;
uint32_t maxGap = 0;

// ---- 状態 ----
bool ready = false;        // スティックを中央に戻したか（出してよいか）
int maxPct = MAX_START;    // 上限（%）
int dir = +1;              // +1 前進、−1 後進
int out = 0;               // 出している強さ（0〜1000、‰）
uint32_t lastNonZero = 0;  // 出力が最後に 0 でなかった時刻
uint32_t lastStep = 0;
uint32_t lockSince = 0;    // ロックの条件が始まった時刻（0 は条件が無い）
uint32_t reverseSince = 0;
bool waitingDir = false;   // 向きを変えるのを待っているか

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

// スティックの Y（-512〜511、上が負）を ‰ にする。上が +、中央付近は 0
int stickPermille(int32_t axisY) {
  int32_t v = -axisY;
  int32_t a = abs(v);
  if (a <= STICK_DEADZONE) return 0;
  int32_t m = (a - STICK_DEADZONE) * 1000 / (512 - STICK_DEADZONE);
  if (m > 1000) m = 1000;
  return v > 0 ? (int)m : -(int)m;
}

void setReady(bool r) {
  if (ready == r) return;
  ready = r;
  if (pad) {
    if (r) {
      pad->setColorLED(0, 255, 0);
    } else {
      pad->setColorLED(255, 0, 0);
    }
  }
}

// すぐ止め、スティックを中央に戻すまで出さない
void stopAndLatch(const char* why, bool rumble) {
  out = 0;
  applyOutput(dir, 0);
  lockSince = 0;
  reverseSince = 0;
  waitingDir = false;
  setReady(false);
  Serial.printf("止めた: %s。スティックを中央に戻すまで出さない\n", why);
  if (rumble && pad) pad->playDualRumble(0, 300, 0xff, 0xff);
}

void onConnected(ControllerPtr ctl) {
  if (pad != nullptr) {
    Serial.println("2台目は使わない");
    return;
  }
  pad = ctl;
  haveData = false;
  lastData = millis();
  gapThisWindow = 0;
  maxGap = 0;
  stick = 0;
  l1 = false;
  lastDpad = 0;
  ready = false;
  ctl->setColorLED(255, 0, 0);
  ctl->playDualRumble(0, 150, 0x80, 0x40);
  Serial.printf("つながった: %s\n", ctl->getModelName().c_str());
}

void onDisconnected(ControllerPtr ctl) {
  if (ctl != pad) return;
  pad = nullptr;  // 先に外す（切れていくコントローラにライトバーの指示を送らない）
  l1 = false;
  stick = 0;
  stopAndLatch("DualSense が切れた", false);
}

void readPad(uint32_t now) {
  if (!pad || !pad->isConnected() || !pad->hasData()) return;  // hasData は新しい値が届いたときだけ真
  if (muting) return;                                         // w: 届いても読まない

  uint32_t gap = now - lastData;
  if (gap > gapThisWindow) gapThisWindow = gap;
  lastData = now;
  haveData = true;

  stick = stickPermille(pad->axisY());
  l1 = pad->l1();

  uint8_t dp = pad->dpad();
  uint8_t pressed = dp & ~lastDpad;  // 押した瞬間だけ
  lastDpad = dp;
  if (pressed & DPAD_UP) {
    maxPct = min(100, maxPct + MAX_STEP);
    Serial.printf("上限 %d%%\n", maxPct);
  }
  if (pressed & DPAD_DOWN) {
    maxPct = max(MAX_STEP, maxPct - MAX_STEP);
    Serial.printf("上限 %d%%\n", maxPct);
  }
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

  setupEncoder();

  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableVirtualDevice(false);  // タッチパッドをマウスとして扱わない

  lastStep = millis();
  Serial.println("g_teleop: L1 を押しながら左スティックの上下で回す。十字キーの上下で上限");
  Serial.println("シリアル: s 止める / w 値を 1 秒読まない（ウォッチドッグの試験） / r count を 0 に");
  Serial.printf("エンコーダの A相: %s。上限 %d%%。ウォッチドッグ %lu ms\n", YELLOW_IS_A ? "黄（GPIO4）" : "緑（GPIO13）",
                maxPct, (unsigned long)WATCHDOG_MS);
  Serial.println("DualSense の PS ボタンを押す（つながらなければ、クリエイト＋PS を長押ししてペアリング）");
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t lastNag = 0;
  static uint32_t gapWindowStart = 0;
  static int32_t lastTotal = 0;

  BP32.update();
  uint32_t now = millis();
  updateEncoder();

  if (muting && (int32_t)(now - muteUntil) >= 0) {
    muting = false;
    Serial.println("w: 値を読むのに戻した");
  }
  readPad(now);

  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 's':
      case 'S':
        stopAndLatch("s を受けた", false);
        break;
      case 'w':
      case 'W':
        muting = true;
        muteUntil = now + MUTE_MS;
        Serial.println("w: DualSense の値を 1 秒読まない");
        break;
      case 'r':
      case 'R':
        total = 0;
        lastTotal = 0;
        Serial.println("count を 0 に戻した");
        break;
      default:
        break;  // 改行など
    }
  }

  // 指令ウォッチドッグ: 最後に値が届いてからの時間だけを見る
  bool fresh = (pad != nullptr) && haveData && (now - lastData <= WATCHDOG_MS);
  if (pad && !fresh && ready) {
    char why[128];  // 日本語は1文字 3 バイト
    snprintf(why, sizeof(why), "ウォッチドッグ（DualSense の値が %lu ms 届かない）", (unsigned long)(now - lastData));
    stopAndLatch(why, false);
  }

  // スティックを中央に戻したら出せるようにする
  if (!ready && fresh) {
    if (stick == 0) {
      setReady(true);
      Serial.println("準備よし（L1 を押しながらスティックで回す）");
    } else if (now - lastNag >= 2000) {
      Serial.println("スティックを中央に戻すまで出さない");
      lastNag = now;
    }
  }

  // 出したい量（‰、符号つき）。L1 を押していて、値が新しく、準備よしのときだけ
  int want = (ready && fresh && l1) ? stick * maxPct / 100 : 0;
  int wantDir = (want > 0) ? +1 : (want < 0 ? -1 : 0);

  int target = 0;
  if (wantDir == 0) {
    waitingDir = false;
  } else if (wantDir == dir) {
    waitingDir = false;
    target = abs(want);
  } else {
    // 逆に倒した: 0 にして、止まるのを待ってから向きを変える
    if (!waitingDir) {
      waitingDir = true;
      Serial.println("向きを変える: 止まるのを待つ");
    }
    if (out == 0 && now - lastNonZero >= STOP_WAIT_MS && abs(cps) < STOP_CPS) {
      dir = wantDir;
      waitingDir = false;
      applyOutput(dir, 0);
      Serial.println(dir > 0 ? "前進にした（IN1 に PWM）" : "後進にした（IN2 に PWM）");
      target = abs(want);
    }
  }

  // 強くするときは 1ms あたり 1‰（1 秒で 0→100%）、弱めるときはすぐ
  int dt = (int)(now - lastStep);
  lastStep = now;
  if (target > out) {
    out = min(target, out + dt);
  } else {
    out = target;
  }
  applyOutput(dir, out);
  if (out > 0) lastNonZero = now;

  // 0.1 秒ごと: エンコーダの速さ、保護の判定、表示
  if (now - lastPrint >= 100) {
    cps = (total - lastTotal) * 1000 / (int32_t)(now - lastPrint);
    lastTotal = total;
    lastPrint = now;

    // ロック保護: 出しているのに回らない
    if (out >= LOCK_DUTY && abs(cps) < LOCK_CPS) {
      if (lockSince == 0) lockSince = now;
      if (now - lockSince >= LOCK_MS) {
        stopAndLatch("出しているのにエンコーダが回らない（軸が止められている、またはエンコーダが数えていない）", true);
      }
    } else {
      lockSince = 0;
    }

    // 向きの見張り: 出している向きと逆に回っている
    if (out >= LOCK_DUTY && dir * cps <= -REVERSE_CPS) {
      if (reverseSince == 0) reverseSince = now;
      if (now - reverseSince >= REVERSE_MS) {
        stopAndLatch("出している向きとエンコーダの向きが逆（YELLOW_IS_A か、モータの赤・白）", true);
      }
    } else {
      reverseSince = 0;
    }

    if (now - gapWindowStart >= 1000) {
      maxGap = gapThisWindow;
      gapThisWindow = 0;
      gapWindowStart = now;
    }

    if (pad) {
      Serial.printf("L1:%d,stick:%d,max:%d,duty:%d,count:%ld,cps:%ld,gap:%lu,ready:%d\n", l1 ? 1 : 0, stick / 10,
                    maxPct, dir * out / 10, (long)total, (long)cps, (unsigned long)maxGap, ready ? 1 : 0);
    } else if (now - lastNag >= 1000) {
      Serial.printf("（未接続）count:%ld,cps:%ld\n", (long)total, (long)cps);
      lastNag = now;
    }
  }
  delay(5);
}
