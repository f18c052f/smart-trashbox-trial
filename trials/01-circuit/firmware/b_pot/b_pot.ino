// 組む順番 B: 可変抵抗 VR1 の読みが 0 から最大まで変わるかを確かめる。
// つなぐもの: ESP32（USB）＋ VR1（上端 +3V3、下端 SGND、ワイパ → GPIO33）
// ボード: esp32_bluepad32 の ESP32 Dev Module（A〜H すべて同じ）
// シリアルモニタ: 115200 bps。シリアルプロッタでもグラフで見られる
//
// 0.1 秒ごとに「raw（0〜4095）」「mV（ピンの電圧）」と、これまでの最小・最大を表示する。
// GPIO19・GPIO23（ドライバの IN1・IN2）には触らない。

const int POT_PIN = 33;  // BENCH_POT

int seenMin = 4095;
int seenMax = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  analogReadResolution(12);
  analogSetPinAttenuation(POT_PIN, ADC_11db);  // 0〜約3.1V を読む設定
}

void loop() {
  int raw = analogRead(POT_PIN);
  int mv = analogReadMilliVolts(POT_PIN);
  if (raw < seenMin) seenMin = raw;
  if (raw > seenMax) seenMax = raw;

  // 「名前:値」をカンマで並べると、シリアルプロッタが線で描く
  Serial.printf("raw:%d,mV:%d,min:%d,max:%d\n", raw, mv, seenMin, seenMax);
  delay(100);
}
