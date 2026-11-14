// 組む順番 C: エンコーダのカウント、A相の色、出力軸1回転あたりのカウントを調べる（E-6 の一部）。
// つなぐもの: ESP32（USB）＋ VR1 ＋ J3（エンコーダ: 青 +3V3、黒 SGND、黄 → R10 → GPIO4、緑 → R11 → GPIO13）
//            R10・R11 は無くてもよい（12V が無いので）。モータの赤・白はどこにもつながない。
// ボード: esp32_bluepad32 の ESP32 Dev Module（A〜H すべて同じ）
// シリアルモニタ: 115200 bps。行末は「改行」か「CRおよびLF」にする
//
// 使い方:
//   - シリアルモニタで r を送るとカウントを 0 に戻す。
//   - 出力軸に印を付け、r を送ってから「正転」（E-2 で決めた向き。軸側から見て時計回り）に手でちょうど 10 回転させる。
//     count を 10 で割ったものが出力軸1回転あたりのカウント（4逓倍。目安 11 × 19 × 4 ≒ 836）。
//   - 正転で count が増えれば黄が A相、減れば緑が A相（そのときは F 以降でピンを入れ替える）。
//
// GPIO19・GPIO23（ドライバの IN1・IN2）には触らない。

#include <driver/pcnt.h>

const int ENC_A_PIN = 4;   // ENC0_A_IN（黄、R10 経由）
const int ENC_B_PIN = 13;  // ENC0_B_IN（緑、R11 経由）
const pcnt_unit_t UNIT = PCNT_UNIT_0;
const int16_t LIMIT = 30000;  // カウンタはここに届くと 0 に戻る。差を取って積算する

int16_t lastRaw = 0;
int32_t total = 0;

void setupEncoder() {
  // 4逓倍（A・B 両方の立ち上がり・立ち下がりを数える）。A が B より先に変わる向きで増える
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

  // 内部プルアップ（約45kΩ）。エンコーダの出力の形式が分からないので入れておく
  gpio_pullup_en((gpio_num_t)ENC_A_PIN);
  gpio_pullup_en((gpio_num_t)ENC_B_PIN);

  // 12.5µs より短いひげ（ノイズ）を無視する。正転の全速でも山と山の間は 200µs 以上ある
  pcnt_set_filter_value(UNIT, 1000);
  pcnt_filter_enable(UNIT);

  pcnt_counter_pause(UNIT);
  pcnt_counter_clear(UNIT);
  pcnt_counter_resume(UNIT);
}

// 前回からの差を取り、カウンタが 0 に戻った分を補って積算する
void updateEncoder() {
  int16_t raw = 0;
  pcnt_get_counter_value(UNIT, &raw);
  int32_t d = (int32_t)raw - lastRaw;
  if (d < -LIMIT / 2) d += LIMIT;
  if (d > LIMIT / 2) d -= LIMIT;
  total += d;
  lastRaw = raw;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  setupEncoder();
  Serial.println("c_encoder: r で 0 に戻す");
}

void loop() {
  static uint32_t lastPrint = 0;
  static int32_t lastTotal = 0;

  updateEncoder();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      total = 0;
      lastTotal = 0;
      Serial.println("count を 0 に戻した");
    }
  }

  uint32_t now = millis();
  if (now - lastPrint >= 100) {
    int32_t cps = (total - lastTotal) * 1000 / (int32_t)(now - lastPrint);  // 1秒あたりのカウント
    Serial.printf("count:%ld,cps:%ld,A:%d,B:%d\n", (long)total, (long)cps, digitalRead(ENC_A_PIN),
                  digitalRead(ENC_B_PIN));
    lastTotal = total;
    lastPrint = now;
  }
  delay(5);
}
