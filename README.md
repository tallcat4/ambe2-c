# ambe2-c

AMBE+2 半レート(2450 bps)のエンコーダとデコーダを、C99 だけで実装したものです。AMBE-3000R(ThumbDV など)とビット互換のフレームを扱います。デコーダは、AMBE-3000R の AMBE+(AMBE-2000 系)の 2000 / 2400 / 4000 bps と、その FEC 付きのレートにも対応しています。

## 主な機能

- **オリジナルのエンコーダ** — 日本語の ABX テストで、AMBE-3000R のエンコーダと聞き分けられませんでした(30 問中 18 問、p = 0.18)。
- **ビット一致のデコーダ** — blip25-vocoder(Rust)の移植。チップの復号音との差 0.77 dB(mbelib は 5.7 dB)。mbelib 互換の関数と軟判定つき。
- **AMBE+ のデコーダ** — AMBE-3000R の RATET 31、0、22、17、26(2400 bps は D-STAR と同じ音声ビット)。仕様非公開のレートを、チップの計測から割り出しました。チップの復号音との差は 2000 / 2400 bps で約 1.2 dB、4000 bps で 2.2 dB([マニュアル](docs/ambeplus.md))。
- **マイコンで動く** — エンコードとデコードを同時に動かして、Portenta H7 で CPU の 12%、ESP32-WROOM-32 で 1 コアの 37%。
- **固定小数点版** — エンコーダは整数演算だけの版もあり、どの CPU でも同じビット列を出します。
- **背景雑音モード** — 無音・背景雑音を背景雑音フレームで送る、AMBE-3000R の ECMODE 0x0800 相当のモード。
- **依存なし** — malloc なし、外部ライブラリなし。

## 動作環境

- C99 のコンパイラ(macOS / Linux で `cc`、ESP-IDF、Arduino で確認)

## ビルド

```bash
make             # encoder/wav2ambe、decoder/ambe2wav、ライブラリ
make FIXED=1     # エンコーダを固定小数点版で(MCU 向け)
make test        # 参照実装とのビット一致と速度
```

## 使い方

```bash
encoder/wav2ambe in.wav out.ambe       # 8 kHz・モノラルの WAV → 7 バイト/20 ms のフレーム列(AMBE-3000R rate 34)
decoder/ambe2wav out.ambe out.wav      # フレーム列 → WAV
```

C の API、mbelib からの移行、マイコンでの実測、無音フレームの仕様は、[エンコーダ](docs/encoder.md) と [デコーダ](docs/decoder.md) のマニュアルを参照してください。AMBE+ のレートは [AMBE+ デコーダ](docs/ambeplus.md) にまとめています。

## ライセンス

MIT。詳細は [`LICENSE`](LICENSE) を参照してください。

- `decoder/` は blip25-vocoder(MIT、Chance Lindsey)の派生物です([`decoder/LICENSE`](decoder/LICENSE))。mbelib 互換 API は mbelib 1.3(ISC)のインターフェースを再現しています(許諾表示は `decoder/include/mbelib.h`)。
- テスト用の音声は LibriSpeech(CC BY 4.0)の抜粋です。
- AMBE+2 には特許があります(US 8,359,197 ほか)。
