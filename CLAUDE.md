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

- プロジェクト: `trials/01-circuit/kicad/mobile-base/mobile-base.kicad_pro`
  （回路図 `mobile-base.kicad_sch`）
- **回路図だけを作る。PCB は作らない**（ユニバーサル基板で組む）。KiCad が作った空の
  `mobile-base.kicad_pcb` はあるが使わない。フットプリントの割り当てや PCB 関係のツールは使わない。
- KiCad 10。操作は kicad プラグインの MCP ツールで行い、ツールには上のファイルの絶対パス
  （`C:\Users\user\repos\smart-trashbox-trial\trials\01-circuit\kicad\mobile-base\...`）を渡す。
- ERC のレポートなど書き出したものの置き場は、残す必要があるときだけ決める。試しに流すだけなら一時フォルダに出す。
