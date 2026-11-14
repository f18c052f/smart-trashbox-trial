# smart-trashbox-trial

投げられたゴミの落下地点を予測し、床を走るゴミ箱が受け止めるシステムのトライアル用リポジトリ。
入口は [docs/README.md](docs/README.md)、計画は [docs/trial-plan.md](docs/trial-plan.md)。

## ドキュメントの置き方

- `docs/inherited/` は旧リポジトリ `smart-trashbox-tribute` から引き継いだ記録。**書き換えも追加もしない。**
- 新しく分かったことは `docs/` の直下に書き、置き換えた `inherited/` の文書へリンクする。
- トライアルの記録は `trials/<番号>-<名前>/` に置く（例: `trials/01-circuit/notes.md`）。

## 回路の判断の進め方

持ち主は回路図の初心者。判断を丸投げしない。

- **電気的な決めごとは Claude が決める**（部品の値、結線、保護、図の描き方など）。根拠はデータシートと
  `docs/inherited/` の資料。聞くのは Claude には分からないこと（手元に何があるか、測った結果、実物の置き方、好み）だけ。
- 聞くときは選択肢を並べるだけにせず、**おすすめと理由を必ず付ける**。「おすすめで」と返されたらそれで進める。
- 安全に関わること（短絡、発熱、バッテリ、電源投入の順序）は、何が起きうるかを一言添えて安全側をすすめる。
- 専門用語には短い説明を付ける。
- 決めたことと理由は `trials/01-circuit/notes.md` に残す。
- 持ち主が回路図を細かく検証できない前提で、kicad プラグインのレビュー用エージェント
  （`kicad:bom-reviewer`、`kicad:schematic-plan-reviewer`）と ERC を必ず通す。

## KiCad

- プロジェクトは回路図の段階ごとに分ける（[trial-plan.md](docs/trial-plan.md) の「回路図を作る順番」）。
  前の段階のプロジェクトは書き換えない。
  - 段階1: `trials/01-circuit/kicad/stage1-motor/stage1-motor.kicad_pro`
  - 段階2: `trials/01-circuit/kicad/stage2-one-wheel/stage2-one-wheel.kicad_pro`
  - 段階3（完成形）: `trials/01-circuit/kicad/mobile-base/mobile-base.kicad_pro`（回路図 `mobile-base.kicad_sch`）
- 自作の部品記号は `trials/01-circuit/kicad/lib/mobile-base.kicad_sym` に置き、全段階で共有する。
  各プロジェクトの `sym-lib-table` から `${KIPRJMOD}/../lib/mobile-base.kicad_sym` で参照する。
- 部品表と配置の計画は各プロジェクトの `specs/`（`bom.md`、`schematic-plan.md`）に置く。
- **回路図だけを作る。PCB は作らない**（ユニバーサル基板で組む）。KiCad が作った空の
  `mobile-base.kicad_pcb` はあるが使わない。フットプリントの割り当てや PCB 関係のツールは使わない。
- KiCad 10。操作は kicad プラグインの MCP ツールで行い、ツールには上のファイルの絶対パス
  （`C:\Users\user\repos\smart-trashbox-trial\trials\01-circuit\kicad\...`）を渡す。
- 作図の MCP ツールの癖（段階1で試して分かったこと。詳しくは `stage1-motor/specs/schematic-plan.md`）:
  - ネット名は [circuit.md](docs/inherited/circuit.md) に合わせ、**ラベル**で付ける。電源記号は部品番号（`#PWR01`）が
    図に出て隠せないので使わない。
  - `place_component` は部品番号と値を部品の中心から上下 3.81mm に中央揃えで置き、動かすツールが無い。
    縦に置く部品は 90° で置いてから `move_component` で 0° に回すと、文字が左右に来る。重なる値は Value を `~` にし、
    値は項目 `Spec` に入れ、図には `add_text` で書く。
  - `add_text` は行ごとに中央揃え。注記は1行ずつ置く。
  - `connect_pins` はつないだ端に `Net-(J1-1)` のような余分なラベルを置くので使わない。線は座標を決めて `add_wires` で引く。
  - 計画を書く前に、一時フォルダの別プロジェクトに同じ座標で試し描きし、ERC と SVG（文字の重なり）を確かめる。
  - 段階2で分かったこと（詳しくは `stage2-one-wheel/specs/schematic-plan.md`）: 文字の向きは部品の最後の回転と同じになる
    （縦のダイオードは 0° で置いて 270° に回すと文字が横に出る）。ラベルだけにつながる短い線はラベルの位置で終える
    （線の先が空くと ERC の警告になる）。部品の外形（ピンの先まで）どうしを触れさせない（KiCad が文字を二度描く）。
- ERC のレポートなど書き出したものの置き場は、残す必要があるときだけ決める。試しに流すだけなら一時フォルダに出す。

## ファーム

- 段階2の使い捨てのファームは `trials/01-circuit/firmware/<順>_<名前>/<順>_<名前>.ino`（入口は同じ場所の `README.md`）。
- Arduino IDE 2（winget で入れた。`%LOCALAPPDATA%\Microsoft\WinGet\Packages\ArduinoSA.IDE.stable_*\`）、ボードは
  `esp32-bluepad32:esp32:esp32`（esp32_bluepad32 4.1.0 の ESP32 Dev Module。arduino-esp32 2.0 系 / ESP-IDF 4.4 の API で書く）。
- 渡す前に、IDE に入っている `resources\app\lib\backend\resources\arduino-cli.exe` を
  `--config-file %USERPROFILE%\.arduinoIDE\arduino-cli.yaml` 付きで使い、ビルドが通ることを確かめる。出力は一時フォルダへ。
- 書き込みは持ち主が IDE で行う。モータにつながる GPIO19・GPIO23 を動かすのは F・G のプログラムだけ。
