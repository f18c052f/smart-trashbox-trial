# 開発環境

旧リポジトリで作った環境と、構築でつまずいた点。新しいリポジトリで同じ道を通らないための記録。

## 1. 全体

| 環境 | 役割 | RealSense をつなぐか |
|---|---|---|
| Windows 11 + WSL2（Ubuntu） | 主な開発。コードの編集は Windows 側、Python の実行は WSL 側 | つながない |
| Raspberry Pi 4 Model B | 固定側の実機。カメラをつないで動かす・記録する | つなぐ |
| ESP32 DevKit | 移動体。Windows の VSCode（PlatformIO）からビルド・書き込み | — |

流れ: WSL で開発・単体テスト・記録データの再生 → git 経由で Pi へ持ち込み、実機で試す → Pi で記録したデータを WSL へ持ち帰って解析する。

## 2. Python（開発PC）

- **Python は WSL の Ubuntu にしか入っていない。** Windows 側で `python` を叩くと Microsoft Store の対話スタブが起動し、
  "Python" と出て何もしない。失敗ではなく無反応なので、実行したつもりになりやすい。
- 旧リポジトリは uv で管理し、Python 3.11 を使った。WSL 越しの呼び方:

  ```
  wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/c/Users/user/repos/<repo> && uv run pytest -q'
  ```

- 複数行のスクリプトを heredoc で `wsl.exe ... bash -lc '...'` に渡すと、引用符が二重になってよく壊れる。
  ファイルに書いてからパスを渡す。

### git worktree を使う場合

リポジトリを Windows 側に置き、Windows と WSL の両方から git を使うと、worktree の `.git` に書かれる絶対パスが
**作った側でしか通じない**（もう片方では `fatal: not a git repository`）。

- 両方から使うなら、worktree 側の `.git` と、本体側の `.git/worktrees/<名前>/gitdir` の**両方**を相対パスに書き換える。
  片方だけ相対にすると、WSL 側でブランチを `main` と誤認することがある。
- この構成では `git worktree list` が `prunable` と表示するが誤検出。**`git worktree prune` を実行すると消える。**
- 消すときは `rm -rf <worktree>` してから `git worktree prune`。

## 3. Raspberry Pi 4

| 項目 | 内容 |
|---|---|
| 機種 | Raspberry Pi 4 Model B Rev 1.2、RAM 4GB |
| OS | Raspberry Pi OS 64-bit **Desktop 版**（Debian 13 "trixie"）。Python 3.13 |
| ネットワーク | Wi-Fi（有線 LAN は使えない環境） |
| 操作 | 開発PC から SSH（公開鍵認証） |
| RealSense SDK | librealsense **v2.58.3** をソースからビルドし、Python バインディングも生成済み（`/usr/local` に入っている） |
| 旧リポジトリ | Pi の `~/repos/smart-trashbox-tribute` に clone がある。新しいリポジトリを置くときに消してよい |

**Desktop 版にした理由**: `realsense-viewer`（GUI）でカメラが生きているかを目で見られるようにするため。
viewer は SDK と同じビルドで一緒にできる。SSH 越しには表示できないので、使うときは Pi のデスクトップで開く。
性能を測るときは `sudo raspi-config` → System Options → Boot / Auto Login → Console にして、デスクトップを常駐させない。

### pyrealsense2 を venv から使う

Raspberry Pi OS ではシステムへの `pip install` が拒否される（PEP 668）ので venv を使う。
ところが `make install` が `.so` を置く場所と venv の探索パスが違うので、そのままでは `import pyrealsense2` が失敗する。
旧リポジトリでは `.so` を venv へコピーした。

```bash
cp -rL /usr/local/lib/python3.13/dist-packages/pyrealsense2 \
       <venv>/lib/python3.13/site-packages/
chown -R <user>:<user> <venv>/lib/python3.13/site-packages/pyrealsense2
```

別案は `--system-site-packages` 付きで venv を作ること。**librealsense をビルドし直したらコピーもやり直す。**

### RealSense の確認コマンド（Pi 上）

| 確認 | コマンド | 見るところ |
|---|---|---|
| 認識しているか | `lsusb \| grep -i realsense` | `Intel Corp. RealSense D435` が出る |
| USB3 でつながっているか | `lsusb -t` | Class=Video の行が **5000M**。480M なら USB2 なので青いポートへ挿し直す（USB2 で測った fps は使えない） |
| シリアル番号など | `dmesg \| grep -i realsense` | |
| 給電が足りているか | `vcgencmd get_throttled` | `throttled=0x0` なら正常。0 以外は電圧低下。**電圧低下は「Pi 4 の性能不足」と誤診されやすい**ので、fps を測る前後に必ず見る |
| 切断が起きていないか | `dmesg \| grep -iE 'disconnect\|over-current\|under-voltage'` | RealSense のバス番号で切断が繰り返し出たら給電を疑う |
| SDK から見えるか | `rs-enumerate-devices -s` | |

シャットダウン: `ssh <user>@<pi> 'sudo shutdown -h now'`

その他:

- microSD への書き込み速度は測るたびに 15% ほどばらついた。判定に使うなら複数回測って幅を見る。
- 60fps の Depth を連続で書き出すと、microSD の書き込み速度が追いつかない見込み。

## 4. ESP32 ファームウェア（PlatformIO）

| 項目 | 旧リポジトリでの値 |
|---|---|
| フレームワーク | ESP-IDF（`framework = espidf`）。ESP-IDF 5.5.5 |
| platform | pioarduino の platform-espressif32 **55.03.311** に固定（`https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip`） |
| board | `esp32dev` |
| ツールチェーン | toolchain-xtensa-esp-elf 14.2.0+20260121 |
| テレオペ | Bluepad32 **4.2.0**（タグで固定）＋ BTstack |
| ビルドの分け方 | `[env:bench]`（可変抵抗で開ループ確認）、`[env:teleop]`（DualSense）、`[env:production]`（無線なし）、`[env:native]`（ホストでの単体テスト） |

2026-09-21 時点で `[env:bench]` と `[env:teleop]` のビルドは通った。書き込んでの動作確認はしていない。

### 構築手順とつまずいた点

**初回**: `~/.platformio` を消してから拡張を入れ、`pio run -e bench`。ツールチェーン約1GB の取得が走り、20分ほどかかる。

1. **VSCode 拡張は `pioarduino.pioarduino-ide` を使う。** 素の「PlatformIO IDE」（`platformio.platformio-ide`）は使わない。
   - 素の拡張は PlatformIO Core 6.2.0 を入れる。これが要求する `tool-scons` 4.41101.0 と、platform 55.03.311 が要求する
     4.40801.0 が同じディレクトリを奪い合い、**リンクの段だけ** `ModuleNotFoundError: No module named 'SCons.Tool.FortranCommon'` で落ちる。
     コンパイルは全部通るので「あと少し」に見えるが、原因は版の食い違い。
   - 入れ替え:

     ```
     code --uninstall-extension platformio.platformio-ide
     code --uninstall-extension ms-vscode.cpptools-extension-pack
     code --install-extension pioarduino.pioarduino-ide
     code --install-extension Jason2866.esp-decoder
     code --install-extension ms-vscode.cpptools
     ```

     cpptools の「拡張パック」を消すと本体も消えるので、本体だけ入れ直す。
   - 落ち着いた状態: PlatformIO Core が `pioarduino` 6.1.19、`packages/tool-scons` が 4.40801.0 のまま残っている。
     **ビルドのたびに `tool-scons` が2種類インストールされていたら、また版が食い違っている。**
   - 応急処置は `<PlatformIO Core Dir>/penv/Scripts/python.exe -m pip install --no-cache-dir "pioarduino==6.1.19"`。
     拡張が Core を入れ直すと元に戻る。
   - 外れだった仮説: 「素の拡張と pioarduino の同居が原因」「`.pio` に古い状態が残っている」。どちらも関係なかった。
2. **ツールチェーンの導入が途中で失敗すると、中身の無い殻と導入済みの印（`.piopm`）だけが残り、二度と取り直さない。**
   `~/.espressif` ができていなければ導入は終わっていない。`~/.platformio` を消してやり直すのが早い。
3. **PlatformIO の拡張は、ワークスペースのルートに `platformio.ini` がある前提で動く。**
   ファームを `firmware/` のようなサブディレクトリに置くなら、そこをルートにして別ウィンドウで開く。
4. **Windows（`core.autocrlf=true`）で Bluepad32 / BTstack を取得すると2か所で止まる。**
   - `error: corrupt patch at line 20` → clone 時にパッチが CRLF になる。`git clone -c core.autocrlf=false -c core.eol=lf` で取る。
   - `PermissionError: [WinError 5]` → git のパックファイルが読み取り専用で消せない。読み取り専用を外して消し直す。
5. **Bluepad32 4.2.0 は ESP-IDF 5.x でそのままコンパイルが通らない。** `cmd_system.c` に `#include <driver/gpio.h>` が足りない
   （上流の main ではコミット e9b755f で直っているが、4.2.0 より新しいタグがまだ無い）。取得後に1行足す。
6. **テレオペと本番のビルドが分かれているかは、ELF のシンボルを `nm` で見て確かめる。** `firmware.map` を grep すると、
   捨てられたセクションや探しただけのライブラリまで拾ってしまい判定できない。

### 本番ビルドから無線を外す

- `sdkconfig` に `CONFIG_BT_ENABLED=n` を書くと Bluetooth は外れる。
- ところが classic ESP32 では、`sdkconfig` で Wi-Fi を無効にできない（`CONFIG_ESP_WIFI_ENABLED` は設定項目として出てこない）。
  `EXCLUDE_COMPONENTS` も、依存をたどって無線のコンポーネントを引き戻してしまうので効かない。
- 効いたのは、ルートの `CMakeLists.txt` で **`COMPONENTS` に使うコンポーネントだけを列挙する**方法だった。

### Flash サイズの警告（未対応）

```
Warning! Flash memory size mismatch detected. Expected 4MB, found 2MB!
```

`board = esp32dev` は 4MB を想定しているが、`sdkconfig.defaults` で `CONFIG_ESPTOOLPY_FLASHSIZE_*` を設定していないため ESP-IDF の既定（2MB）になる。
`partitions_singleapp_large.csv` は 4MB 前提なので、書き込み時に問題になるかもしれない。
新しく作るときは Flash サイズを `sdkconfig.defaults` に明記する。

## 5. BTstack のライセンス

- DualSense をつなぐのに使う **Bluepad32 は Apache-2.0** だが、依存している **BTstack はオープンソースではない**。
  個人の非商用利用に限られ、商用利用や金銭的な利益を得る用途には BlueKitchen 社の有償ライセンスが要る。
- 旧リポジトリでは BTstack のソースをコミットせず、ビルドのときにスクリプトで取得してパッチを当てた（git の履歴に入れないため）。
  テレオペ用のビルドだけがこれを使い、本番用のビルドは BTstack を一切参照しない。
- **リポジトリを公開して LICENSE を置くなら、テレオペ用ビルドの成果物はその対象に含めない**と明記する。

## 6. 3Dプリンタと CAD

- プリンタ: Bambu Lab A1 mini（180 × 180 × 180mm、囲いなし、ベッド上限 80℃）。ASA / ABS / PC / 繊維入りは公式に非推奨なので、使える上限は PETG。
  180mm を超える部品は分割する。スライサは Bambu Studio。
- CAD は旧リポジトリで build123d（Python、OCCT カーネル）を形状の正にし、STEP 経由で FreeCAD を確認・測定に使う形を採った。
  FreeCAD 単独を避けた理由は [decisions.md](./decisions.md#7-採らなかった案) の最後の行。
  トライアルでは、まず手で描いて刷ってみる、でかまわない。
