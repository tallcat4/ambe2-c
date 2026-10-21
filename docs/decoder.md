# decoder（blip25c）— blip25-vocoder の AMBE+2 半レート・デコーダの C 移植

> このページのコマンドは `decoder/` ディレクトリで実行します。

[blip25-vocoder](https://github.com/OpenBLIP25/blip25-vocoder)（Rust, MIT）の AMBE+2 2450 bps デコーダを、
C99 にビット単位で一致するよう移植したものです。mbelib と同じ名前・引数の関数も用意しているので、
mbelib を使っている C のアプリは、ヘッダとライブラリを差し替えるだけで移行できます。

- **音質**：blip25 の合成は、リファレンス実装の固定小数点版の移植です。AMBE-3000R チップ自身の復号音との差（mel スペクトル距離）は 0.77 dB で、無声フレームはサンプル単位で一致します。mbelib は 5.7 dB です。
- **軽さ**：整数演算のみで、動的メモリ確保も libm も使いません。デコーダ 1 つあたりの状態は約 2.5 KB、コードと定数表は約 77 KB（-Os）です。速度は Apple M5 で 1 フレーム 9.8 µs（実時間の約 2000 倍）で、Rust 版と同じです。
  MCU の実機では、Cortex-M7（400 MHz）で 0.81 ms、ESP32（240 MHz、コードと表を RAM に配置）で 2.0 ms です（rate 34。rate 33 はわずかに重い）。どちらも出力はホストとビット単位で一致しました（[エンコーダのマニュアル](encoder.md#mcu-での実測)）。
- **対応する形式**：AMBE+2 の半レートだけです。
  - 2450 bps（FEC なし 49 ビット）
  - 3600x2450 bps（FEC 付き 72 ビット、硬判定・軟判定）
  - IMBE（P25 Phase 1）には対応していません。
  - AMBE-3000R の AMBE+(AMBE-2000 系)のレート(2000 / 2400 / 4000 bps と FEC 付き、2400 bps は D-STAR と同じ音声ビット)は [AMBE+ デコーダ](ambeplus.md) を参照してください。こちらは浮動小数点演算と libm を使います。

## ビルド
```
make            # libblip25.a、ambe2wav（フレーム列 → WAV）、テスト用プログラム
make test       # 同梱のテストデータで blip25-vocoder（Rust）との一致を確認（下記）
```
ソースは `src/*.c` だけで、依存はありません。自分のビルドシステムに `src/*.c` と `include/` を加えても使えます。

## 使い方
### コマンドライン
```
./ambe2wav in.ambe out.wav            # 7 バイト／20 ms のフレーム列（AMBE-3000R rate 34 の並び）→ 8 kHz WAV
./ambe2wav in.ambe out.wav --ambe-d   # ambe_d 順（mbelib / DSD / .amb）のフレーム列
./ambe2wav in.ambe out.wav --rate33   # 9 バイト／20 ms の FEC 付きフレーム列（AMBE-3000R rate 33、wav2ambe --fec の出力）
```
エンコーダ([マニュアル](encoder.md))の `wav2ambe` と組み合わせると、C だけで符号化から復号まで試せます。

### mbelib からの移行
`#include <mbelib.h>` を、このリポジトリの `include/mbelib.h` に向け、`libmbe` の代わりに `libblip25.a` をリンクします。
次の関数が、mbelib 1.3 と同じ引数・同じビット配置で使えます。

```
mbe_initMbeParms, mbe_printVersion,
mbe_processAmbe3600x2450Frame(f), mbe_processAmbe2450Data(f),
mbe_eccAmbe3600x2450C0, mbe_demodulateAmbe3600x2450Data, mbe_eccAmbe3600x2450Data,
mbe_synthesizeSilence(f), mbe_floattoshort
```
`ambe_fr[4][24]` と `ambe_d[49]` の並び、`errs` / `errs2`、`err_str` の文字（`=`、`R`、`T`）、出力のスケールも mbelib と同じです。
出力のスケールは、float 版が short の 1/7、short 版が ±32760 で切ったものです。

mbelib との違い：
- デコーダの状態は `cur_mp` に入ります（`mbe_parms` の中に `blip25_decoder` を持っています）。そのため、ストリームごとに別の `mbe_parms` を使ってください（mbelib と同じ使い方です）。
- `prev_mp`、`prev_mp_enhanced`、`uvquality` は受け取りますが使いません。`w0`・`L`・`Vl` などのパラメータ欄は埋めません（`repeat` だけ更新します）。
- C0 は拡張 Golay [24,12] として復号し、4 ビット誤りを検出したフレームは補間（前フレームの繰り返しとフェード）に回します。
- `mbe_processAmbe2450Data(f)` では、呼び出し側が渡した `*errs2`（自前の FEC で訂正した数）を誤りの程度として、補間の強さに使います。

### 独自 API（`include/blip25.h`）
```c
#include "blip25.h"
blip25_decoder dec;                       /* 約 2.5 KB。スタックや static に置ける */
int16_t pcm[160];
blip25_init(&dec);
blip25_decode_2450(&dec, frame7, pcm);          /* 7 バイト、ambe_d 順（mbelib / DSD / .amb と同じ） */
blip25_decode_2450_thumbdv(&dec, frame7, pcm);  /* 7 バイト、AMBE-3000R rate 34 の並び */
blip25_decode_3600(&dec, frame9, pcm, &errs);   /* 9 バイト、AMBE-3000R rate 33（FEC 付き） */
blip25_decode_3600_soft(&dec, llr72, pcm);      /* 3600x2450 の軟判定 72 個（int8 の LLR、送信順） */
blip25_decode_bits(&dec, bits49, status, pcm);  /* 49 ビット（1 バイト 1 ビット）。status = FEC の誤りの程度 0..15、または BLIP25_LOST */
blip25_decode_lost(&dec, pcm);                  /* 受信できなかったフレーム */
```
軟判定の入力は、送信順の 72 個の LLR です（`llr[2k]` がディビット k の上位ビット、`llr[2k+1]` が下位ビット）。
符号が硬判定（正なら 1）、絶対値が確からしさです。blip25-vocoder の `Vocoder::decode_soft` と同じく、
Golay 符号を Chase-II で軟判定復号し、得られたビットを誤りなしのフレームとして合成します。
FEC だけを使うなら `blip25_fec_3600_soft` もあります。ビット誤り率 4.7% の通信路では、守られた 24 ビットを
正しく復元できたフレームの割合が、硬判定の 95.6% から 99.9% に上がりました。

戻り値は、そのフレームの扱いです（`BLIP25_SPEECH` / `BLIP25_TONE` / `BLIP25_REPEAT` / `BLIP25_SILENCE`）。
1 つの `blip25_decoder` は 1 本のストリーム専用です（スレッドセーフではありません）。

## ビット順の注意
| 形式 | 並び |
|---|---|
| ambe_d（49 ビット） | u0(12) u1(12) u2(11) u3(14)、上位ビットから。mbelib の `ambe_d[49]`、DSD、.amb ファイル、`blip25_decode_2450` |
| ThumbDV rate 34（7 バイト） | 上とは別の並び。`blip25_thumbdv_to_bits` で変換。pyambelib の `thumbdv_map` と同じ表 |
| AMBE-3000R rate 33（9 バイト） | 36 ディビット、P25 Phase 2 の Annex S インターリーブ。DSD / pyambelib の `rW/rX/rY/rZ` 表でほどいた `ambe_fr[v][i]` は、符号語 v のビット i（最下位ビットが 0） |

rate 33 の並びはチップで確認しました。同じ音声を rate 33 と rate 34 で符号化し、rate 33 を FEC でほどいた
49 ビットが rate 34 と 1432 フレームすべてで一致しました。

## 検証
`make test` は、同梱のテストデータ（`tests/vectors`）を復号し、出力のハッシュ値を blip25-vocoder 1.0（Rust）の出力のハッシュ値（`tests/expected.h`）と比べます。一致すれば、PCM がサンプル単位で同じです。

| テスト | 内容 | 結果 |
|---|---|---|
| speech | チップが出したビット列 24978 フレーム（日本語・英語） | 一致 |
| status | 同じビット列に、誤りの程度 0〜15 と消失をランダムに付けたもの | 一致 |
| random | ランダムな 49 ビット 4000 フレーム（補間・無音の経路も通る） | 一致 |
| chip3600 | チップの rate 33 出力 1432 フレーム（FEC の結果と PCM） | 一致 |
| noisy3600 | 同じものに 3% のビット誤りを入れたもの（訂正不能 13 フレームを含む） | 一致 |
| soft_noisy / soft_harsh | チップの rate 33 出力に雑音を加えた軟判定 LLR（ビット誤り率 13% / 24%） | 一致 |
| soft_ties | 同じく、LLR を粗く量子化して同点を多くしたもの | 一致 |
| soft_random | 絶対値 0〜4 のランダムな LLR 4000 フレーム（同点だらけ） | 一致 |

- AddressSanitizer と UndefinedBehaviorSanitizer の下（-O0）、-O3、-Os -ffast-math でも同じ結果です。
- mbelib 互換関数も、PCM（±32760 で切った値）と `ambe_d` が Rust 版と一致します（`mbe3600_*`、`mbe2450_pcm`）。
- テストデータの作り直しと、Rust 版をその場で動かして比べるツールは、研究用リポジトリ neural-ambe2 の `blip25c/tools/` にあります。

## 移植の方法
`src/b25_dsp_a.c`、`b25_dsp_b.c`、`b25_decode_c.c`、`b25_decode_d.c` は、blip25-vocoder の `src/engine/dsp.rs` と
`decode.rs` を 1 関数ずつ C に置き換えたものです。規則は [porting.md](porting.md) に書いてあります（生成スクリプトは neural-ambe2 の `blip25c/tools/`）。
- Rust の `wrapping_*` は、`src/b25_internal.h` の補助関数で、C の未定義動作なしに再現しています。
- 関数のプロトタイプは `tools/gen_protos.py` で Rust のシグネチャから生成しています。
- 定数表は `tools/gen_tables.py`、Golay の表は `tools/gen_golay.py` で生成しています。

前提にしている C 処理系の性質は、2 の補数、狭い符号付き型への変換で下位ビットが残ること、符号付き値の `>>` が算術シフトであることです。
GCC・Clang・MSVC はすべて満たします。

## ライセンス・注意
- このディレクトリのコードは blip25-vocoder（MIT, Copyright (c) Chance Lindsey）の派生物です（`LICENSE`）。
- mbelib 互換 API は、mbelib 1.3（ISC, Copyright (C) 2010 mbelib Author）のインターフェースとビット配置を再現しています。
- blip25-vocoder の合成部はリファレンス実装の逆解析に基づいており、AMBE+2 には特許があります。
  blip25-vocoder の PATENT_NOTICE.md と ATTRIBUTION.md を参照してください。
