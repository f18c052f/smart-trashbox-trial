// 段階3 の段 8: LED の口（J10 につなぐ WS2812B のテープ）を確かめる。
// つなぐもの: 段 7 までと同じ（G32 の分圧を含む）＋ LED の口（U6・R16・R17・C14・C15・J10）。
// ボード: esp32_bluepad32 の ESP32 Dev Module（ほかのプログラムと同じ）
// シリアルモニタ: 115200 bps、行末は「改行」
//
// モータの GPIO（19・23・21・25・22・26）には触らない。使うのは GPIO27（LED_GPIO → U6 の 1A）と GPIO32（電池電圧）だけ。
//
// 決めごと（bom.md の「LED の口」）:
//   - 12V が来ている（電池電圧の読みが 8.0V 以上）と分かってから 1 秒たつまで、LED に信号を出さない。
//     USB だけのときは U6 と LED に電源が無いので、GPIO27 は L のまま。
//   - LED の合計の電流を、点けている色から計算して 400mA 以下に抑える
//     （1個あたり: 消えていても 1mA、赤・緑・青はそれぞれ最大 20mA とみなす。多めの見積もり）。
//   - LED は 8個まで（J10 の決まり。全部を白・最大にしても U4 の 1A に収まる数）。
//
// 起動すると「レベルを見る」モード（段 8-2）。送る文字:
//   h  GPIO27 を H にする（J10-2 が 5V 近くになるはず）。12V が来ていないときはしない
//   l  GPIO27 を L にする（J10-2 が 0V）
//   s  LED を点けるモードへ進む（段 8-3。レベルを見るモードに戻るときは EN ボタンでリセット）
// LED を点けるモード:
//   1〜8  つないでいる LED の数（最初は 5）
//   r g b w  全部を赤・緑・青・白にする
//   c  1個ずつ順に点ける（J10 に近い LED から、赤 → 緑 → 青 → 次の LED）。並びと、どの LED も3色出るかを見る
//   x  全部消す
//   + -  明るさ（4 → 8 → 16 → 32 → 64 → 128 → 255。最初は 16）
// 表示（1 秒ごと）:
//   vbat    電池（安定化電源）の電圧 [V]。段階2の H-1 の式
//   v12     12V が来ているか（yes / no）。信号を出すのは yes になって 1 秒たってから
//   mode    level（レベルを見る）/ led（LED を点ける）
//   gpio27  レベルを見るモードでの GPIO27（H / L）
//   n color bright  LED の数、色、明るさ
//   est     計算した LED の合計の電流 [mA]（上限 400）

#include "driver/rmt.h"

const int LED_PIN = 27;   // LED_GPIO（R17 で GND へ引いてある）
const int VBAT_PIN = 32;  // VBAT_SENSE（ADC1）
const rmt_channel_t RMT_CH = RMT_CHANNEL_0;

const int NUM_MAX = 8;          // J10 につなぐのは 8個まで
const float BUDGET_MA = 400.0;  // LED の合計の上限
const float IDLE_MA = 1.0;      // 消えている LED 1個の電流
const float CH_MA = 20.0;       // 赤・緑・青 1色を最大にしたときの電流

const float VBAT_ON_V = 8.0;        // これ以上なら 12V が来ているとみなす（電池の使い切り 9.0V より下）
const uint32_t VM_SETTLE_MS = 1000; // 12V が来てから信号を出すまで待つ（U4 の起動は軽い負荷で。データシート p.20）

// 段階2の H-1 の係数（この ESP32 と R1・R2 の組み合わせのもの）
float vbatFromMv(float mv) { return 5.472 * mv / 1000.0 - 0.090; }

// WS2812B の信号の形。RMT は 80MHz を 2 で割り、1 tick = 25ns
//   0: H 0.40µs・L 0.85µs、1: H 0.80µs・L 0.45µs（データシートの ±0.15µs の中）
const uint16_t T0H = 16, T0L = 34, T1H = 32, T1L = 18;

enum Mode { MODE_LEVEL, MODE_LED };
Mode mode = MODE_LEVEL;
bool levelHigh = false;

int numLeds = 5;
const uint8_t BRIGHT_STEPS[] = {4, 8, 16, 32, 64, 128, 255};
const int BRIGHT_COUNT = sizeof(BRIGHT_STEPS) / sizeof(BRIGHT_STEPS[0]);
int brightIdx = 2;
char color = 'x';  // r g b w c x
int chaseStep = 0;

uint8_t frame[NUM_MAX][3];  // R, G, B（送るときに G, R, B の順にする）
rmt_item32_t items[NUM_MAX * 24];
float estMa = 0;
bool capped = false;

float vbat = 0;
bool vmOn = false;
uint32_t vmOnSince = 0;

bool ready(uint32_t now) { return vmOn && (now - vmOnSince >= VM_SETTLE_MS); }

void readVbat() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(VBAT_PIN);
  vbat = vbatFromMv(sum / 8.0);
}

void rmtStart() {
  rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)LED_PIN, RMT_CH);
  cfg.clk_div = 2;
  cfg.mem_block_num = 4;  // 256 項目。8個 × 24 ビット = 192 が全部入る
  cfg.tx_config.idle_output_en = true;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  ESP_ERROR_CHECK(rmt_config(&cfg));
  ESP_ERROR_CHECK(rmt_driver_install(RMT_CH, 0, 0));
}

// いまの色・明るさ・数から frame を作り、上限を超えるなら全体を暗くする
void buildFrame() {
  uint8_t b = BRIGHT_STEPS[brightIdx];
  for (int i = 0; i < NUM_MAX; i++) frame[i][0] = frame[i][1] = frame[i][2] = 0;
  for (int i = 0; i < numLeds; i++) {
    uint8_t r = 0, g = 0, bl = 0;
    switch (color) {
      case 'r': r = b; break;
      case 'g': g = b; break;
      case 'b': bl = b; break;
      case 'w': r = g = bl = b; break;
      case 'c':
        if (i == (chaseStep / 3) % numLeds) {
          int c = chaseStep % 3;
          if (c == 0) r = b; else if (c == 1) g = b; else bl = b;
        }
        break;
      default: break;
    }
    frame[i][0] = r; frame[i][1] = g; frame[i][2] = bl;
  }
  // 合計の電流（つないでいる numLeds 個ぶん）
  float sumCh = 0;
  for (int i = 0; i < numLeds; i++) sumCh += frame[i][0] + frame[i][1] + frame[i][2];
  float lit = sumCh / 255.0 * CH_MA;
  float idle = numLeds * IDLE_MA;
  capped = false;
  if (idle + lit > BUDGET_MA && lit > 0) {
    float f = (BUDGET_MA - idle) / lit;
    for (int i = 0; i < numLeds; i++)
      for (int c = 0; c < 3; c++) frame[i][c] = (uint8_t)(frame[i][c] * f);
    lit *= f;
    capped = true;
  }
  estMa = idle + lit;
}

// NUM_MAX 個ぶん送る（numLeds より先は消す。数を減らしたときに前の色が残らないように）
void sendFrame() {
  int k = 0;
  for (int i = 0; i < NUM_MAX; i++) {
    uint8_t grb[3] = {frame[i][1], frame[i][0], frame[i][2]};
    for (int c = 0; c < 3; c++) {
      for (int bit = 7; bit >= 0; bit--) {
        bool one = grb[c] & (1 << bit);
        items[k].level0 = 1;
        items[k].duration0 = one ? T1H : T0H;
        items[k].level1 = 0;
        items[k].duration1 = one ? T1L : T0L;
        k++;
      }
    }
  }
  rmt_write_items(RMT_CH, items, k, true);
  delayMicroseconds(300);  // 区切り（L を 280µs 以上。新しい版の WS2812B に合わせる）
}

void printHelp() {
  if (mode == MODE_LEVEL) {
    Serial.println("レベルを見るモード: h = GPIO27 を H、l = L、s = LED を点けるモードへ");
  } else {
    Serial.println("LED を点けるモード: 1〜8 = 数、r g b w = 色、c = 1個ずつ、x = 消す、+ - = 明るさ");
  }
}

void handleKey(char k, uint32_t now) {
  if (k == '\n' || k == '\r' || k == ' ') return;
  if (mode == MODE_LEVEL) {
    if (k == 'h') {
      if (!ready(now)) {
        Serial.println("12V が来ていないので H にしない（U6 に電源が無い）");
        return;
      }
      digitalWrite(LED_PIN, HIGH);
      levelHigh = true;
      Serial.println("GPIO27 を H にした。J10-2 − J10-3 を測る（4.5V 以上で合格）");
    } else if (k == 'l') {
      digitalWrite(LED_PIN, LOW);
      levelHigh = false;
      Serial.println("GPIO27 を L にした。J10-2 − J10-3 を測る（0.1V 以下で合格）");
    } else if (k == 's') {
      digitalWrite(LED_PIN, LOW);
      levelHigh = false;
      rmtStart();
      mode = MODE_LED;
      color = 'x';
      Serial.println("LED を点けるモードにした（戻るときは EN ボタンでリセット）");
      printHelp();
    } else {
      printHelp();
    }
    return;
  }
  // LED を点けるモード
  if (k >= '1' && k <= '8') {
    numLeds = k - '0';
    chaseStep = 0;
    Serial.printf("LED の数を %d にした\n", numLeds);
  } else if (k == 'r' || k == 'g' || k == 'b' || k == 'w' || k == 'c' || k == 'x') {
    color = k;
    chaseStep = 0;
  } else if (k == '+') {
    if (brightIdx < BRIGHT_COUNT - 1) brightIdx++;
  } else if (k == '-') {
    if (brightIdx > 0) brightIdx--;
  } else if (k == 'h' || k == 'l') {
    Serial.println("LED を点けるモードでは使えない（EN ボタンでリセットすると、レベルを見るモードに戻る）");
  } else {
    printHelp();
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // 最初は L（R17 でも L に引いてある）
  Serial.begin(115200);
  delay(500);
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);
  Serial.println("l_led: LED の口（J10 の WS2812B）を確かめる。モータの GPIO には触らない");
  Serial.printf("決めごと: 12V が来て %lu ms たってから信号を出す。LED は %d 個まで、合計 %.0f mA まで\n",
                (unsigned long)VM_SETTLE_MS, NUM_MAX, BUDGET_MA);
  printHelp();
}

void loop() {
  static uint32_t lastVbat = 0, lastSend = 0, lastChase = 0, lastPrint = 0;
  uint32_t now = millis();

  if (now - lastVbat >= 50) {
    lastVbat = now;
    readVbat();
    bool on = vbat >= VBAT_ON_V;
    if (on && !vmOn) {
      vmOn = true;
      vmOnSince = now;
      Serial.println("12V が来た（1 秒後から LED に信号を出す）");
    } else if (!on && vmOn) {
      vmOn = false;
      Serial.println("12V が無い（LED に信号を出さない）");
    }
  }

  // 12V が無いあいだは GPIO27 を L に保つ（レベルを見るモード）
  if (mode == MODE_LEVEL && levelHigh && !ready(now)) {
    digitalWrite(LED_PIN, LOW);
    levelHigh = false;
    Serial.println("12V が無いので GPIO27 を L に戻した");
  }

  while (Serial.available()) handleKey((char)Serial.read(), now);

  if (mode == MODE_LED) {
    if (color == 'c' && now - lastChase >= 400) {
      lastChase = now;
      chaseStep = (chaseStep + 1) % (numLeds * 3);
    }
    if (ready(now) && now - lastSend >= 20) {  // 50 回/秒
      lastSend = now;
      buildFrame();
      sendFrame();
    }
  }

  if (now - lastPrint >= 1000) {
    lastPrint = now;
    if (mode == MODE_LED) buildFrame();  // 表示用（12V が無いときも計算だけする）
    Serial.printf("vbat:%.2f,v12:%s,mode:%s,gpio27:%s,n:%d,color:%c,bright:%d,est:%.0fmA%s\n",
                  vbat, ready(now) ? "yes" : (vmOn ? "wait" : "no"),
                  mode == MODE_LEVEL ? "level" : "led",
                  mode == MODE_LEVEL ? (levelHigh ? "H" : "L") : "-",
                  numLeds, color, BRIGHT_STEPS[brightIdx], mode == MODE_LED ? estMa : 0.0,
                  capped ? "（上限で暗くした）" : "");
  }
  delay(1);
}
