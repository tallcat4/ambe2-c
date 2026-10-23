# ambe2-c

AMBE-3000R(ThumbDV など)とビット互換のフレームを扱う、AMBE 系ボコーダのエンコーダとデコーダです。C99 だけで書いています。

## 対応している形式

| 形式 | エンコーダ | デコーダ | RATET |
|---|---|---|---|
| AMBE+2 2450 | ○ | ○ | 34 |
| AMBE+2 3600x2450 | ○ | ○ | 33 |
| AMBE+ 2000 | ○ | ○ | 31 |
| AMBE+ 2400 | ○ | ○ | 0 |
| AMBE+ 4000x2400 | ○ | ○ | 22 |
| AMBE+ 4000 | ○ | ○ | 17 |
| AMBE+ 6400x4000 | ○ | ○ | 26 |

## 品質

- AMBE+2 のエンコーダの音声は、日本語の ABX テストでチップと聞き分けられませんでした(18/30、p = 0.18)。FEC 付きのフレームはチップとビット一致します。
- AMBE+2 のデコーダは blip25-vocoder とビット一致します。
- AMBE+ 6400x4000 のデコーダの音声も、ABX テストでチップと聞き分けられませんでした(17/30、p = 0.29)。
- チップが復号した音声との mel スペクトル距離は、AMBE+2 で 0.77 dB、AMBE+ で 1.19〜2.03 dB です。
- AMBE+ のエンコーダは、AMBE+2 と同じ解析に AMBE+ の符号帳を組み合わせたものです。チップのエンコーダの出力との距離(どちらもチップで復号)は 2.8〜3.4 dB で、AMBE+2(2.6 dB)より少し離れています。日本語の ABX テストでは、声の出だしのごく低いうなりを手がかりに、わずかに聞き分けられました(20/30、p = 0.049)([詳細](docs/encoder.md#ambe-のレート))。

## そのほかの機能

- **マイコンで動く**: AMBE+2 のエンコードとデコードを同時に動かして、Portenta H7 で CPU の 12%、ESP32-WROOM-32 で 1 コアの 37% です。
- **固定小数点版**: エンコーダには整数演算だけの版もあり、どの CPU でも同じビット列を出します(AMBE+2 のみ)。
- **背景雑音モード**: 無音や背景雑音を背景雑音フレームで送る、AMBE-3000R の ECMODE 0x0800 相当のモードです。
- **mbelib 互換**: デコーダは mbelib 1.3 と同じ名前と引数の関数を持っていて、差し替えるだけで移行できます。
- **依存なし**:
  - malloc も外部ライブラリも使いません。
  - AMBE+2 の部分は整数演算だけです。
  - AMBE+ のデコーダは浮動小数点演算と標準の数学ライブラリ(libm)を使います。

## 動作環境

- C99 のコンパイラ(macOS / Linux の `cc`、ESP-IDF、Arduino で確認)

## ビルド

```bash
make             # encoder/wav2ambe、decoder/ambe2wav、ライブラリ
make FIXED=1     # エンコーダを固定小数点版で(MCU 向け)
make test        # 参照実装・チップのデータとの一致と速度
```

## 使い方

```bash
encoder/wav2ambe in.wav out.ambe                 # 8 kHz・モノラルの WAV → 7 バイト/20 ms のフレーム列(RATET 34)
encoder/wav2ambe in.wav out.ambe --fec           # FEC 付きの 9 バイト/20 ms のフレーム列(RATET 33)
encoder/wav2ambe in.wav out.bin --rate 26        # AMBE+ のレート: 31、0、22、17、26
decoder/ambe2wav out.ambe out.wav                # RATET 34 のフレーム列 → WAV
decoder/ambe2wav in.ambe out.wav --rate 26       # ほかのレート: 34、33、31、0、22、17、26(--rate33 は --rate 33 と同じ)
decoder/ambe2wav in.bin out.wav --rate 26 --chand  # RATET 26 のチャネルパケット(CHAND / CHAND4、任意のビット数)
```

- [エンコーダ](docs/encoder.md)
- [デコーダ](docs/decoder.md)

## ライセンス

MIT。詳細は [`LICENSE`](LICENSE) を参照してください。

- `decoder/` は blip25-vocoder(MIT、Chance Lindsey)の派生物です([`decoder/LICENSE`](decoder/LICENSE))。
- mbelib 互換の API は、mbelib 1.3(ISC)のインターフェースを再現しています。AMBE+ の 2400 bps の符号帳も mbelib の表を使っています(許諾表示は `decoder/include/mbelib.h`)。
- テスト用の音声は LibriSpeech(CC BY 4.0)の抜粋です。
- AMBE+2 には特許があります(US 8,359,197 ほか)。
