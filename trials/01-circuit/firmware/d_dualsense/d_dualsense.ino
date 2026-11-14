// 組む順番 D: DualSense とつながるか、値がどのくらいの間隔で届くかを確かめる（E-5 の一部）。
// つなぐもの: C までと同じ（新しい配線なし）。USB だけ。
// ボード: esp32_bluepad32 の ESP32 Dev Module（A〜H すべて同じ）
// シリアルモニタ: 115200 bps
//
// 使い方:
//   - DualSense の「クリエイト」ボタン（左上の小さいボタン）と PS ボタンを、ライトバーが速く点滅するまで同時に長押しする。
//   - つながるとライトバーが緑になり、短く振動する。
//   - 2回目からは PS ボタンを押すだけでつながる（ESP32 がペアリングを覚えている）。
//
// 表示（0.2 秒ごと）:
//   LX・LY・RX・RY はスティック（-512〜511、上に倒すと Y は負）、L2・R2 は 0〜1023、L1・R1 は 0/1、
//   dpad は十字キー（上1・下2・右4・左8 の和）、gap は直前 1 秒で値が届く間隔の最大（ミリ秒）。
//   gap は、つながりが途切れたら止める仕組み（指令ウォッチドッグ）の待ち時間を決める材料になる。
//
// GPIO19・GPIO23（ドライバの IN1・IN2）には触らない。

#include <Bluepad32.h>

ControllerPtr pad = nullptr;
uint32_t lastData = 0;
uint32_t maxGap = 0;

void onConnected(ControllerPtr ctl) {
  if (pad != nullptr) {
    Serial.println("2台目は使わない");
    return;
  }
  pad = ctl;
  lastData = millis();
  maxGap = 0;
  ControllerProperties p = ctl->getProperties();
  Serial.printf("つながった: %s（VID 0x%04x, PID 0x%04x）\n", ctl->getModelName().c_str(), p.vendor_id,
                p.product_id);
  ctl->setColorLED(0, 255, 0);
  ctl->playDualRumble(0, 150, 0x80, 0x40);
}

void onDisconnected(ControllerPtr ctl) {
  if (ctl == pad) {
    pad = nullptr;
    Serial.println("切れた");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("Bluepad32 %s\n", BP32.firmwareVersion());
  const uint8_t* a = BP32.localBdAddress();
  Serial.printf("ESP32 の Bluetooth アドレス: %02X:%02X:%02X:%02X:%02X:%02X\n", a[0], a[1], a[2], a[3], a[4], a[5]);
  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableVirtualDevice(false);  // タッチパッドをマウスとして扱わない
  Serial.println("DualSense のクリエイト＋PS を長押ししてペアリング");
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t gapWindowStart = 0;
  static uint32_t gapThisWindow = 0;

  bool updated = BP32.update();
  uint32_t now = millis();

  if (pad && pad->isConnected() && updated && pad->hasData()) {
    uint32_t gap = now - lastData;
    if (gap > gapThisWindow) gapThisWindow = gap;
    lastData = now;
  }
  if (now - gapWindowStart >= 1000) {
    maxGap = gapThisWindow;
    gapThisWindow = 0;
    gapWindowStart = now;
  }

  if (now - lastPrint >= 200) {
    lastPrint = now;
    if (pad && pad->isConnected()) {
      Serial.printf("LX:%4d LY:%4d RX:%4d RY:%4d L2:%4d R2:%4d L1:%d R1:%d dpad:%2d gap:%lums battery:%d\n",
                    pad->axisX(), pad->axisY(), pad->axisRX(), pad->axisRY(), pad->brake(), pad->throttle(),
                    pad->l1() ? 1 : 0, pad->r1() ? 1 : 0, pad->dpad(), (unsigned long)maxGap, pad->battery());
    } else {
      Serial.println("（未接続）");
    }
  }
  delay(5);
}
