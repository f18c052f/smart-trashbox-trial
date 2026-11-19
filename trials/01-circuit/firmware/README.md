# 段階2・3の使い捨てファーム

段階2は [bom.md の「組む順番と電流制限」](../kicad/stage2-one-wheel/specs/bom.md#組む順番と電流制限) の A〜H、
段階3は組み終えた回路を確かめる [stage3-procedure.md](../stage3-procedure.md) の段 0〜8（8 は LED の口）で、
ESP32 に書き込むプログラム。結果は [notes.md](../notes.md) に書く。
配線と測る値を含めた段ごとの手順は [stage2-procedure.md](../stage2-procedure.md) と [stage3-procedure.md](../stage3-procedure.md)。

| 順 | プログラム | 状態 |
|---|---|---|
| A | [a_hello](a_hello/a_hello.ino) | ビルド確認済み |
| B | [b_pot](b_pot/b_pot.ino) | ビルド確認済み |
| C | [c_encoder](c_encoder/c_encoder.ino) | ビルド確認済み |
| D | [d_dualsense](d_dualsense/d_dualsense.ino) | ビルド確認済み |
| E | （プログラムは要らない。ESP32 には何も足さない） | — |
| F・G-1・G-2 | [f_bench](f_bench/f_bench.ino)（つまみで IN1/IN2 を PWM。エンコーダも表示） | ビルド確認済み |
| G-3 | [g_teleop](g_teleop/g_teleop.ino)（DualSense で輪1個。エンコーダ） | ビルド確認済み |
| H | [h_vbat](h_vbat/h_vbat.ino)（GPIO32 で電池電圧） | ビルド確認済み |
| 段階3 の 2〜5 | [i_bench](i_bench/i_bench.ino)（f_bench を3輪に。シリアルで選んだ輪、または3輪全部をつまみで回す） | ビルド確認済み |
| 段階3 の 6 | [k_teleop](k_teleop/k_teleop.ino)（g_teleop を3輪に。十字キーの左右で輪を選ぶ） | ビルド確認済み |
| 段階3 の 7 | h_vbat（段階2と同じ） | ビルド確認済み |
| 段階3 の 8 | [l_led](l_led/l_led.ino)（LED の口。GPIO27 の H・L と、WS2812B の色・明るさをシリアルで選ぶ） | ビルド確認済み（2026-11-20） |

A〜D・h_vbat・l_led はモータにつながる GPIO（19・23・21・25・22・26）に触らない。モータを動かすのは f_bench・g_teleop・i_bench・k_teleop だけ。

## 環境

- Arduino IDE 2.3.10（スタートメニューの「Arduino IDE」）
- ボード: **esp32_bluepad32 の ESP32 Dev Module**（ボードパッケージ esp32_bluepad32 4.1.0。土台は arduino-esp32 2.0 系 / ESP-IDF 4.4）
  - Flash Size は 4MB のまま（A で実物と合っているかを確かめる）。ほかの設定も変えない
- すべて同じボードで書く。G・K は DualSense・エンコーダ・PWM を1つのプログラムで使うので、Bluepad32 入りのボードでしか書けない。
  A〜C で同じ環境を先に試しておく

## 書き込みの手順（毎回同じ）

1. ESP32 を USB で PC につなぐ。
2. Arduino IDE で「ファイル」→「開く」から、そのプログラムの `.ino` を開く。
3. 「ツール」→「ボード」→「esp32_bluepad32」→「ESP32 Dev Module」を選ぶ（一度選べば次からは残る）。
   **「esp32」の下にも同じ名前の「ESP32 Dev Module」がある。こちらを選ぶと、D・G・K で `Bluepad32.h: No such file or directory` になる。**
   A〜C はどちらでもビルドが通ってしまうので、毎回 esp32_bluepad32 の下から選び直す。
4. 「ツール」→「ポート」で ESP32 の COM ポートを選ぶ。
5. 左上の「→」（書き込み）を押す。下の欄に `Connecting....` が出て止まったら、基板の **BOOT** ボタンを押したままにし、
   数字（%）が進み始めたら離す。
6. 「ツール」→「シリアルモニタ」を開き、右下の速さを **115200 baud** にする。C・F・G・I・K は左下の行末を「改行」にする。

## 各段で見ること

| 順 | 見ること | 合格 | notes.md に書くこと |
|---|---|---|---|
| A | 2秒ごとの表示と LED の点滅 | 書き込める。`Flash: 実際 4 MB / 設定 4 MB -> OK` | 実際の Flash の大きさ、チップの rev |
| B | `raw` と `mV` | つまみを回しきると raw が 0 付近から 4095 まで変わる（`min`・`max` に残る） | min・max |
| C | `count` | テスターで TP5（エンコーダの青 − 黒）が約 3.3V。手で回すと count が増減する。10 回転で count がほぼ同じになる | 10 回転の count（何回か）、正転で増えたか減ったか → A相の色 |
| D | ペアリングと表示 | つながるとライトバーが緑。スティック・L1 の値が変わる。DualSense の電源を切ると「切れた」。PS ボタンでつなぎ直せる | つながったか、`gap` のふだんの値と最大、遅れの感じ |
| F・G-1・G-2 | `pot`・`duty`・`count`・`ready`。送る文字 `f`・`b`・`s`・`r` | つまみを 0 に戻すまで出さない。前進で赤（G では正転で `count` が増える）、`b` で緑。動いている間は向きを変えない。`s` ですぐ止まる | 手順書の F・G の記録 |
| 段階3 の 2〜5 | `wheel`・`count`（輪0/輪1/輪2）。送る文字 `0`・`1`・`2`・`a`・`f`・`b`・`s`・`r` | 選んだ輪だけが回る。正転で、その輪の `count` が増える | 手順書の段 2〜5 の記録 |
| 段階3 の 6 | `sel`・`duty`・`count`・`cps`・`gap` | 十字キーで選んだ輪だけが回る。止める仕組みが3輪とも効く | 手順書の段 6 の記録 |
| 段階3 の 8 | `v12`・`mode`・`gpio27`・`n`・`color`・`bright`・`est`。送る文字 `h`・`l`・`s`、`1`〜`8`・`r`・`g`・`b`・`w`・`c`・`x`・`+`・`-` | `h` で J10-2 が 4.5V 以上、`l` で 0.1V 以下。`c` で J10 に近い LED から赤 → 緑 → 青。12V が無いと信号を出さない | 手順書の段 8 の記録 |

- l_led は、12V が来ている（GPIO32 の読みで 8.0V 以上）と分かって 1 秒たつまで LED に信号を出さない。LED の合計は 400mA まで（計算で明るさを抑える）。G32 の分圧（段 7）がつながっていること。
- l_led の信号は ESP32 の RMT（LED 用の信号を作る回路）で作る。ライブラリは足していない（ボードパッケージに入っている ESP-IDF の `driver/rmt.h` を使う）。

- C では R10・R11 は無くてもよい（12V が無いので）。モータの赤・白はどこにもつながない。段階3の段 2（手で数える）も同じ。
- C の「正転」は段階1の E-2 で決めた向き: **軸側（タイヤ側）から見て時計回り**（[notes.md](../notes.md) の記録）。
  正転で count が増えれば黄が A相、減れば緑が A相。
- f_bench・g_teleop の先頭の `YELLOW_IS_A` は C の結果（正転で `count` が増えたら `true`、減ったら `false`）。
- i_bench・k_teleop の先頭の `YELLOW_IS_A[3]` は輪0・輪1・輪2 の順。段階3の段 2 で3輪とも確かめる（輪0 は段階2の C では黄が A相）。2つのプログラムで同じにする。
- i_bench は、書き込み・リセット・USB の抜き挿しのあと**輪0 に戻る**。段階3では 12V を入れる前に毎回 `wheel:` を見る。
- D の DualSense のペアリング: 「クリエイト」ボタン（左上の小さいボタン）と PS ボタンを、ライトバーが速く点滅するまで同時に長押しする。
