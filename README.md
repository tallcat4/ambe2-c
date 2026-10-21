# ambe2-c

AMBE-3000R(ThumbDV など)とビット互換のフレームを扱う、AMBE 系ボコーダのエンコーダとデコーダです。C99 だけで書いています。

- **AMBE+2 半レート(2450 bps)**: エンコーダとデコーダの両方があります。
- **AMBE+(AMBE-2000 系)の 2000 / 2400 / 4000 bps と、その FEC 付きのレート**: デコーダがあります。

## 対応している形式

| 形式 | AMBE-3000R のレート(RATET) | フレーム | エンコーダ | デコーダ | 品質 |
|---|---|---|---|---|---|
| AMBE+2 2450 bps(FEC なし) | 34 | 49 ビット | ○ | ○ | エンコーダ: チップと ABX で聞き分け不可(18/30、p = 0.18)<br>デコーダ: blip25-vocoder とビット一致、チップとの差 0.77 dB |
| AMBE+2 3600x2450 bps(P25 Phase 2 半レート、DMR、dPMR) | 33 | 72 ビット | ○ | ○ 硬判定・軟判定 | エンコーダ: FEC はチップの出力とビット一致<br>デコーダ: 同上 |
| AMBE+ 2000 bps | 31 | 40 ビット | − | ○ | チップとの差 1.19 dB |
| AMBE+ 2400 bps(D-STAR と同じ音声ビット) | 0 | 48 ビット | − | ○ | チップとの差 1.24 dB |
| AMBE+ 2400 bps + FEC 1600 | 22 | 80 ビット | − | ○ 硬判定・軟判定 | チップとの差 1.24 dB |
| AMBE+ 4000 bps | 17 | 80 ビット | − | ○ | チップとの差 2.03 dB |
| AMBE+ 4000 bps + FEC 2400 | 26 | 128 ビット | − | ○ 硬判定・軟判定 | チップとの差 2.03 dB、デコーダ: チップと ABX で聞き分け不可(17/30、p = 0.29) |

- 「チップとの差」は、同じビット列をチップ自身が復号した音声との mel スペクトル距離です(日本語と英語の平均)。
- ABX は日本語の音声で行いました。
- 対応していないもの:
  - IMBE(P25 Phase 1)
  - D-STAR の FEC 付きフレーム(72 ビット)の直接の復号。FEC を外した 48 ビットは、RATET 0 として復号できます。
  - AMBE+ のトーンや無音のフレーム

## そのほかの機能

- **マイコンで動く**: AMBE+2 のエンコードとデコードを同時に動かして、Portenta H7 で CPU の 12%、ESP32-WROOM-32 で 1 コアの 37% です。
- **固定小数点版**: エンコーダには整数演算だけの版もあり、どの CPU でも同じビット列を出します。
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
decoder/ambe2wav out.ambe out.wav                # RATET 34 のフレーム列 → WAV
decoder/ambe2wav in.ambe out.wav --rate 26       # ほかのレート: 34、33、31、0、22、17、26(--rate33 は --rate 33 と同じ)
```

詳しくはマニュアルを参照してください。

- [エンコーダ](docs/encoder.md): C の API、FEC 付きの出力、マイコンでの実測、無音フレームの仕様
- [デコーダ](docs/decoder.md): AMBE+2 のデコーダと、mbelib からの移行
- [AMBE+ デコーダ](docs/ambeplus.md): AMBE+ のレートの形式と、チップから割り出したしくみ

## ライセンス

MIT。詳細は [`LICENSE`](LICENSE) を参照してください。

- `decoder/` は blip25-vocoder(MIT、Chance Lindsey)の派生物です([`decoder/LICENSE`](decoder/LICENSE))。
- mbelib 互換の API は、mbelib 1.3(ISC)のインターフェースを再現しています。AMBE+ の 2400 bps の符号帳も mbelib の表を使っています(許諾表示は `decoder/include/mbelib.h`)。
- テスト用の音声は LibriSpeech(CC BY 4.0)の抜粋です。
- AMBE+2 には特許があります(US 8,359,197 ほか)。
