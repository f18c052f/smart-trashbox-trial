# docs — 旧リポジトリからの引き継ぎ

投げられたゴミの落下地点を空中で予測し、床を走るゴミ箱が移動して受け止めるシステムを作る。
元動画: [勝手に入るゴミ箱作った Smart Trashbox](https://www.youtube.com/watch?v=NqDTE6dHpJw)

このディレクトリは、旧リポジトリ `smart-trashbox-tribute`から
**引き継ぐ価値のある事実と判断だけ**を抜き出したものである。

## このリポジトリの進め方

1. **まずトライアルをやる。** 回路作成・ゴミ箱作成・プロトタイプ作成・シミュレーションを、
   捨てる前提の試作で一通り行い、実物を触って分かることを先に分かっておく。
2. **本番用のソースはその後に書く。** 仕様書を先に固めることはしない。

計画のたたきは [trial-plan.md](./trial-plan.md) にある。

## 引き継ぎの前提

- **旧リポジトリは廃止する。** このディレクトリは旧リポジトリを参照しなくても読めるように書いてある。
- **旧リポジトリで取った実測値（RealSense の記録、fps など）は持ち込まない。**
  ここにある数値は、メーカー資料・商品仕様・データシートから取ったものと、設計上の想定値だけである。
  想定値にはそう書いてある。必要な値はトライアルで測り直す。
- **例外として、部品の採寸値は参考値として残した**（[reference-measurements.md](./reference-measurements.md)）。
  合否条件や確定値にはせず、トライアルで測り直して食い違ったら測り直した値を正とする。
- 旧コードは持ち込まない。何があったかは [legacy-code.md](./legacy-code.md) に概要だけ残した。

## 目次

| ファイル | 内容 |
|---|---|
| [overview.md](./overview.md) | 何を作るか。システム構成、難しいところ（時間予算）、成功条件、やらないこと |
| [hardware.md](./hardware.md) | 手元にある部品と機材。型番と公表仕様、使うときの注意 |
| [reference-measurements.md](./reference-measurements.md) | 旧リポジトリで部品を採寸した値（参考値） |
| [decisions.md](./decisions.md) | 旧リポジトリで決めたことのうち、引き継ぐもの。理由と、採らなかった案 |
| [circuit.md](./circuit.md) | 移動体の回路（電源・ドライバ・エンコーダ・接地・ピン割当）のテキスト版 |
| [circuit/schematic-draft.html](./circuit/schematic-draft.html) | 回路図のたたき台（未完成）。ブラウザで開いて見る |
| [circuit-bringup.md](./circuit-bringup.md) | 回路に初めて電気を流すときの段階的な手順（E-0〜E-7） |
| [dev-environment.md](./dev-environment.md) | 開発環境の構成と、環境構築でつまずいた点 |
| [legacy-code.md](./legacy-code.md) | 旧リポジトリにあったコードの概要と、トライアルで使えそうな考え方 |
| [trial-plan.md](./trial-plan.md) | トライアル計画のたたき（回路・ゴミ箱・プロトタイプ・シミュレーション） |

## 回路図のたたき台について

[circuit/schematic-draft.html](./circuit/schematic-draft.html) は旧リポジトリで作った作図の下書きで、
**完成していない**（清書版は Cirkit Designer で作図していたが、途中で止まっている）。

- 中身: 電源系統、モータ1個ぶんの結線、ドライバの全端子、電流の守り、電源投入の順序、
  エンコーダのプルアップ予備位置、ESP32 の信号線14本、電圧監視、接地、部品一覧、コンデンサの配置。
- claude.ai のアーティファクトの本体をそのまま保存したものなので、`<!doctype html>`〜`<body>` の
  外枠が無い（再公開するときに外枠が二重にならないよう、わざと付けていない）。
  ブラウザで直接開いても表示できる。文字化けしたときは表示エンコーディングを UTF-8 にする。
- 公開版: https://claude.ai/code/artifact/e98e597f-e794-46dd-a881-f42f51099272
- 図の中の `kShippedPinPlan` は旧ファームウェアにあったピン割当表の名前である。
  中身は [circuit.md](./circuit.md#5-esp32-のピン割当) に書き写してある。
