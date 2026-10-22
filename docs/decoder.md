# デコーダ

AMBE+2 と AMBE+ のデコーダです(`decoder/`)。API は `include/blip25.h` と `include/mbelib.h` にあります。

## 構成

| ソース | 内容 |
|---|---|
| `src/b25_*.c` | blip25-vocoder(Rust)の AMBE+2 デコーダを、1 関数ずつ C に移植したもの。規則は [porting.md](porting.md) |
| `src/blip25.c` | API とフレーム形式の変換 |
| `src/mbelib_compat.c` | mbelib 1.3 と同じ名前と引数の関数 |
| `src/ambeplus*.c` | AMBE+ のデコーダ。合成には blip25 のエンジンを使う |

- AMBE+2 のデコーダは、blip25-vocoder 1.0 とビット一致します。`make test` で確かめられます。
- 移植は、2 の補数と算術右シフトの処理系を前提にしています(GCC、Clang、MSVC)。

## mbelib からの移行

`include/mbelib.h` を使い、`libmbe` の代わりに `libblip25.a` をリンクします。mbelib との違いは次のとおりです。

- デコーダの状態は `cur_mp` に入ります。ストリームごとに別の `mbe_parms` を使ってください。
- `prev_mp`、`prev_mp_enhanced`、`uvquality` は使いません。`w0`、`L`、`Vl` などの欄も埋めません。
- C0 で 4 ビットの誤りを検出したフレームは、補間に回します。

## AMBE+

| RATET | 関数 |
|---|---|
| 31 | `blip25_decode_ambeplus_r31` |
| 0 | `blip25_decode_ambeplus_r0` |
| 22 | `blip25_decode_ambeplus_r22` |
| 17 | `blip25_decode_ambeplus_r17` |
| 26 | `blip25_decode_ambeplus_r26` |

- AMBE+ のレートの仕様は公開されていません。フレームの形式は、チップで測った結果と、失効した DVSI の特許から割り出しました。
- 2400 bps の符号帳は mbelib の D-STAR 用と同じです。4000 bps の 40〜49 番と 66〜78 番のビットの符号帳は、実際の音声に当てはめた推定値です。4000 bps のチップとの差(2.03 dB)の大部分はここにあります。
- 対応していないもの: b0 ≥ 124 のフレーム(トーン、無音、消失。今はフレーム繰り返しとして扱います)。

測定と当てはめのスクリプトは neural-ambe2 の `ambeplus/` にあります。
