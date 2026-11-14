// 組む順番 A: 書き込めるか、Flash サイズの設定が合っているかを確かめる。
// つなぐもの: ESP32 だけ（USB）。ほかの GPIO には何もつながない。
// ボード: esp32_bluepad32 の ESP32 Dev Module（A〜H すべて同じ）
// シリアルモニタ: 115200 bps
//
// 2秒ごとに基板の情報を表示し、GPIO2 の LED を点滅させる（LED が無い基板もある。そのときは表示だけ見る）。
// GPIO19・GPIO23（ドライバの IN1・IN2）には触らない。

#include <esp_flash.h>

const int LED_PIN = 2;

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  static bool led = false;
  static uint32_t n = 0;

  // 実際の Flash の大きさは、チップの ID の下位 8 ビットから分かる（2 のべき乗）
  uint32_t id = 0;
  esp_flash_read_id(NULL, &id);
  uint32_t actual = 1UL << (id & 0xFF);
  uint32_t configured = ESP.getFlashChipSize();  // 書き込んだプログラムが想定している大きさ

  Serial.printf("[%lu] %s rev%d, %d コア, %lu MHz\n", (unsigned long)n, ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("  Flash: 実際 %lu MB / 設定 %lu MB -> %s\n", (unsigned long)(actual >> 20),
                (unsigned long)(configured >> 20),
                configured <= actual ? "OK" : "設定が実際より大きい（ツール > Flash Size を実際の値に）");

  led = !led;
  digitalWrite(LED_PIN, led);
  n++;
  delay(2000);
}
