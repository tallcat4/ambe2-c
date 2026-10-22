# エンコーダ

AMBE+2 2450 bps のエンコーダです(`encoder/`)。API は `include/ambe_enc.h` にあります。

## 構成

| 版 | ソース | ビルド | Apple M5 |
|---|---|---|---|
| double | `src/encoder.c analysis.c quant.c fft.c enc_tables.c` | `make` | 36 µs/フレーム |
| float | 同上 | `make CFLAGS="-O2 -DAMBE_ENC_FLOAT"` | 36 µs/フレーム |
| 固定小数点 | `src/fx_*.c` | `make FIXED=1` | 18 µs/フレーム |

- FEC 付き(3600x2450)の出力には、どの版でも `src/fec.c` を加えます。
- double 版は、numpy の参照実装(neural-ambe2 の `lite/lite_encoder.py`)とビット一致します。
- 固定小数点版の出力は、どの CPU でも同じです。音質は float 版との差が 0.01 dB 以内です。
- 定数(学習済みのモデルを含む)は neural-ambe2 で生成しています(`ambe-enc/tools/gen_tables.py`、`gen_fx_tables.py`)。

## しくみ

- ピッチと有声/無声は、パワースペクトルから求めた特徴量を小さな MLP で採点して決めます。ピッチは 1 フレーム先まで見るので、`ambe_enc_push` はひとつ前のフレームを返します。
- 量子化は仕様どおりです。表は、チップで測った結果に合わせて 4 か所直しています。

## マイコン

固定小数点版の、1 フレーム(20 ms)あたりの時間です。

| ボード | エンコード | デコード |
|---|---|---|
| Portenta H7(Cortex-M7、400 MHz) | 1.6 ms | 0.81 ms |
| ESP32-WROOM-32(240 MHz) | 5.3 ms | 2.0 ms |

- ESP32 では、コードと表を RAM に置いてください。フラッシュのままだと約 3 倍遅くなります。`examples/esp32/main/ambe_enc_ram.lf` のリンカ設定で置けます。
- 実機と QEMU でのテストは `tests/mcu/` にあります。

## 無音フレームの仕様

AMBE-3000R で測った、無音に関わるフレームの扱いです。

- **既定(ECMODE 0x0000)**: 一定値の入力は b0 = 119、b1 = 16、b2 = 0 のフレームになり、チップのデコーダでは完全なゼロになります。b3〜b8 は量子化器の状態で決まります。
- **ECMODE 0x0800**: 無音や定常的な雑音は、背景雑音フレーム(b0 = 124、b1 = 16、b2 = 雑音の大きさ)になります。このフレームは量子化器の予測を更新しません。
- **デコーダ**: b0 が 124 か 125 で、b1 が 16〜23 なら背景雑音として鳴らします。ほかの b0 = 120〜127 は消失として扱います。
- **DMR の無音フレーム**は、b2 = 1 の背景雑音フレームです。

このエンコーダは、一定値の入力を既定の動作と同じ形にします。`--background`(`ambe_enc_set_background`)では、チップの ECMODE 0x0800 の出力で学習した VAD を使って、背景雑音フレームを出します。フレームごとの判定はチップと 92% 一致します。

測定のスクリプトと記録は neural-ambe2 の `scripts/probe_silence.py` と `data/probe_silence/` にあります。
