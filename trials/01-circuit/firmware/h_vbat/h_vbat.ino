// 組む順番 H: GPIO32（VBAT_SENSE）で電池電圧を読む。段階2では電池の代わりに安定化電源の電圧を読む。
// つなぐもの: G までと同じ ＋ VBAT_SENSE の列 → GPIO32。モータの赤・白は J2 から抜く。
// ボード: esp32_bluepad32 の ESP32 Dev Module（A〜H すべて同じ）
// シリアルモニタ: 115200 bps
//
// H-1（USB を挿したまま）: 安定化電源の電圧を変え、表示の pinmV と vbat を、電源の表示と並べて書き留める。
//   この表から、読みを電池の電圧に直す係数を決める（係数は段階3のファームで使う）。
// H-2（USB を抜き、5V 電源 U4 から動かす）: 表示は見えない。GPIO2 の LED が 0.5 秒ごとに点滅すれば動いている。
//
// 表示（0.5 秒ごと。そのあいだ 2ms ごとに読んだ約 250 回ぶん）:
//   raw    ADC の読み（0〜4095）の平均
//   pinmV  ピンの電圧（mV）の平均。チップに書き込まれた補正値で直した値
//   min・max  pinmV の最小・最大（読みのばらつき）
//   vbat   pinmV を分圧の比 22/(100+22) で割り戻した電圧（V）。係数を決める前の計算値
//
// GPIO19・GPIO23（ドライバの IN1・IN2）には触らない（ドライバの中のプルダウンで L のまま）。

const int VBAT_PIN = 32;  // VBAT_SENSE（ADC1）
const int LED_PIN = 2;

// 分圧: R1 100kΩ（VM 側）、R2 22kΩ（SGND 側）。H-1 の結果で係数を決めるまでは計算どおりの値
const float R1_KOHM = 100.0;
const float R2_KOHM = 22.0;
const float SCALE = (R1_KOHM + R2_KOHM) / R2_KOHM;  // 5.545。ピンの電圧 × SCALE = 電池の電圧

const uint32_t SAMPLE_MS = 2;
const uint32_t PRINT_MS = 500;

uint32_t sumRaw = 0;
uint32_t sumMv = 0;
uint32_t n = 0;
int minMv = 100000;
int maxMv = -1;

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);  // 0〜約3.1V を読む設定（まっすぐ読めるのは約 2.45V まで）
  Serial.println("h_vbat: GPIO32 の電圧と、分圧の比で割り戻した電圧を 0.5 秒ごとに表示する");
  Serial.printf("分圧 %.0fk / %.0fk、比 %.4f（vbat = pinmV × %.3f）\n", R1_KOHM, R2_KOHM, 1.0 / SCALE, SCALE);
}

void loop() {
  static uint32_t lastSample = 0;
  static uint32_t lastPrint = 0;
  static bool led = false;

  uint32_t now = millis();

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    int raw = analogRead(VBAT_PIN);
    int mv = analogReadMilliVolts(VBAT_PIN);
    sumRaw += raw;
    sumMv += mv;
    n++;
    if (mv < minMv) minMv = mv;
    if (mv > maxMv) maxMv = mv;
  }

  if (now - lastPrint >= PRINT_MS && n > 0) {
    lastPrint = now;
    float raw = (float)sumRaw / n;
    float mv = (float)sumMv / n;
    float vbat = mv / 1000.0 * SCALE;
    Serial.printf("raw:%.0f,pinmV:%.1f,min:%d,max:%d,vbat:%.3f,n:%lu\n", raw, mv, minMv, maxMv, vbat, (unsigned long)n);
    if (mv > 2450) Serial.println("  注意: 2.45V を超えている（ADC がまっすぐ読めない範囲。3.3V を超えるとピンが壊れる）");

    sumRaw = 0;
    sumMv = 0;
    n = 0;
    minMv = 100000;
    maxMv = -1;

    led = !led;
    digitalWrite(LED_PIN, led);
  }
  delay(1);
}
