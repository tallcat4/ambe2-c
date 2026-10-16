# ambe2-c — AMBE+2 2450 bps エンコーダとデコーダ（純 C）

AMBE-3000R（ThumbDV など）とビット互換の AMBE+2 半レート（2450 bps）コーデックを、C99 だけで実装したものです。

| | ディレクトリ | 中身 | 品質 | 速度（Apple M5、1 フレーム 20 ms あたり） |
|---|---|---|---|---|
| **エンコーダ** | [`encoder/`](encoder/README.md) | オリジナル実装（AMBE-3000R の出力を教師に学習した解析＋仕様どおりの量子化） | 日本語の ABX テストで、チップのエンコーダと聞き分けられず（30 問中 18 問、p = 0.18） | 39 µs（約 510 倍速）、malloc なし |
| **デコーダ** | [`decoder/`](decoder/README.md) | blip25-vocoder（Rust, MIT）の C 移植。Rust 版とビット一致、mbelib 互換の関数と軟判定つき | チップの復号音との差 0.77 dB（mbelib は 5.7 dB） | 9.8 µs（約 2000 倍速）、整数演算のみ |

- フレームの形式は AMBE-3000R の rate 34（49 ビット / 7 バイト、FEC なし）です。
- デコーダは rate 33（3600x2450、FEC 付き 72 ビット）にも、硬判定・軟判定の両方で対応しています。
- mbelib・DSD の `ambe_d[49]` の並びとの相互変換も用意しています。
- エンコーダは、AMBE-3000R の ECMODE 0x0800 と同じように、無音・背景雑音を背景雑音フレーム（b0 = 124）で送るモードも持っています（任意。チップとのフレーム判定の一致 92%）。無音フレームの仕様の実測は [`encoder/README.md`](encoder/README.md#無音フレームの仕様ambe-3000r-の実測) にあります。

```
make            # encoder/libambeenc.a, encoder/wav2ambe, decoder/libblip25.a, decoder/ambe2wav
make test       # 両方のテスト（参照実装とのビット一致）と速度
./encoder/wav2ambe in.wav out.ambe       # 8 kHz モノラル WAV → 7 バイト／20 ms のフレーム列
./encoder/wav2ambe in.wav out.ambe --background   # 無音・背景雑音を背景雑音フレームで送る
./decoder/ambe2wav out.ambe out.wav      # フレーム列 → 8 kHz WAV
```
C のアプリへの組み込み方は、それぞれの README を見てください。mbelib を使っているアプリは、`decoder/include/mbelib.h` と `libblip25.a` に差し替えるだけで移行できます。

## 経緯
開発と研究は、別のリポジトリ（neural-ambe2）で行いました。
- AMBE-3000R の計測と教師データの収集
- 学習（Python）と Rust 版の試作
- 試聴テスト
- 定数表とテストデータの生成スクリプト

## ライセンス・注意
- `decoder/` は blip25-vocoder（MIT, Copyright (c) Chance Lindsey）の派生物です（`decoder/LICENSE`）。mbelib 互換 API は mbelib 1.3（ISC）のインターフェースを再現しています。
- `encoder/` のライセンスは未定です。
- `encoder/tests/vectors` の音声は JSUT と LibriSpeech からの抜粋です。
- AMBE+2 には特許があります（US 8,359,197 ほか）。blip25-vocoder の合成部はリファレンス実装の逆解析に基づいています。
