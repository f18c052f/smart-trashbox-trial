// 段階3（mobile-base）を 9×15cm のユニバーサル基板1枚に載せる配置図と配線表を作り、
// 回路図のネットリスト（nets.json）と照らし合わせて検査する。
//
// 使い方: node layout.mjs
//   → layout-top.svg（部品面から見た図）、layout-back.svg（はんだ面から見た図。左右が反対）、
//     wires.md（配線表）を書き、検査の結果を表示する。誤りが1つでもあれば終了コード 1。
//
// 座標は穴の番号。左上の穴を (0,0) とし、x は右へ、y は下へ数える。
// 図と表では、人が数えやすいように 1 から数えた「列・行」で書く（列 = x+1、行 = y+1）。
//
// nets.json は KiCad の mobile-base.kicad_sch から書き出したネットリストを、ネット名 → ["部品番号-ピン", ...] にしたもの。
// 回路図を直したら、ネットリストを書き出し直して nets.json を作り直し、このスクリプトを流し直す。

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));

// node layout.mjs --import <KiCad のネットリスト（kicadsexpr 形式の .net）> で nets.json を作り直す
const imp = process.argv.indexOf('--import');
if (imp >= 0) {
  const src = fs.readFileSync(process.argv[imp + 1], 'utf8');
  const blocks = src.slice(src.indexOf('(nets')).split(/\n\t\t\(net\b/).slice(1);
  const lines = blocks.map((b) => {
    const name = b.match(/\(name "([^"]*)"\)/)[1].replace(/^\//, '');
    const nodes = [...b.matchAll(/\(ref "([^"]+)"\)\s+\(pin "([^"]+)"\)/g)].map((x) => `${x[1]}-${x[2]}`);
    return `  ${JSON.stringify(name)}: ${JSON.stringify(nodes)}`;
  });
  fs.writeFileSync(path.join(HERE, 'nets.json'), `{\n${lines.join(',\n')}\n}\n`);
  console.log(`nets.json を作り直した（${lines.length} 本のネット）`);
}
const NETS = JSON.parse(fs.readFileSync(path.join(HERE, 'nets.json'), 'utf8'));

// ---------------------------------------------------------------- 基板
// 9×15cm の板は売り手によって穴の数が 30×48〜34×53 と違う。少ないほう（30 行 × 48 列）に収める。
// 穴の多い板では、余りを右と下に残す（左上の穴から数える）。
const BOARD = { cols: 48, rows: 30, w: 150, h: 90 };
const P = 2.54;
const MX = (BOARD.w - (BOARD.cols - 1) * P) / 2;
const MY = (BOARD.h - (BOARD.rows - 1) * P) / 2;

// 基板の外に付くもの（回路図にはあるが、この基板には穴を取らない）
const OFFBOARD = new Set(['J1', 'F1', 'SW1', 'M0', 'M1', 'M2', 'C3', 'C4', 'C5']);
// 部品の中でつながっているピン（ESP32 ボードの GND は基板の上で1本につながっている）
const INTERNAL = [['U5-14', 'U5-20', 'U5-26']];

// ---------------------------------------------------------------- データ
const parts = [];
const back = [];
const top = [];

const Rc = (x0, y0, x1, y1) => ({ rect: [x0, y0, x1, y1] });
const Ci = (cx, cy, r) => ({ circle: [cx, cy, r] });

// z: low = 低い部品（上を線が通ってよい）、high = 背の高い部品、raised = ソケットで浮いた ESP32、off = 基板の外
function part(ref, pins, o = {}) {
  parts.push({
    ref, pins, body: o.body || [], z: o.z || 'low', kind: o.kind || '', label: o.label ?? ref,
    net: o.net || null, shadow: o.shadow || null, note: o.note || '', route: o.route || null,
  });
}
// 裏（はんだ面）の裸のすずめっき線。通る穴すべてにはんだ付けする。cls: bus / out / stub
function bw(cls, ...pts) { back.push({ cls, pts }); }
// 表（部品面）の被覆つきの線。両端だけが電気的な点。cls: P（2.3A まで）/ L（0.8A まで）/ S（信号）
function tw(cls, from, to, note, via = []) { top.push({ id: '', cls, from, to, via, note }); }

// ---- 電源の入口・母線・GND-STAR（左上） ------------------------------------
bw('bus', [1, 9], [45, 9]);    // PGND の母線（行 10）
bw('bus', [1, 11], [45, 11]);  // VM の母線（行 12）
part('J9', { 1: [1, 11], 2: [1, 9] }, { body: [Rc(-0.9, 8, 2.1, 12)], z: 'high', kind: 'term', note: '電池からの線（SW1 の後ろ）を受ける。10A 以上' });
part('C1', { 1: [6, 11], 2: [6, 9] }, { body: [Ci(6, 10, 2.46)], z: 'high', kind: 'ecap', note: '− 足が GND-STAR' });
bw('bus', [6, 9], [6, 5]);     // GND-STAR の島（C1 の − 足から北へ）
bw('stub', [6, 6], [5, 6]);    // NT1 の口
bw('stub', [6, 7], [5, 7]);    // J10 の GND の線の口
part('TPG', { 1: [6, 5] }, { kind: 'tp', label: 'TP-G', net: 'PGND', note: 'GND-STAR を測る点（C1 の − 足と同じ）。回路図には描かない' });
part('D3', { 1: [10, 13], 2: [10, 9] }, { body: [Rc(9.3, 9.6, 10.7, 12.4)], kind: 'diode', note: '帯（カソード）を下（VM の側）に' });
bw('stub', [10, 11], [10, 13]);
part('TP1', { 1: [6, 13] }, { kind: 'tp' });
bw('stub', [6, 11], [6, 13]);
part('F2', { 1: [12, 12], 2: [12, 3] }, { z: 'off', kind: 'fuse', note: '線付きのホルダ。ホルダは基板の外に出し、縁に結束バンドで留める' });
bw('stub', [12, 11], [12, 12]);
part('R1', { 1: [13, 13], 2: [13, 16] }, { body: [Rc(12.6, 13.7, 13.4, 15.3)], kind: 'res' });
bw('stub', [13, 11], [13, 13]);
part('TP4', { 1: [13, 17] }, { kind: 'tp' });
bw('stub', [13, 16], [13, 17]);
bw('stub', [13, 16], [14, 16]); // VBAT_SENSE の口

// ---- 5V 電源（U4） -------------------------------------------------------
part('U4', { 1: [10, 3], 2: [9, 3], 3: [8, 3] }, { body: [Rc(6.75, 2.1, 11.25, 3.9)], z: 'high', kind: 'reg', note: '1 番（VIN）を右に' });
part('C13', { 1: [11, 5], 2: [9, 5] }, { body: [Rc(9.3, 4.6, 10.7, 5.4)], kind: 'cap' });
bw('stub', [10, 3], [12, 3]);  // VIN → F2 の 2
bw('stub', [11, 3], [11, 5]);  // VIN → C13
bw('stub', [9, 3], [9, 6], [6, 6]); // GND → C13 → GND-STAR（専用の線）
part('TP2', { 1: [8, 1] }, { kind: 'tp' });
bw('stub', [8, 3], [8, 1]);
bw('stub', [8, 2], [5, 2]);    // +5V の口（3本の線を出す）

// ---- ドライバ3台・モータの口 ----------------------------------------------
const DRV = [
  { u: 'U1', x: 14, j: 'J2', c7: 'C7', c10: 'C10' },
  { u: 'U2', x: 24, j: 'J5', c7: 'C8', c10: 'C11' },
  { u: 'U3', x: 34, j: 'J7', c7: 'C9', c10: 'C12' },
];
for (const d of DRV) {
  const x = d.x;
  const pins = {};
  for (let k = 1; k <= 8; k++) pins[k] = [x + k - 1, 6];
  part(d.u, pins, { body: [Rc(x - 0.5, 5.55, x + 7.5, 6.45)], kind: 'drv', shadow: Rc(x - 0.63, 4.4, x + 7.64, 7.6), note: '丸ピンのソケット（1×8）に立てて挿す' });
  part(d.j, { 1: [x + 5, 2], 2: [x + 7, 2] }, { body: [Rc(x + 4, 0.5, x + 8, 3.5)], z: 'high', kind: 'term', note: '1 = 赤、2 = 白' });
  part(d.c7, { 1: [x + 5, 11], 2: [x + 5, 9] }, { body: [Rc(x + 4.55, 9.4, x + 5.45, 10.6)], kind: 'cap' });
  part(d.c10, { 1: [x + 3, 3], 2: [x + 1, 3] }, { body: [Rc(x + 1.4, 2.55, x + 2.6, 3.45)], kind: 'cap' });
  bw('out', [x + 5, 6], [x + 5, 2]);              // OUT1 → 端子台 1（赤）
  bw('out', [x + 7, 6], [x + 7, 2]);              // OUT2 → 端子台 2（白）
  bw('out', [x, 6], [x - 1, 6], [x - 1, 9]);      // GND → PGND 母線
  bw('out', [x + 6, 6], [x + 6, 9]);              // RS → PGND 母線
  bw('out', [x + 4, 6], [x + 4, 7]);              // VM の口
  bw('stub', [x, 6], [x, 3], [x + 1, 3]);         // GND → C10 の −
  bw('stub', [x + 3, 6], [x + 3, 3]);             // VREF → C10 の +（行 5・6 が +3V3 の線の口）
  bw('stub', [x + 1, 6], [x + 1, 7]);             // IN2 の口
  bw('stub', [x + 2, 6], [x + 2, 7]);             // IN1 の口
  tw('P', [x + 4, 7], [x + 4, 11], `${d.u} の VM → VM の母線`);
}
part('TP7', { 1: [42, 8] }, { kind: 'tp' });
bw('stub', [42, 9], [42, 8]);
part('TP6', { 1: [42, 12] }, { kind: 'tp' });
bw('stub', [42, 11], [42, 12]);
part('C2', { 1: [45, 11], 2: [45, 9] }, { body: [Ci(45, 10, 2.46)], z: 'high', kind: 'ecap' });

// ---- LED の口（左の縁） ---------------------------------------------------
part('J10', { 1: [1, 15], 2: [1, 17], 3: [1, 19] }, { body: [Rc(-0.9, 14, 2.1, 20)], z: 'high', kind: 'term', note: '上から 5V・DIN・GND' });
bw('stub', [1, 15], [7, 15]);  // +5V
bw('stub', [1, 19], [7, 19]);  // PGND（専用の線で GND-STAR へ）
part('C15', { 1: [7, 16], 2: [7, 18] }, { body: [Ci(7, 17, 1.24)], z: 'high', kind: 'ecap', note: '帯（−）を下に' });
bw('stub', [7, 15], [7, 16]);
bw('stub', [7, 19], [7, 18]);
part('R16', { 1: [4, 17], 2: [2, 17] }, { body: [Rc(2.4, 16.65, 3.6, 17.35)], kind: 'res' });
bw('stub', [1, 17], [2, 17]);  // J10-2 → R16
bw('stub', [4, 17], [5, 17]);  // LED_BUF の口

// ---- レベル変換 U6（ESP32 の西） ----------------------------------------------
{
  const ux = 16, uy = 14; // 切り欠きを左に。下の列が 1〜7、上の列が 14〜8
  const pins = {};
  for (let i = 0; i < 7; i++) { pins[1 + i] = [ux + i, uy + 3]; pins[14 - i] = [ux + i, uy]; }
  part('U6', pins, { body: [Rc(ux - 0.6, uy - 0.6, ux + 6.6, uy + 3.6)], kind: 'dip', note: '14 ピンの IC ソケット。切り欠きを左に' });
}
// U6 の SGND を囲む線（北・東・南）
for (const x of [17, 18, 20, 21]) bw('stub', [x, 14], [x, 13]);
bw('stub', [17, 13], [23, 13]);
bw('stub', [23, 13], [23, 21]);
for (const x of [19, 20, 22]) bw('stub', [x, 17], [x, 19]);
bw('stub', [15, 19], [23, 19]);
bw('stub', [16, 17], [15, 17], [15, 19]);           // 1 番（1OE）→ SGND
part('C14', { 1: [15, 14], 2: [15, 16] }, { body: [Rc(14.6, 14.4, 15.4, 15.6)], kind: 'cap' });
bw('stub', [16, 14], [15, 14], [14, 14]);           // 14 番（VCC）→ C14、+5V の口
bw('stub', [15, 16], [15, 17]);
part('R17', { 1: [17, 18], 2: [17, 19] }, { body: [Rc(16.6, 17.6, 17.4, 19.4)], kind: 'res', note: '立てて付ける' });
bw('stub', [17, 17], [17, 18]);                      // 2 番（1A）→ R17
bw('stub', [17, 18], [16, 18]);                      // LED_GPIO の口
bw('stub', [18, 17], [18, 18]);                      // 3 番（1Y）→ LED_BUF の口

// ---- SGND の線（行 22）と、ESP32 の GND への道 ---------------------------------------
bw('stub', [3, 21], [23, 21]);
bw('stub', [23, 17], [28, 17]);                      // ESP32 の GND（北の列の 1 番目）へ
bw('stub', [24, 17], [24, 16]);                      // NT1 の口
part('TPS', { 1: [26, 16] }, { kind: 'tp', label: 'TP-S', net: 'SGND', note: 'SGND を測る点。回路図には描かない' });
bw('stub', [26, 17], [26, 16]);
part('NT1', { 1: [5, 6], 2: [24, 16] }, {
  kind: 'nt', note: 'GND-STAR と SGND をつなぐ1本だけの線（被覆つき AWG22）。SGND の側の端は、procedure.md の段 0 で PGND と SGND が鳴らないことを確かめてから付ける',
  route: [[5, 5.55], [12.2, 5.55], [12.2, 11.8], [23.6, 11.8], [23.6, 15.6]],
});

// ---- 可変抵抗 VR1 --------------------------------------------------------------
part('VR1', { 1: [11, 18], 2: [11, 19], 3: [11, 20] }, { body: [Rc(9.15, 17.15, 12.85, 20.85)], z: 'high', kind: 'pot', note: '手元の可変抵抗。足が合わなければ3本の線で基板の外に出す' });
bw('stub', [11, 18], [11, 16]);                      // +3V3 の口（行 17・18）
bw('stub', [11, 19], [13, 19]);                      // BENCH_POT の口
bw('stub', [11, 20], [11, 21]);                      // SGND

// ---- エンコーダの口（下の縁）と保護抵抗 ---------------------------------------------
const ENC = [
  { j: 'J3', x: 1, ra: 'R10', rb: 'R11', tp: 'TP5' },
  { j: 'J6', x: 9, ra: 'R12', rb: 'R13', tp: 'TP8' },
  { j: 'J8', x: 17, ra: 'R14', rb: 'R15', tp: 'TP9' },
];
for (const e of ENC) {
  const x = e.x;
  part(e.j, { 1: [x, 28], 2: [x + 2, 28], 3: [x + 4, 28], 4: [x + 6, 28] }, { body: [Rc(x - 1, 26.5, x + 7, 29.5)], z: 'high', kind: 'term', note: '左から 青・黒・黄・緑' });
  bw('stub', [x, 28], [x, 24]);                      // 青（+3V3）→ TP（行 26・27 が線の口）
  part(e.tp, { 1: [x, 24] }, { kind: 'tp' });
  bw('stub', [x + 2, 28], [x + 2, 21]);              // 黒 → SGND の線
  for (const [ref, dx] of [[e.ra, 4], [e.rb, 6]]) {
    part(ref, { 1: [x + dx, 25], 2: [x + dx, 24] }, { body: [Rc(x + dx - 0.4, 23.6, x + dx + 0.4, 25.4)], kind: 'res', note: '立てて付ける' });
    bw('stub', [x + dx, 28], [x + dx, 25]);
    bw('stub', [x + dx, 24], [x + dx, 23]);          // ESP32 へ行く線の口
  }
}

// ---- ESP32（右下。アンテナを左、USB を右に） ------------------------------------------
{
  const pins = {};
  for (let k = 1; k <= 19; k++) { pins[k] = [27 + k, 27]; pins[19 + k] = [27 + k, 17]; }
  part('U5', pins, { body: [Rc(25.6, 16.49, 47.8, 27.51)], z: 'raised', kind: 'esp32', note: 'ピンソケット 1×19 を2列。アンテナを左、USB を右に' });
}
for (const x of [29, 30, 33, 35]) bw('stub', [x, 17], [x, 16]); // 北の列の外側の口（IN）
for (const x of [36, 38, 39, 40]) bw('stub', [x, 17], [x, 18]); // 北の列の内側の口（エンコーダ）
for (const x of [35, 36, 37, 38, 39, 42]) bw('stub', [x, 27], [x, 26]); // 南の列の内側の口
// 電池電圧（C6・R2 は ESP32 の下。GPIO32 と GND の間）
part('C6', { 1: [34, 25], 2: [34, 23] }, { body: [Rc(33.55, 23.4, 34.45, 24.6)], kind: 'cap', note: 'ESP32 の下' });
bw('stub', [34, 17], [34, 23]);                      // GND（北の列の 7 番目）→ C6
// 北の列の GND 2本（1 番目と 7 番目）を基板の上でもつなぐ。ESP32 を抜いても R2・C6 の GND が SGND に残るように
bw('stub', [34, 19], [28, 19], [28, 17]);
bw('stub', [34, 27], [34, 25]);                      // GPIO32 → C6
part('R2', { 1: [32, 25], 2: [32, 22] }, { body: [Rc(31.6, 22.3, 32.4, 24.7)], kind: 'res', note: 'ESP32 の下' });
bw('stub', [34, 25], [32, 25]);
bw('stub', [32, 22], [34, 22]);
bw('stub', [32, 25], [31, 25]);                      // VBAT_SENSE の口
// 3V3
bw('stub', [28, 27], [28, 28], [25, 28]);
part('TP3', { 1: [25, 29] }, { kind: 'tp' });
bw('stub', [25, 28], [25, 29]);
bw('stub', [26, 28], [26, 29]);
bw('stub', [27, 28], [27, 29]);
// 5V と JP1（USB の横）
part('JP1', { 1: [45, 29], 2: [46, 29] }, { body: [Rc(44.5, 28.55, 46.5, 29.45)], kind: 'hdr', note: '2ピンのヘッダとジャンパピン。USB を挿す前に外す' });
bw('stub', [46, 27], [46, 29]);
bw('stub', [45, 29], [44, 29]);

// ---------------------------------------------------------------- 表の線
// 道のり（via）は図に描くためのもの。電気的には両端だけが効く。
// 束ねる所: ① C1 と J9 のすきま（W04・W05）、② U1 の西（W06・W07・W12）、③ 行 13 の横の束（IN と 5V）、
//            ④ ESP32 のアンテナの端の下（列 25〜26）から ESP32 の下へ入る束。
// 5V・GND（L）
tw('L', [5, 2], [7, 15], 'U4 の 5V → LED の口（W05 とより合わせる）', [[4.4, 2.6], [2.75, 3.4], [2.75, 13.0], [6.4, 14.1]]);
tw('L', [5, 7], [7, 19], 'GND-STAR → LED の口の GND（W04 とより合わせる）', [[4.3, 7.3], [3.0, 7.8], [3.0, 13.2], [8.45, 14.5], [8.45, 18.9], [7.4, 18.9]]);
tw('L', [6, 2], [14, 14], 'U4 の 5V → U6', [[6, 0.7], [13.0, 0.7], [13.0, 11.95], [14.45, 11.95], [14.45, 13.55]]);
tw('L', [7, 2], [44, 29], 'U4 の 5V → JP1（ESP32 の 5V）', [[7, 0.4], [13.25, 0.4], [13.25, 12.15], [24.75, 12.15], [24.75, 29.65], [44, 29.65]]);
// +3V3（S）
tw('S', [27, 29], [17, 26], '3V3 → J8 の青', [[27, 28.55], [24.55, 28.55], [24.55, 26.0], [17.6, 26.0]]);
tw('S', [17, 25], [9, 26], 'J8 の青 → J6 の青', [[16.4, 25.95], [9.6, 25.95]]);
tw('S', [9, 25], [1, 26], 'J6 の青 → J3 の青', [[8.4, 25.95], [1.6, 25.95]]);
tw('S', [26, 29], [11, 17], '3V3 → VR1', [[25.45, 28.4], [25.45, 21.25], [13.85, 21.25], [13.85, 16.6], [11.6, 16.6]]);
tw('S', [11, 16], [17, 4], 'VR1 の 3V3 → U1 の VREF', [[11, 15.2], [12.45, 13.6], [12.45, 4.4], [16.4, 4.4]]);
tw('S', [17, 5], [27, 4], 'U1 の VREF → U2 の VREF', [[18, 4.6], [26.4, 4.6]]);
tw('S', [27, 5], [37, 4], 'U2 の VREF → U3 の VREF', [[28, 4.6], [36.4, 4.6]]);
// IN（S）: ドライバの下の口 → 行 13 の横の束 → ESP32 の北の列（IN2 の2本は ESP32 の下を通って南の列へ）
tw('S', [15, 7], [29, 16], 'U1 IN2 ← GPIO23', [[15, 8.2], [15.2, 12.3], [29, 12.3]]);
tw('S', [16, 7], [35, 16], 'U1 IN1 ← GPIO19', [[16, 8.2], [16.2, 12.45], [35, 12.45]]);
tw('S', [26, 7], [33, 16], 'U2 IN1 ← GPIO21', [[26, 8.2], [26.2, 12.75], [33, 12.75]]);
tw('S', [25, 7], [36, 26], 'U2 IN2 ← GPIO25（ESP32 の下を通す）', [[25, 8.2], [25.35, 12.6], [25.35, 21.2], [36, 21.2]]);
tw('S', [36, 7], [30, 16], 'U3 IN1 ← GPIO22', [[36, 8.2], [35.8, 12.9], [30, 12.9]]);
tw('S', [35, 7], [37, 26], 'U3 IN2 ← GPIO26（ESP32 の下を通す）', [[35, 8.2], [34.8, 13.05], [25.15, 13.05], [25.15, 21.0], [37, 21.0]]);
// エンコーダ（S）: 保護抵抗の上の口 → 行 22 の束 → ESP32 の下
tw('S', [5, 23], [40, 18], 'ENC0_A_IN → GPIO4', [[5, 21.6], [24.3, 21.6], [25.6, 19.8], [40, 19.8]]);
tw('S', [7, 23], [42, 26], 'ENC0_B_IN → GPIO13', [[7, 21.75], [24.3, 21.75], [25.7, 20.4], [42, 20.4]]);
tw('S', [13, 23], [39, 26], 'ENC1_A_IN → GPIO14', [[13, 21.9], [24.3, 21.9], [25.8, 20.6], [39, 20.6]]);
tw('S', [15, 23], [39, 18], 'ENC1_B_IN → GPIO16', [[15, 22.05], [24.3, 22.05], [25.9, 19.6], [39, 19.6]]);
tw('S', [21, 23], [38, 18], 'ENC2_A_IN → GPIO17', [[21, 22.2], [24.3, 22.2], [26.0, 19.4], [38, 19.4]]);
tw('S', [23, 23], [36, 18], 'ENC2_B_IN → GPIO18', [[23, 22.35], [24.3, 22.35], [26.1, 19.2], [36, 19.2]]);
// そのほか（S）
tw('S', [14, 16], [31, 25], '電池電圧（R1 の下）→ ESP32 の下の R2・C6', [[14.3, 16.4], [14.3, 20.3], [24.2, 20.3], [25.2, 21.6], [31, 21.6]]);
tw('S', [13, 19], [35, 26], 'VR1 のまん中 → GPIO33', [[13.5, 20.0], [24.4, 20.0], [25.4, 21.4], [35, 21.4]]);
tw('S', [16, 18], [38, 26], 'GPIO27 → U6 の 2 番（LED）', [[16, 18.6], [15.7, 19.3], [15.7, 20.15], [24.3, 20.15], [25.3, 20.8], [38, 20.8]]);
tw('S', [18, 18], [5, 17], 'U6 の 3 番 → R16（LED の口）', [[18.4, 18.6], [18.4, 21.05], [8.65, 21.05], [8.65, 19.6], [5.3, 19.6], [5.3, 17.6]]);

top.forEach((w, i) => { w.id = 'W' + String(i + 1).padStart(2, '0'); });

// ---------------------------------------------------------------- ピンの名前（表と図の説明用）
const PIN_NAMES = {
  drv: { 1: 'GND', 2: 'IN2', 3: 'IN1', 4: 'VREF', 5: 'VM', 6: 'OUT1', 7: 'RS', 8: 'OUT2' },
  U4: { 1: 'VIN', 2: 'GND', 3: 'VOUT' },
  U6: { 1: '1OE', 2: '1A', 3: '1Y', 4: '2OE', 5: '2A', 6: '2Y', 7: 'GND', 8: '3Y', 9: '3A', 10: '3OE', 11: '4Y', 12: '4A', 13: '4OE', 14: 'VCC' },
  U5: {
    1: '3V3', 2: 'EN', 3: 'G36', 4: 'G39', 5: 'G34', 6: 'G35', 7: 'G32', 8: 'G33', 9: 'G25', 10: 'G26', 11: 'G27', 12: 'G14', 13: 'G12',
    14: 'GND', 15: 'G13', 16: 'SD2', 17: 'SD3', 18: 'CMD', 19: '5V', 20: 'GND', 21: 'G23', 22: 'G22', 23: 'TXD', 24: 'RXD', 25: 'G21',
    26: 'GND', 27: 'G19', 28: 'G18', 29: 'G5', 30: 'G17', 31: 'G16', 32: 'G4', 33: 'G0', 34: 'G2', 35: 'G15', 36: 'SD1', 37: 'SD0', 38: 'CLK',
  },
};
function pinName(ref, n) {
  if (/^U[123]$/.test(ref)) return PIN_NAMES.drv[n];
  if (PIN_NAMES[ref]) return PIN_NAMES[ref][n];
  return null;
}
const pinLabel = (ref, n) => { const nm = pinName(ref, n); return nm ? `${ref}-${n}（${nm}）` : `${ref}-${n}`; };
const cr = ([x, y]) => `${x + 1},${y + 1}`; // 図と表の「列,行」

// ---------------------------------------------------------------- 検査
const errors = [];
const warns = [];
const notes = [];
const key = (x, y) => `${x},${y}`;
const holes = new Map();
function H(x, y) {
  const k = key(x, y);
  if (!holes.has(k)) holes.set(k, { x, y, pins: [], back: null, ends: [] });
  return holes.get(k);
}

const pinNet = new Map();
for (const [net, nodes] of Object.entries(NETS)) for (const n of nodes) pinNet.set(n, net);
const netOf = (p, n) => (p.net ? p.net : pinNet.get(`${p.ref}-${n}`));

function expand(pts) {
  const out = [];
  for (let i = 0; i < pts.length - 1; i++) {
    const [x0, y0] = pts[i];
    const [x1, y1] = pts[i + 1];
    if (x0 !== x1 && y0 !== y1) errors.push(`裏の線が斜めになっている: ${JSON.stringify(pts)}`);
    const dx = Math.sign(x1 - x0);
    const dy = Math.sign(y1 - y0);
    let x = x0;
    let y = y0;
    if (i === 0) out.push([x, y]);
    while (x !== x1 || y !== y1) { x += dx; y += dy; out.push([x, y]); }
  }
  return out;
}

const inB = ([x, y]) => x >= 0 && y >= 0 && x < BOARD.cols && y < BOARD.rows;
for (const p of parts) {
  for (const [n, xy] of Object.entries(p.pins)) {
    if (!inB(xy)) errors.push(`${p.ref}-${n} が基板の外 (${cr(xy)})`);
    const h = H(...xy);
    h.pins.push({ ref: p.ref, n });
    if (h.pins.length > 1) errors.push(`穴 ${cr(xy)} に部品の足が2本: ${h.pins.map((q) => q.ref + '-' + q.n).join(', ')}`);
  }
}
for (const b of back) {
  b.holes = expand(b.pts);
  for (const xy of b.holes) {
    if (!inB(xy)) errors.push(`裏の線が基板の外 (${cr(xy)})`);
    const h = H(...xy);
    if (!h.back || b.cls === 'bus' || (b.cls === 'out' && h.back === 'stub')) h.back = b.cls;
  }
}
for (const w of top) {
  for (const xy of [w.from, w.to]) {
    if (!inB(xy)) errors.push(`${w.id} の端が基板の外 (${cr(xy)})`);
    const h = H(...xy);
    h.ends.push(w.id);
    if (h.pins.length) errors.push(`${w.id} の端 ${cr(xy)} が部品の足（${h.pins[0].ref}-${h.pins[0].n}）と同じ穴`);
    if (!h.back) errors.push(`${w.id} の端 ${cr(xy)} が裏の線につながっていない`);
    if (h.ends.length > 1) errors.push(`穴 ${cr(xy)} に線の端が2本: ${h.ends.join(', ')}`);
  }
}

// つながり（Union-Find）
const parent = new Map();
for (const k of holes.keys()) parent.set(k, k);
const find = (k) => { while (parent.get(k) !== k) { parent.set(k, parent.get(parent.get(k))); k = parent.get(k); } return k; };
const uni = (a, b) => { a = find(a); b = find(b); if (a !== b) parent.set(a, b); };
for (const b of back) for (let i = 1; i < b.holes.length; i++) uni(key(...b.holes[i - 1]), key(...b.holes[i]));
for (const w of top) uni(key(...w.from), key(...w.to));
const pinHole = new Map();
for (const p of parts) for (const [n, xy] of Object.entries(p.pins)) pinHole.set(`${p.ref}-${n}`, key(...xy));
for (const g of INTERNAL) for (let i = 1; i < g.length; i++) if (pinHole.has(g[0]) && pinHole.has(g[i])) uni(pinHole.get(g[0]), pinHole.get(g[i]));

// 部品の足の一覧と、回路図の足の一覧が同じか
const placed = new Set();
for (const p of parts) for (const n of Object.keys(p.pins)) {
  placed.add(`${p.ref}-${n}`);
  if (!p.net && !pinNet.has(`${p.ref}-${n}`)) errors.push(`${p.ref}-${n} は回路図に無い`);
}
for (const node of pinNet.keys()) {
  const ref = node.split('-')[0];
  if (OFFBOARD.has(ref)) continue;
  if (!placed.has(node)) errors.push(`回路図の ${node} が基板に置かれていない`);
}

// かたまりごとのネット
const compNets = new Map();
const compPins = new Map();
for (const p of parts) for (const [n, xy] of Object.entries(p.pins)) {
  const net = netOf(p, n);
  if (!net) continue;
  const r = find(key(...xy));
  if (!compNets.has(r)) { compNets.set(r, new Set()); compPins.set(r, []); }
  compNets.get(r).add(net);
  compPins.get(r).push(`${p.ref}-${n}`);
}
for (const [r, set] of compNets) if (set.size > 1) errors.push(`短絡: ${[...set].join(' と ')} がつながっている（${compPins.get(r).join(', ')}）`);
const netRoots = new Map();
for (const p of parts) for (const [n, xy] of Object.entries(p.pins)) {
  const net = netOf(p, n);
  if (!net || net.startsWith('unconnected')) continue;
  if (!netRoots.has(net)) netRoots.set(net, new Map());
  const r = find(key(...xy));
  if (!netRoots.get(net).has(r)) netRoots.get(net).set(r, []);
  netRoots.get(net).get(r).push(`${p.ref}-${n}`);
}
for (const [net, roots] of netRoots) {
  if (roots.size > 1) errors.push(`つながっていない: ${net} が ${roots.size} つに分かれている（${[...roots.values()].map((v) => v.join(' ')).join(' / ')}）`);
}
// 部品の中のつながり（ESP32 の GND）に頼らずに、基板の上だけでつながっているか。
// 頼ってよいのは、ほかに何もつながらない ESP32 の GND の足だけ（ESP32 を抜いたときに浮くものを作らない）。
{
  const p2 = new Map();
  for (const k of holes.keys()) p2.set(k, k);
  const f2n = (k) => { while (p2.get(k) !== k) { p2.set(k, p2.get(p2.get(k))); k = p2.get(k); } return k; };
  const u2 = (a, b) => { a = f2n(a); b = f2n(b); if (a !== b) p2.set(a, b); };
  for (const b of back) for (let i = 1; i < b.holes.length; i++) u2(key(...b.holes[i - 1]), key(...b.holes[i]));
  for (const w of top) u2(key(...w.from), key(...w.to));
  const internalPins = new Set(INTERNAL.flat());
  const byNet = new Map();
  for (const p of parts) for (const [n, xy] of Object.entries(p.pins)) {
    const net = netOf(p, n);
    if (!net || net.startsWith('unconnected')) continue;
    if (!byNet.has(net)) byNet.set(net, new Map());
    const r = f2n(key(...xy));
    if (!byNet.get(net).has(r)) byNet.get(net).set(r, []);
    byNet.get(net).get(r).push(`${p.ref}-${n}`);
  }
  for (const [net, roots] of byNet) {
    if (roots.size < 2) continue;
    const groups = [...roots.values()];
    const leaning = groups.filter((g) => !g.every((x) => internalPins.has(x)));
    if (leaning.length > 1) errors.push(`ESP32 のボードの中のつながりに頼っている: ${net}（${groups.map((g) => g.join(' ')).join(' / ')}）。ESP32 を抜くと浮く`);
    else notes.push(`${net}: ${groups.filter((g) => g.every((x) => internalPins.has(x))).map((g) => g.join(' ')).join('、')} は基板の上では何にもつながない（ESP32 のボードの中で GND につながる）`);
  }
}
for (const [k, h] of holes) {
  const r = find(k);
  if ((h.back || h.ends.length) && !compNets.has(r)) warns.push(`どの部品にもつながらない線がある（${cr([h.x, h.y])}）`);
}

// 隣の穴どうしの危ない組み合わせ（はんだが橋になったときに壊れるもの）
const compNet = (k) => { const s = compNets.get(find(k)); return s ? [...s][0] : null; };
const HV = /^(VM|VM_U4|BAT\+|BAT_FUSED|OUT\d[+-])$/;
const GPIO_NETS = new Set(Object.entries(NETS).filter(([, nodes]) => nodes.some((n) => /^U5-(\d+)$/.test(n) && !['U5-1', 'U5-14', 'U5-19', 'U5-20', 'U5-26'].includes(n))).map(([net]) => net));
const conductive = (h) => h && (h.pins.length || h.back || h.ends.length);
const adjacent = [];
for (const [k, h] of holes) {
  if (!conductive(h)) continue;
  for (const [dx, dy] of [[1, 0], [0, 1]]) {
    const k2 = key(h.x + dx, h.y + dy);
    const h2 = holes.get(k2);
    if (!conductive(h2) || find(k) === find(k2)) continue;
    const samePart = h.pins.length && h2.pins.length && h.pins[0].ref === h2.pins[0].ref;
    if (samePart) continue;
    const a = compNet(k) || '（なし）';
    const b = compNet(k2) || '（なし）';
    const where = `${cr([h.x, h.y])} と ${cr([h2.x, h2.y])}`;
    adjacent.push({ a, b, where });
    if (HV.test(a) || HV.test(b)) errors.push(`12V の線の隣に別の線: ${a} と ${b}（${where}）`);
    else if ((a === 'PGND' && b === 'SGND') || (a === 'SGND' && b === 'PGND')) errors.push(`PGND と SGND が隣り合っている（${where}）`);
    else if ((/^\+5V/.test(a) && GPIO_NETS.has(b)) || (/^\+5V/.test(b) && GPIO_NETS.has(a))) errors.push(`5V の隣に ESP32 のピンにつながる線: ${a} と ${b}（${where}）`);
  }
}

// 部品の形どうしの重なり、線の端を挿せるか
function inside(body, [x, y]) {
  if (body.rect) { const [x0, y0, x1, y1] = body.rect; return x > x0 && x < x1 && y > y0 && y < y1; }
  const [cx, cy, r] = body.circle; return (x - cx) ** 2 + (y - cy) ** 2 < r * r;
}
function overlap(a, b) {
  if (a.rect && b.rect) { const [a0, a1, a2, a3] = a.rect; const [b0, b1, b2, b3] = b.rect; return a0 < b2 && b0 < a2 && a1 < b3 && b1 < a3; }
  if (a.circle && b.circle) { const [ax, ay, ar] = a.circle; const [bx, by, br] = b.circle; return (ax - bx) ** 2 + (ay - by) ** 2 < (ar + br) ** 2; }
  const c = a.circle ? a : b; const r = a.rect ? a : b;
  const [cx, cy, cr0] = c.circle; const [x0, y0, x1, y1] = r.rect;
  const nx = Math.max(x0, Math.min(cx, x1)); const ny = Math.max(y0, Math.min(cy, y1));
  return (cx - nx) ** 2 + (cy - ny) ** 2 < cr0 * cr0;
}
const solid = parts.filter((p) => p.body.length && p.z !== 'off');
for (let i = 0; i < solid.length; i++) for (let j = i + 1; j < solid.length; j++) {
  const a = solid[i]; const b = solid[j];
  if ((a.z === 'raised' && b.z === 'low') || (b.z === 'raised' && a.z === 'low')) {
    if (a.body.some((x) => b.body.some((y) => overlap(x, y)))) notes.push(`${a.z === 'raised' ? b.ref : a.ref} は ESP32 の下（ESP32 を挿す前に付ける）`);
    continue;
  }
  if (a.body.some((x) => b.body.some((y) => overlap(x, y)))) errors.push(`${a.ref} と ${b.ref} の形が重なる`);
}
for (const p of parts) if (p.shadow) for (const q of solid) if (q !== p && q.body.some((b) => overlap(b, p.shadow))) errors.push(`${q.ref} が ${p.ref}（立てたドライバ）の場所に入っている`);
for (const w of top) for (const xy of [w.from, w.to]) for (const p of solid) {
  if (!p.body.some((b) => inside(b, xy))) continue;
  if (p.z === 'raised') notes.push(`${w.id} の端 ${cr(xy)} は ESP32 の下（ESP32 を挿す前に付ける）`);
  else errors.push(`${w.id} の端 ${cr(xy)} が ${p.ref} の下で、上から挿せない`);
}
for (const w of top) for (const xy of [w.from, w.to]) for (const p of parts) if (p.shadow && inside(p.shadow, xy)) notes.push(`${w.id} の端 ${cr(xy)} は ${p.ref} を立てる所（ドライバを挿す前に付ける）`);

// ---------------------------------------------------------------- 色と線
const NET_COLOR = [
  [/^(VM|VM_U4)$/, '#d62728'], [/^PGND$/, '#2b2b2b'], [/^OUT/, '#ff7f0e'], [/^\+5V/, '#e377c2'], [/^\+3V3$/, '#a89c00'],
  [/^SGND$/, '#1f63c6'], [/^VBAT_SENSE$/, '#9467bd'], [/^M\d_IN/, '#2ca02c'], [/^ENC/, '#12a3b4'], [/^LED/, '#8c564b'],
  [/^BENCH_POT$/, '#6b6b6b'], [/^unconnected/, '#c8c8c8'],
];
const netColor = (net) => { if (!net) return '#999'; for (const [re, c] of NET_COLOR) if (re.test(net)) return c; return '#999'; };
const BACK_W = { bus: 1.0, out: 0.8, stub: 0.5 };
const TOP_W = { P: 0.75, L: 0.6, S: 0.45 };

// ---------------------------------------------------------------- 図
const OX = 5;
const OY = 14;
const SVG_W = BOARD.w + 2 * OX;
const SVG_H = OY + BOARD.h + 37;
const FONT = "'Yu Gothic','Meiryo','Hiragino Sans',sans-serif";
const f2 = (v) => Math.round(v * 100) / 100;

function render(view) {
  const mir = view === 'back';
  const X = (x) => f2(OX + MX + (mir ? BOARD.cols - 1 - x : x) * P);
  const Y = (y) => f2(OY + MY + y * P);
  const o = [];
  const t = (x, y, s, size = 1.6, extra = '') => o.push(`<text x="${f2(x)}" y="${f2(y)}" font-size="${size}" ${extra}>${s}</text>`);
  o.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${SVG_W}mm" height="${SVG_H}mm" viewBox="0 0 ${SVG_W} ${SVG_H}" font-family="${FONT}">`);
  o.push(`<rect x="0" y="0" width="${SVG_W}" height="${SVG_H}" fill="#ffffff"/>`);
  const title = mir
    ? 'はんだ面から見た図（左右が反対）— 裸のすずめっき線（裏の線）'
    : '部品面から見た図 — 部品の置き場所と、被覆つきの線（表の線）';
  t(OX, 6, title, 3.2, 'font-weight="bold"');
  t(OX, 10.5, `mobile-base（段階3）を 9×15cm の板1枚に。1:1 で印刷すると板に重ねられる。穴の数は 30 行 × 48 列に収めた（多い板は右と下が余る）。`, 1.9);
  // 基板
  o.push(`<rect x="${OX}" y="${OY}" width="${BOARD.w}" height="${BOARD.h}" rx="2" fill="#f4f0e2" stroke="#7a6f50" stroke-width="0.35"/>`);
  // 取付穴（板による）
  for (const [mx, my] of [[4, 4], [BOARD.w - 4, 4], [4, BOARD.h - 4], [BOARD.w - 4, BOARD.h - 4]]) {
    o.push(`<circle cx="${OX + mx}" cy="${OY + my}" r="1.6" fill="#ffffff" stroke="#7a6f50" stroke-width="0.25" stroke-dasharray="0.6 0.4"/>`);
  }
  // ゾーン
  const zone = (x0, y0, x1, y1, fill, label, lx, ly) => {
    const xa = Math.min(X(x0), X(x1)) - P / 2; const xb = Math.max(X(x0), X(x1)) + P / 2;
    o.push(`<rect x="${f2(xa)}" y="${f2(Y(y0) - P / 2)}" width="${f2(xb - xa)}" height="${f2(Y(y1) - Y(y0) + P)}" fill="${fill}" stroke="none"/>`);
    if (label) t(X(lx), Y(ly), label, 1.8, `fill="#7a5050" font-weight="bold" ${mir ? 'text-anchor="end"' : ''}`);
  };
  zone(0, 0, 47, 13, '#fbe3e1', '', 0, 0);
  zone(0, 14, 47, 29, '#e3ecfb', '', 0, 0);
  // 穴
  for (let y = 0; y < BOARD.rows; y++) for (let x = 0; x < BOARD.cols; x++) {
    o.push(`<circle cx="${X(x)}" cy="${Y(y)}" r="0.42" fill="#d7c69a"/>`);
  }
  // 列・行の番号
  for (let x = 0; x < BOARD.cols; x++) if ((x + 1) % 5 === 0 || x === 0 || x === BOARD.cols - 1) {
    t(X(x), OY + MY - 3.0, String(x + 1), 1.5, 'text-anchor="middle" fill="#7a6f50"');
    t(X(x), OY + MY + (BOARD.rows - 1) * P + 4.2, String(x + 1), 1.5, 'text-anchor="middle" fill="#7a6f50"');
  }
  for (let y = 0; y < BOARD.rows; y++) if ((y + 1) % 5 === 0 || y === 0 || y === BOARD.rows - 1) {
    t(OX + MX - 3.4, Y(y) + 0.5, String(y + 1), 1.5, 'text-anchor="middle" fill="#7a6f50"');
    t(OX + MX + (BOARD.cols - 1) * P + 3.4, Y(y) + 0.5, String(y + 1), 1.5, 'text-anchor="middle" fill="#7a6f50"');
  }
  t(OX + MX - 3.4, OY + MY - 3.0, '行＼列', 1.2, 'text-anchor="middle" fill="#7a6f50"');
  // ゾーンの名前

  const bodyShape = (b, fill, stroke, dash = '', sw = 0.25) => {
    if (b.rect) {
      const [x0, y0, x1, y1] = b.rect;
      const xa = Math.min(X(x0), X(x1)); const xb = Math.max(X(x0), X(x1));
      return `<rect x="${f2(xa)}" y="${f2(Y(y0))}" width="${f2(xb - xa)}" height="${f2(Y(y1) - Y(y0))}" rx="0.4" fill="${fill}" stroke="${stroke}" stroke-width="${sw}" ${dash ? `stroke-dasharray="${dash}"` : ''}/>`;
    }
    const [cx, cy, r] = b.circle;
    return `<circle cx="${X(cx)}" cy="${Y(cy)}" r="${f2(r * P)}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}" ${dash ? `stroke-dasharray="${dash}"` : ''}/>`;
  };
  const KIND_FILL = { term: '#5fae5f', ecap: '#3d4f8a', drv: '#555555', reg: '#333333', esp32: 'none', dip: '#444444', pot: '#4c7fb0', hdr: '#333333', res: '#d9b36c', cap: '#e8a33d', diode: '#444444' };

  // 立てたドライバの場所（部品を置かない）
  if (!mir) for (const p of parts) if (p.shadow) {
    o.push(bodyShape(p.shadow, 'rgba(120,120,120,0.12)', '#888', '0.8 0.5', 0.2));
    const [x0, y0] = p.shadow.rect;
    t(X(x0) + 0.3, Y(y0) + 1.5, `${p.ref}（立てる）`, 1.3, 'fill="#555"');
  }
  // ESP32 の外形
  const esp = parts.find((p) => p.ref === 'U5');
  // 部品（表の図だけ形を塗る）
  for (const p of parts) {
    if (p.z === 'off' || p.ref === 'U5') continue;
    for (const b of p.body) {
      if (mir) o.push(bodyShape(b, 'none', '#b0a27a', '0.6 0.4', 0.2));
      else o.push(bodyShape(b, p.z === 'high' ? KIND_FILL[p.kind] + 'cc' : KIND_FILL[p.kind] + '99', '#222', '', 0.2));
    }
  }
  if (!mir) {
    // ソケット
    for (const yy of [17, 27]) o.push(bodyShape(Rc(27.5, yy - 0.45, 46.5, yy + 0.45), '#333333aa', '#222'));
  }
  // 裏の線
  for (const b of back) {
    const net = compNet(key(...b.pts[0]));
    const pts = b.pts.map(([x, y]) => `${X(x)},${Y(y)}`).join(' ');
    if (mir) {
      o.push(`<polyline points="${pts}" fill="none" stroke="#ffffff" stroke-width="${BACK_W[b.cls] + 0.5}" stroke-linecap="round" stroke-linejoin="round"/>`);
      o.push(`<polyline points="${pts}" fill="none" stroke="${netColor(net)}" stroke-width="${BACK_W[b.cls]}" stroke-linecap="round" stroke-linejoin="round"/>`);
    } else {
      o.push(`<polyline points="${pts}" fill="none" stroke="${netColor(net)}" stroke-opacity="0.45" stroke-width="${f2(BACK_W[b.cls] * 0.6)}" stroke-dasharray="0.7 0.5" stroke-linecap="round"/>`);
    }
  }
  // 足
  for (const p of parts) for (const [n, xy] of Object.entries(p.pins)) {
    const net = netOf(p, n);
    const isTP = p.kind === 'tp';
    o.push(`<circle cx="${X(xy[0])}" cy="${Y(xy[1])}" r="${isTP ? 0.85 : 0.7}" fill="${netColor(net)}" stroke="${isTP ? '#000' : '#fff'}" stroke-width="${isTP ? 0.3 : 0.18}"/>`);
    if (n === '1' && Object.keys(p.pins).length > 2 && !isTP) o.push(`<rect x="${f2(X(xy[0]) - 0.85)}" y="${f2(Y(xy[1]) - 0.85)}" width="1.7" height="1.7" fill="none" stroke="#000" stroke-width="0.22"/>`);
  }
  // 線の端（表の線の口）
  for (const w of top) for (const xy of [w.from, w.to]) {
    o.push(`<circle cx="${X(xy[0])}" cy="${Y(xy[1])}" r="0.5" fill="#ffffff" stroke="${netColor(compNet(key(...xy)))}" stroke-width="0.35"/>`);
  }
  // ESP32 の外形（上に重ねて点線）
  if (esp) {
    o.push(bodyShape(esp.body[0], mir ? 'none' : 'rgba(40,40,40,0.05)', '#222', '1.2 0.6', 0.35));
    const [x0, y0, x1] = esp.body[0].rect;
    if (!mir) {
      o.push(bodyShape(Rc(25.6, 18.6, 27.3, 25.4), 'rgba(250,220,90,0.35)', '#b08b00', '0.5 0.4', 0.2));
      t(X(25.75), Y(22.3), 'ｱﾝﾃﾅ', 1.3, 'fill="#806000"');
      t(X(46.2), Y(22.3), 'USB→', 1.6, 'text-anchor="end" fill="#333" font-weight="bold"');
      t(X(46.3), Y(24.3), 'U5 ESP32', 1.8, 'text-anchor="end" fill="#333" font-weight="bold"');
    } else {
      t((X(x0) + X(x1)) / 2, Y(y0) + 2.0, 'U5 ESP32 の列', 1.5, 'text-anchor="middle" fill="#555"');
    }
  }
  // F2（ホルダは基板の外。2本の線で入る）
  if (!mir) {
    const f = parts.find((p) => p.ref === 'F2');
    const [a1, a2] = [f.pins[1], f.pins[2]];
    o.push(`<polyline points="${X(a1[0])},${Y(a1[1])} ${X(11.7)},${Y(10.5)} ${X(11.7)},${Y(4.5)} ${X(a2[0])},${Y(a2[1])}" fill="none" stroke="#d62728" stroke-width="0.3" stroke-dasharray="0.8 0.5"/>`);
    o.push(`<rect x="${f2(X(11.7) - 0.9)}" y="${f2(Y(7.5) - 1.6)}" width="1.8" height="3.2" rx="0.3" fill="#ffffff" stroke="#d62728" stroke-width="0.3"/>`);
  }
  // NT1（GND-STAR と SGND をつなぐ1本の線）
  if (!mir) for (const p of parts) if (p.route) {
    const pts = [p.pins[1], ...p.route, p.pins[2]].map(([x, y]) => `${X(x)},${Y(y)}`).join(' ');
    o.push(`<polyline points="${pts}" fill="none" stroke="#ffffff" stroke-width="1.1" stroke-linejoin="round" stroke-linecap="round"/>`);
    o.push(`<polyline points="${pts}" fill="none" stroke="#2b2b2b" stroke-width="0.7" stroke-linejoin="round" stroke-linecap="round"/>`);
    o.push(`<polyline points="${pts}" fill="none" stroke="#1f63c6" stroke-width="0.3" stroke-dasharray="0.9 0.9" stroke-linejoin="round"/>`);
    const [mx, my] = p.route[1];
    o.push(`<rect x="${f2(X(mx) + 0.4)}" y="${f2(Y(my) - 0.9)}" width="4.6" height="1.6" rx="0.3" fill="#ffffff" stroke="#2b2b2b" stroke-width="0.25"/>`);
    t(X(mx) + 2.7, Y(my) + 0.4, p.ref, 1.25, 'text-anchor="middle" font-weight="bold"');
  }
  // 表の線
  if (!mir) for (const w of top) {
    const net = compNet(key(...w.from));
    const pts = [w.from, ...w.via, w.to].map(([x, y]) => `${X(x)},${Y(y)}`).join(' ');
    o.push(`<polyline points="${pts}" fill="none" stroke="#ffffff" stroke-width="${TOP_W[w.cls] + 0.35}" stroke-linejoin="round" stroke-linecap="round" opacity="0.9"/>`);
    o.push(`<polyline points="${pts}" fill="none" stroke="${netColor(net)}" stroke-width="${TOP_W[w.cls]}" stroke-linejoin="round" stroke-linecap="round"/>`);
    const all = [w.from, ...w.via, w.to];
    const mid = all[Math.floor(all.length / 2)];
    const prev = all[Math.floor(all.length / 2) - 1] || mid;
    const lx = (X(mid[0]) + X(prev[0])) / 2; const ly = (Y(mid[1]) + Y(prev[1])) / 2;
    o.push(`<rect x="${f2(lx - 1.6)}" y="${f2(ly - 0.95)}" width="3.2" height="1.6" rx="0.3" fill="#ffffff" stroke="${netColor(net)}" stroke-width="0.2"/>`);
    t(lx, ly + 0.45, w.id.slice(1), 1.25, 'text-anchor="middle" font-weight="bold"');
  }
  // 部品の名前
  for (const p of parts) {
    if (p.ref === 'U5' || p.z === 'off' && p.ref !== 'F2') continue;
    let lx; let ly;
    if (p.body.length) {
      const b = p.body[0];
      if (b.rect) { lx = (X(b.rect[0]) + X(b.rect[2])) / 2; ly = (Y(b.rect[1]) + Y(b.rect[3])) / 2; } else { lx = X(b.circle[0]); ly = Y(b.circle[1]); }
    } else { const xy = Object.values(p.pins)[0]; lx = X(xy[0]); ly = Y(xy[1]) - 1.2; }
    if (p.ref === 'F2') { if (mir) continue; lx = X(11.7) - 2.4; ly = Y(7.5) + 0.1; }
    const LAB_OFF = {
      TP1: [1.9, 1.7], TP2: [2.2, 0.5], TP3: [-2.4, 0.5], TP4: [-2.4, 0.5], TP5: [-1.9, 0.5], TP8: [-1.9, 0.5], TP9: [-1.9, 0.5], TP6: [2.6, 0.5], TP7: [2.6, 0.5], TPG: [-2.6, 0.5], TPS: [0, -0.6],
      F2: [0, 0], NT1: [-2.2, 0.5], R17: [-1.8, 0.5], C14: [-1.8, 0.5], R1: [1.8, 0.5], D3: [1.8, 0.5], C13: [2.3, 0.5], C6: [1.8, 0.5], R2: [-1.8, 0.5],
      R10: [-1.8, 0.4], R11: [1.8, 0.4], R12: [-1.8, 0.4], R13: [1.8, 0.4], R14: [-1.8, 0.4], R15: [1.8, 0.4], JP1: [0, -1.2], C7: [1.8, 0.5], C8: [1.8, 0.5], C9: [1.8, 0.5],
      U4: [-1.75, 0.55], C10: [0, -0.9], C11: [0, -0.9], C12: [0, -0.9], U1: [0, 2.3], U2: [0, 2.3], U3: [0, 2.3],
    };
    const off = LAB_OFF[p.ref] || [0, 0.55];
    const dx = mir ? -off[0] : off[0];
    const light = !mir && p.body.length && p.z === 'high' && !['VR1'].includes(p.ref);
    t(lx + dx, ly + off[1], p.label, p.kind === 'tp' ? 1.3 : 1.55, `text-anchor="middle" font-weight="bold" fill="${mir ? '#7a6f50' : light ? '#ffffff' : '#111'}"`);
  }
  // 凡例
  const ly0 = OY + BOARD.h + 5;
  t(OX, ly0, mir ? '裏の線（裸のすずめっき線。通る穴はすべてはんだ付け）' : '表の線（被覆つき。番号は wires.md の配線表）', 2.0, 'font-weight="bold"');
  const legend = [
    ['VM（12V）', 'VM'], ['PGND', 'PGND'], ['モータの出力', 'OUT0+'], ['5V', '+5V'], ['3V3', '+3V3'], ['SGND', 'SGND'],
    ['IN（ドライバへ）', 'M0_IN1'], ['エンコーダ', 'ENC0_A'], ['LED', 'LED_DIN'], ['電池電圧', 'VBAT_SENSE'], ['つまみ', 'BENCH_POT'],
  ];
  legend.forEach(([name, net], i) => {
    const cx = OX + (i % 6) * 25; const cy = ly0 + 4 + Math.floor(i / 6) * 4;
    o.push(`<line x1="${cx}" y1="${cy - 0.5}" x2="${cx + 5}" y2="${cy - 0.5}" stroke="${netColor(net)}" stroke-width="1"/>`);
    t(cx + 6, cy, name, 1.7);
  });
  const notesL = mir
    ? [
      '太さ: 太い線 = 母線（φ1.0、少なくとも φ0.8）。中くらい = モータ・ドライバの電流（φ0.8）。細い線 = そのほか（φ0.5〜0.6 か部品の足の切れ端）。',
      '母線（行 10 の PGND、行 12 の VM）の間の行 11 には何も付けない。VM の線の隣（上下左右の穴）に別の線を付けない。',
      '左右が反対の図。基板を裏返したとき、この図の左が部品面の右になる。',
    ]
    : [
      '線の太さ: P = 2.3A まで（いちばん太い被覆つき単線、AWG20〜22）、L = 0.8A まで（AWG22）、S = 信号（AWG22〜24）。',
      '□ は 1 番ピン。黒い縁の丸はテスターを当てる点（TP）。灰色の点線の四角は、ドライバを立てて挿す所（部品を置かず、線だけ通してよい）。',
      '薄い点線は裏の線（はんだ面の図を見る）。ESP32 の下の線と部品は、ESP32 を挿す前に付ける。F2 のホルダは基板の外。',
    ];
  o.push(`<rect x="${OX}" y="${ly0 + 10.6}" width="5" height="2.4" fill="#fbe3e1" stroke="#c99"  stroke-width="0.15"/>`);
  t(OX + 6, ly0 + 12.5, '上の帯（行 1〜14）: 12V・モータ', 1.7);
  o.push(`<rect x="${OX + 50}" y="${ly0 + 10.6}" width="5" height="2.4" fill="#e3ecfb" stroke="#99c" stroke-width="0.15"/>`);
  t(OX + 56, ly0 + 12.5, '下の帯（行 15〜30）: 信号', 1.7);
  notesL.forEach((s, i) => t(OX, ly0 + 17.5 + i * 3.4, s, 1.7));
  o.push('</svg>');
  return o.join('\n');
}

// ---------------------------------------------------------------- 配線表
function endDesc(xy) {
  const r = find(key(...xy));
  let best = null;
  for (const p of parts) for (const [n, pxy] of Object.entries(p.pins)) {
    if (find(key(...pxy)) !== r) continue;
    const d = Math.abs(pxy[0] - xy[0]) + Math.abs(pxy[1] - xy[1]) + (/^(TP|NT1)/.test(p.ref) ? 10 : 0);
    if (!best || d < best.d) best = { d, ref: p.ref, n };
  }
  return best ? pinLabel(best.ref, best.n) : '—';
}
const lenMM = (pts) => { let s = 0; for (let i = 1; i < pts.length; i++) s += Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]) * P; return s; };
function wiresMd() {
  const L = [];
  L.push('# 配線表（ユニバーサル基板）');
  L.push('');
  L.push('`layout.mjs` が書き出したもの。手で直さない（直すときは `layout.mjs` を直して `node layout.mjs` を流し直す）。');
  L.push('「列,行」は左上の穴を 1,1 として数えた位置（部品面から見て）。図は [layout-top.svg](./layout-top.svg)（部品面）と [layout-back.svg](./layout-back.svg)（はんだ面）。');
  L.push('');
  L.push(`検査の結果: 誤り ${errors.length} 件、注意 ${warns.length} 件。回路図（nets.json）の ${Object.keys(NETS).length} 本のネットと照らし合わせた。`);
  L.push('');
  L.push('## 表の線（被覆つき）');
  L.push('');
  L.push('線の端は、つなぐ足のすぐ隣の穴に通し、裏で裸の線（はんだ面の図）とはんだ付けする。長さは図の道のりで、1割ほど長めに切る。');
  L.push('');
  L.push('| 線 | 種類 | ネット | 端 A（列,行） | 端 B（列,行） | 長さの目安 | メモ |');
  L.push('|---|---|---|---|---|---|---|');
  for (const w of top) {
    const net = compNet(key(...w.from));
    const len = lenMM([w.from, ...w.via, w.to]);
    L.push(`| ${w.id} | ${w.cls} | \`${net}\` | ${cr(w.from)} ${endDesc(w.from)} | ${cr(w.to)} ${endDesc(w.to)} | ${Math.round(len / 5) * 5 + 10}mm | ${w.note} |`);
  }
  for (const p of parts) if (p.route) {
    const len = lenMM([p.pins[1], ...p.route, p.pins[2]]);
    L.push(`| ${p.ref} | L | \`PGND\` ↔ \`SGND\` | ${cr(p.pins[1])} GND-STAR（C1-2） | ${cr(p.pins[2])} SGND（U5-20 へ） | ${Math.round(len / 5) * 5 + 10}mm | ${p.note} |`);
  }
  L.push('');
  L.push('種類: P = 2.3A まで（手元の被覆つき単線でいちばん太いもの）、L = 0.8A まで（AWG22 以上）、S = 信号（AWG24 でよい）。');
  L.push('');
  L.push('## 裏の線（裸のすずめっき線）の量');
  L.push('');
  L.push('| 種類 | 太さ | 本数 | 長さの合計 |');
  L.push('|---|---|---|---|');
  const sum = {};
  for (const b of back) { sum[b.cls] = sum[b.cls] || { n: 0, len: 0 }; sum[b.cls].n++; sum[b.cls].len += lenMM(b.pts); }
  const CLS = { bus: '母線（PGND・VM、GND-STAR の島）', out: 'モータ・ドライバの電流（2.3A まで）', stub: 'そのほか（信号・5V・3V3・SGND）' };
  const DIA = { bus: 'φ1.0（φ0.8 以上）', out: 'φ0.8', stub: 'φ0.5〜0.6 か部品の足の切れ端' };
  for (const c of ['bus', 'out', 'stub']) if (sum[c]) L.push(`| ${CLS[c]} | ${DIA[c]} | ${sum[c].n} | 約 ${Math.round(sum[c].len / 10)}cm |`);
  L.push('');
  L.push('## 部品の位置');
  L.push('');
  L.push('| 部品 | 足（列,行） | メモ |');
  L.push('|---|---|---|');
  for (const p of parts) {
    const pins = Object.entries(p.pins).map(([n, xy]) => `${n}: ${cr(xy)}${pinName(p.ref, n) ? `（${pinName(p.ref, n)}）` : ''}`).join('、');
    const shortPins = p.ref === 'U5' ? '南の列 1〜19: 29,28 → 47,28、北の列 20〜38: 29,18 → 47,18' : pins;
    L.push(`| ${p.label} | ${shortPins} | ${p.note} |`);
  }
  if (notes.length) {
    L.push('');
    L.push('## 付ける順番の注意（検査が見つけたもの）');
    L.push('');
    for (const n of [...new Set(notes)]) L.push(`- ${n}`);
  }
  L.push('');
  return L.join('\n');
}

fs.writeFileSync(path.join(HERE, 'layout-top.svg'), render('top') + '\n');
fs.writeFileSync(path.join(HERE, 'layout-back.svg'), render('back') + '\n');
fs.writeFileSync(path.join(HERE, 'wires.md'), wiresMd());

console.log(`部品 ${parts.length}、裏の線 ${back.length}、表の線 ${top.length}、使う穴 ${[...holes.values()].filter(conductive).length}`);
console.log(`誤り ${errors.length} 件`);
for (const e of errors) console.log('  ✗ ' + e);
console.log(`注意 ${warns.length} 件`);
for (const w of warns) console.log('  ! ' + w);
const kinds = {};
for (const a of adjacent) { const k = [a.a, a.b].sort().join(' / '); kinds[k] = (kinds[k] || 0) + 1; }
if (process.argv.includes('--adj')) for (const a of adjacent) console.log(`    ${a.a} / ${a.b}: ${a.where}`);
console.log('隣り合う別のネット（許したもの）:');
for (const [k, n] of Object.entries(kinds).sort((x, y) => y[1] - x[1])) console.log(`  ${n} × ${k}`);
process.exitCode = errors.length ? 1 : 0;
