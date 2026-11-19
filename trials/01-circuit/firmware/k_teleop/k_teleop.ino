// 段階3 の組む順番 K・L: DualSense で3輪を回す（E-7）。十字キーの左右で回す輪を選ぶ。3輪ぶんのエンコーダのカウントも表示する。
// g_teleop を3輪に広げたもの。止める仕組みと強さの変え方は g_teleop と同じ。
// つなぐもの: J までと同じ（3輪ぶんのドライバ・モータ・エンコーダ）。VR1 は使わない（つないだままでよい）。
// ボード: esp32_bluepad32 の ESP32 Dev Module（段階2と同じ）
// シリアルモニタ: 115200 bps。行末は「改行」にする
//
// 操作（DualSense）:
//   - 十字キーの右・左で、回す輪を 輪0 → 輪1 → 輪2 → 全部 → 輪0 … と選ぶ（最初は輪0）。
//     選び直すと、全部の輪をすぐ止め、スティックを中央に戻すまで出さない。
//   - L1 を押しているあいだだけ動く（デッドマン）。離すとすぐ止まる。
//   - 左スティックの上下で前進・後進。倒した量で強さが変わる。いちばん倒して「上限」（最初は 30%）。
//     「全部」のときは3輪に同じ強さを出す。選んでいない輪には出さない。
//   - 十字キーの上・下で上限を 10% ずつ変える（10〜100%）。
//   - 速度の制御はしない（開ループ）。スティックの量をそのまま PWM のデューティにする。
//
// 止める仕組み（どれも全部の輪の出力をすぐ 0 にする）:
//   - L1 を離す。
//   - DualSense の値が WATCHDOG_MS より長く届かない（指令ウォッチドッグ）。DualSense が切れる。
//   - どれかの輪で、出しているのにエンコーダが回らない（ロック保護）。
//   - どれかの輪で、出している向きとエンコーダの向きが逆（エンコーダの向きの見張り）。
//   - シリアルモニタから s。
//   L1 を離したとき以外は、スティックを中央に戻すまで出さない。書き込み・リセットの直後、つないだ直後、輪を選び直したときも同じ。
//   ライトバーは、出せるとき緑、スティックを中央に戻すのを待っているとき赤。
//
// 強くするときは 0 から 100% まで 1 秒かけて上げる。弱めるときはすぐ。
// 向きを変えるとき（スティックを逆に倒したとき）は、輪ごとに、出力を 0 にして（IN1・IN2 とも L、モータは惰性で回る）、
// 0 になってから STOP_WAIT_MS たち、その輪のエンコーダも止まってから逆へ出す（回生で VM が上がらないように）。
//
// シリアルモニタから送る文字:
//   s  すぐ止める。スティックを中央に戻すまで出さない
//   w  DualSense の値を 1 秒間読まない（途切れたことにして、ウォッチドッグで止まるかを試す）
//   r  3輪の count を 0 に戻す
//
// 表示（0.1 秒ごと）:
//   L1    L1 を押しているか（1/0）
//   stick スティック（%。+ が上 = 前進）。上限を掛ける前の値
//   max   上限（%）
//   sel   選んでいる輪（0・1・2・all）
//   duty  3輪に出している強さ（%。+ が前進、− が後進。輪0/輪1/輪2）
//   count 3輪のエンコーダの積算（輪0/輪1/輪2）
//   cps   3輪の 1 秒あたりのカウント（輪0/輪1/輪2）
//   gap   直前 1 秒で、DualSense の値が届く間隔のいちばん長いもの（ms）
//   ready 出せる（1）か、スティックを中央に戻すのを待っている（0）か
//
// モータにつながる GPIO（19・23・21・25・22・26）を動かすのは f_bench・g_teleop・i_bench・k_teleop だけ。

#include <Bluepad32.h>
#include <driver/pcnt.h>

// ---- エンコーダの A相（輪ごと。i_bench と同じ表にする） ----
// 正転（軸側から見て時計回り）で count が増えたら true（黄が A相）、減ったら false（緑が A相）。
const bool YELLOW_IS_A[3] = {true, true, true};

// ---- 輪ごとのピンと状態 ----
struct Wheel {
  int in1Pin;     // ドライバの IN1（前進）
  int in2Pin;     // ドライバの IN2（後進）
  int chIn1;      // PWM のチャネル
  int chIn2;
  int yellowPin;  // エンコーダの黄（R10・R12・R14 経由）
  int greenPin;   // エンコーダの緑（R11・R13・R15 経由）
  pcnt_unit_t unit;
  // エンコーダ
  int16_t lastRaw;
  int32_t total;
  int32_t lastTotal;
  int32_t cps;
  // 出力
  int dir;               // +1 前進、−1 後進
  int out;               // 出している強さ（0〜1000、‰）
  uint32_t lastNonZero;  // 出力が最後に 0 でなかった時刻
  uint32_t lockSince;    // ロックの条件が始まった時刻（0 は条件が無い）
  uint32_t reverseSince;
  bool waitingDir;  // 向きを変えるのを待っているか
};

Wheel wheels[3] = {
    {19, 23, 0, 1, 4, 13, PCNT_UNIT_0, 0, 0, 0, 0, +1, 0, 0, 0, 0, false},   // 輪0: U1・M0
    {21, 25, 2, 3, 14, 16, PCNT_UNIT_1, 0, 0, 0, 0, +1, 0, 0, 0, 0, false},  // 輪1: U2・M1
    {22, 26, 4, 5, 17, 18, PCNT_UNIT_2, 0, 0, 0, 0, +1, 0, 0, 0, 0, false},  // 輪2: U3・M2
};

const int SEL_ALL = 3;

// ---- PWM（f_bench・g_teleop と同じ） ----
const uint32_t PWM_FREQ = 20000;               // 20kHz（耳に聞こえない）
const uint8_t PWM_BITS = 10;
const uint32_t PWM_MAX = (1 << PWM_BITS) - 1;  // 1023。ledcWrite はこの値のとき H のままにする

// ---- スティック（g_teleop と同じ） ----
const int STICK_DEADZONE = 40;  // 中央から ±40（全体の約 8%）は 0 とみなす。スティックは -512〜511
const int MAX_START = 30;       // 上限の最初の値（%）
const int MAX_STEP = 10;        // 十字キーで変える幅（%）

// ---- 止める仕組み（g_teleop と同じ） ----
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
const int16_t LIMIT = 30000;

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
bool ready = false;      // スティックを中央に戻したか（出してよいか）
int maxPct = MAX_START;  // 上限（%）
int sel = 0;             // 回す輪（0・1・2、SEL_ALL は全部）
uint32_t lastStep = 0;

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

// 全部の輪をすぐ止め、スティックを中央に戻すまで出さない
void stopAll() {
  for (int k = 0; k < 3; k++) {
    Wheel& w = wheels[k];
    w.out = 0;
    applyOutput(w, w.dir, 0);
    w.lockSince = 0;
    w.reverseSince = 0;
    w.waitingDir = false;
  }
  setReady(false);
}

void stopAndLatch(const char* why, bool rumble) {
  stopAll();
  Serial.printf("止めた: %s。スティックを中央に戻すまで出さない\n", why);
  if (rumble && pad) pad->playDualRumble(0, 300, 0xff, 0xff);
}

void selectWheel(int s) {
  sel = s;
  stopAll();
  if (sel == SEL_ALL) {
    Serial.println("選んだ: 全部（3輪に同じ強さを出す）。スティックを中央に戻すまで出さない");
  } else {
    Serial.printf("選んだ: 輪%d。スティックを中央に戻すまで出さない\n", sel);
  }
  if (pad) pad->playDualRumble(0, 80, 0x60, 0x30);
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
  if (pressed & DPAD_RIGHT) selectWheel((sel + 1) % 4);
  if (pressed & DPAD_LEFT) selectWheel((sel + 3) % 4);
}

// 1輪ぶん: 出したい量（‰、符号つき）から、向きと強さを決めて出す
void driveWheel(int k, int want, uint32_t now, int dt) {
  Wheel& w = wheels[k];
  int wantDir = (want > 0) ? +1 : (want < 0 ? -1 : 0);

  int target = 0;
  if (wantDir == 0) {
    w.waitingDir = false;
  } else if (wantDir == w.dir) {
    w.waitingDir = false;
    target = abs(want);
  } else {
    // 逆に倒した: 0 にして、止まるのを待ってから向きを変える
    if (!w.waitingDir) {
      w.waitingDir = true;
      Serial.printf("輪%d 向きを変える: 止まるのを待つ\n", k);
    }
    if (w.out == 0 && now - w.lastNonZero >= STOP_WAIT_MS && abs(w.cps) < STOP_CPS) {
      w.dir = wantDir;
      w.waitingDir = false;
      applyOutput(w, w.dir, 0);
      Serial.printf("輪%d %s\n", k, w.dir > 0 ? "前進にした（IN1 に PWM）" : "後進にした（IN2 に PWM）");
      target = abs(want);
    }
  }

  // 強くするときは 1ms あたり 1‰（1 秒で 0→100%）、弱めるときはすぐ
  if (target > w.out) {
    w.out = min(target, w.out + dt);
  } else {
    w.out = target;
  }
  applyOutput(w, w.dir, w.out);
  if (w.out > 0) w.lastNonZero = now;
}

// 0.1 秒ごと: 1輪ぶんのロック保護と向きの見張り。止めたら true
bool checkWheel(int k, uint32_t now) {
  Wheel& w = wheels[k];
  char why[200];  // 日本語は1文字 3 バイト

  // ロック保護: 出しているのに回らない
  if (w.out >= LOCK_DUTY && abs(w.cps) < LOCK_CPS) {
    if (w.lockSince == 0) w.lockSince = now;
    if (now - w.lockSince >= LOCK_MS) {
      snprintf(why, sizeof(why), "輪%d で、出しているのにエンコーダが回らない（軸が止められている、またはエンコーダが数えていない）", k);
      stopAndLatch(why, true);
      return true;
    }
  } else {
    w.lockSince = 0;
  }

  // 向きの見張り: 出している向きと逆に回っている
  if (w.out >= LOCK_DUTY && w.dir * w.cps <= -REVERSE_CPS) {
    if (w.reverseSince == 0) w.reverseSince = now;
    if (now - w.reverseSince >= REVERSE_MS) {
      snprintf(why, sizeof(why), "輪%d で、出している向きとエンコーダの向きが逆（YELLOW_IS_A か、モータの赤・白）", k);
      stopAndLatch(why, true);
      return true;
    }
  } else {
    w.reverseSince = 0;
  }
  return false;
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

  for (int k = 0; k < 3; k++) setupEncoder(k);

  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableVirtualDevice(false);  // タッチパッドをマウスとして扱わない

  lastStep = millis();
  Serial.println("k_teleop: L1 を押しながら左スティックの上下で回す。十字キーの左右で輪を選ぶ（輪0→輪1→輪2→全部）、上下で上限");
  Serial.println("シリアル: s 止める / w 値を 1 秒読まない（ウォッチドッグの試験） / r count を 0 に");
  for (int k = 0; k < 3; k++) {
    Serial.printf("輪%d のエンコーダの A相: %s（GPIO%d）\n", k, YELLOW_IS_A[k] ? "黄" : "緑",
                  YELLOW_IS_A[k] ? wheels[k].yellowPin : wheels[k].greenPin);
  }
  Serial.printf("いまは輪0。上限 %d%%。ウォッチドッグ %lu ms\n", maxPct, (unsigned long)WATCHDOG_MS);
  Serial.println("DualSense の PS ボタンを押す（つながらなければ、クリエイト＋PS を長押ししてペアリング）");
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t lastNag = 0;
  static uint32_t gapWindowStart = 0;

  BP32.update();
  uint32_t now = millis();
  for (int k = 0; k < 3; k++) updateEncoder(wheels[k]);

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
        for (int k = 0; k < 3; k++) {
          wheels[k].total = 0;
          wheels[k].lastTotal = 0;
        }
        Serial.println("3輪の count を 0 に戻した");
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

  // 出したい量（‰、符号つき）。L1 を押していて、値が新しく、準備よしのときだけ。選んだ輪（全部なら3輪）にだけ出す
  int want = (ready && fresh && l1) ? stick * maxPct / 100 : 0;
  int dt = (int)(now - lastStep);
  lastStep = now;
  for (int k = 0; k < 3; k++) {
    bool active = (sel == SEL_ALL) || (sel == k);
    driveWheel(k, active ? want : 0, now, dt);
  }

  // 0.1 秒ごと: エンコーダの速さ、保護の判定、表示
  if (now - lastPrint >= 100) {
    int32_t span = (int32_t)(now - lastPrint);
    for (int k = 0; k < 3; k++) {
      Wheel& w = wheels[k];
      w.cps = (w.total - w.lastTotal) * 1000 / span;
      w.lastTotal = w.total;
    }
    lastPrint = now;

    for (int k = 0; k < 3; k++) {
      if (checkWheel(k, now)) break;  // 1つで止めたら、ほかは見なくてよい（全部止めた）
    }

    if (now - gapWindowStart >= 1000) {
      maxGap = gapThisWindow;
      gapThisWindow = 0;
      gapWindowStart = now;
    }

    if (pad) {
      Serial.printf("L1:%d,stick:%d,max:%d,sel:%s,duty:%d/%d/%d,count:%ld/%ld/%ld,cps:%ld/%ld/%ld,gap:%lu,ready:%d\n",
                    l1 ? 1 : 0, stick / 10, maxPct, selName(sel), wheels[0].dir * wheels[0].out / 10,
                    wheels[1].dir * wheels[1].out / 10, wheels[2].dir * wheels[2].out / 10, (long)wheels[0].total,
                    (long)wheels[1].total, (long)wheels[2].total, (long)wheels[0].cps, (long)wheels[1].cps,
                    (long)wheels[2].cps, (unsigned long)maxGap, ready ? 1 : 0);
    } else if (now - lastNag >= 1000) {
      Serial.printf("（未接続）sel:%s,count:%ld/%ld/%ld,cps:%ld/%ld/%ld\n", selName(sel), (long)wheels[0].total,
                    (long)wheels[1].total, (long)wheels[2].total, (long)wheels[0].cps, (long)wheels[1].cps,
                    (long)wheels[2].cps);
      lastNag = now;
    }
  }
  delay(5);
}
