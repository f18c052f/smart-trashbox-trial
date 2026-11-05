# smart-trashbox-trial

投げられたゴミの落下地点を予測し、床を走るゴミ箱が受け止めるシステムのトライアル用リポジトリ。
入口は [docs/README.md](docs/README.md)、計画は [docs/trial-plan.md](docs/trial-plan.md)。

## ドキュメントの置き方

- `docs/inherited/` は旧リポジトリ `smart-trashbox-tribute` から引き継いだ記録。**書き換えも追加もしない。**
- 新しく分かったことは `docs/` の直下に書き、置き換えた `inherited/` の文書へリンクする。
- トライアルの記録は `trials/<番号>-<名前>/` に置く（例: `trials/01-circuit/notes.md`）。

## KiCad

- プロジェクト: `trials/01-circuit/kicad/mobile-base/mobile-base.kicad_pro`
  （回路図 `mobile-base.kicad_sch`）
- **回路図だけを作る。PCB は作らない**（ユニバーサル基板で組む）。KiCad が作った空の
  `mobile-base.kicad_pcb` はあるが使わない。フットプリントの割り当てや PCB 関係のツールは使わない。
- KiCad 10。操作は kicad プラグインの MCP ツールで行い、ツールには上のファイルの絶対パス
  （`C:\Users\user\repos\smart-trashbox-trial\trials\01-circuit\kicad\mobile-base\...`）を渡す。
- ERC のレポートなど書き出したものの置き場は、残す必要があるときだけ決める。試しに流すだけなら一時フォルダに出す。
