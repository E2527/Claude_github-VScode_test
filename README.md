# 3DS Robot Controller

ニンテンドー3DS をロボット操作用のコントローラーにするプロジェクトです。

```
┌──────────┐   Wi-Fi / UDP 30Hz   ┌───────────────────┐  USB シリアル  ┌──────────┐
│   3DS    │ ───────────────────▶ │ Raspberry Pi / PC │ ─────────────▶ │ Arduino  │──▶ モーター
│ homebrew │   ボタン・スティック   │  receiver (Python)│  "M <L> <R>"   │          │
└──────────┘   タッチ・ジャイロ等   └───────────────────┘                └──────────┘
```

| ディレクトリ | 内容 |
|---|---|
| `3ds/` | 3DS 用 homebrew アプリ (C / libctru)。入力を UDP で送信 |
| `receiver/` | ロボット側の受信プログラム (Python 3.9+)。走行指令へ変換 |
| `arduino/motor_driver/` | シリアル指令でモーターを回す Arduino スケッチ |

## 1. 3DS アプリのビルドとインストール

前提: 3DS に homebrew 環境 (Luma3DS + Homebrew Launcher) が導入済みであること。

1. ビルド (次のどれか)
   - **`./3ds/build.sh`** — devkitPro がインストール済みならそれを、なければ Docker で devkitPro 入りのイメージを自動で取得してビルドします
   - **VS Code** — 「Reopen in Container」で開くと devkitPro 入りの環境になるので、ターミナルで `make -C 3ds`
   - **GitHub Actions** — push すると自動でビルドされ、Actions の Artifacts から `robot-controller.3dsx` をダウンロードできます
   - 自分で [devkitPro](https://devkitpro.org/wiki/Getting_Started) を入れる場合は `sudo dkp-pacman -S 3ds-dev` のあと `make -C 3ds`

   `3ds/robot-controller.3dsx` ができます。
2. SD カードの `/3ds/` にコピーし、Homebrew Launcher から起動

> devkitPro のバージョンは `3ds/build.sh`・`.devcontainer/devcontainer.json`・`.github/workflows/build.yml` の
> `devkitpro/devkitarm@sha256:...` で固定しています。更新するときは 3 か所とも同じ値に書き換えてください。

### 操作

| ボタン | 動作 |
|---|---|
| SELECT | 送信先 (ロボット) の IP アドレスを変更 (SD の `/3ds/robot-controller/config.txt` に保存) |
| START | 終了 (ロボットへ停止パケットを送ってから終了) |
| その他すべて | そのままロボットへ送信 |

## 2. ロボット側 (受信)

```sh
cd receiver
python -m robot3ds                  # 受信して左右の車輪指令を表示
```

3DS が手元になくても、疑似送信ツールで試せます:

```sh
python -m robot3ds.fake_sender 127.0.0.1
```

### デフォルトの操作割り当て (差動二輪)

| 入力 | 動作 |
|---|---|
| **R を押している間だけ動く** | デッドマンスイッチ (`--deadman none` で無効化) |
| スライドパッド / 十字キー | 前後進・旋回 |
| L | ターボ (最高速度) |

### モーターを動かす

1. `arduino/motor_driver/motor_driver.ino` を Arduino に書き込む (ピン番号は配線に合わせて変更)
2. Raspberry Pi などと USB 接続して:
   ```sh
   pip install -r requirements.txt
   python -m robot3ds --serial /dev/ttyUSB0
   ```

### 自分のプログラムから使う

```python
from robot3ds import Button, ControllerReceiver, DriveConfig, to_drive_command

rx = ControllerReceiver(port=4950)
while True:
    state = rx.poll()              # 0.5 秒途絶えると None
    if state and state.pressed(Button.A):
        print("A!")
    cmd = to_drive_command(state, DriveConfig())
    # cmd.left / cmd.right (-1.0..1.0) をモーターへ
```

`state` からは全ボタン、スライドパッド、Cスティック (New 3DS)、タッチ座標、加速度、ジャイロが取れるので、アーム操作や ROS 2 (`geometry_msgs/Twist` への変換) などにも応用できます。

## 安全機能

- **デッドマンスイッチ**: R を離すと即停止
- **通信途絶で停止**: 受信側は 0.5 秒パケットが来なければ停止指令を出す
- **終了時停止**: 3DS アプリは START で終了する際に停止パケットを送る
- **Arduino 側タイムアウト**: 0.5 秒シリアル指令が来なければモーター停止

## 通信プロトコル

UDP、ポート 4950、1 パケット 40 バイト (little endian)。定義は `3ds/source/protocol.h` と `receiver/robot3ds/protocol.py`。

| offset | 型 | 内容 |
|---|---|---|
| 0 | char[4] | `"R3DS"` |
| 4 | u8 | version (1) |
| 5 | u8 | flags (bit0: 停止要求, bit1: タッチ中, bit2: New 3DS) |
| 6 | u16 | 予約 |
| 8 | u32 | シーケンス番号 |
| 12 | u32 | ボタン (libctru の `KEY_*` ビット) |
| 16 | s16×2 | スライドパッド x, y (約 ±156) |
| 20 | s16×2 | Cスティック x, y |
| 24 | u16×2 | タッチ x (0-319), y (0-239) |
| 28 | s16×3 | 加速度 x, y, z |
| 34 | s16×3 | ジャイロ x, y, z |

## テスト

```sh
cd receiver
python -m unittest discover -s tests
```

---

# Metro Chime (iPhone 時報アプリ)

`metro-chime/` は、日付・曜日・時刻を夜の街のモーショングラフィックとエレクトロニカで知らせる iPhone 向け Web アプリです。
音楽は Web Audio API でその場で合成しているので、音声ファイルはありません (122 BPM、Am9–Fmaj9–Cmaj7–Em7)。

- **起動アニメーション**: 信号ラインとカラーバンドが走り、タイトルが飛び込み、駅の発車標風のパタパタ表示で日付と時刻が確定してしたあと「TAP TO START」で待機。タップすると時計画面に切り替わり、カウントダウンなしで即座に音楽付きの時報が始まる (「時計だけ表示」で音なしで開く)。iPhone は最初のタップまで音を出せないため、この 1 タップを挟んでいる
- **背景シーン**: 夜景 / 雨 / トンネル / 路線図 の 4 種類を 2 分ごとに切り替え。上部の `01/04 SKYLINE` をタップすると手動で切り替え。切り替えはつながりのある演出になっている
  - 夜景 → 雨: 街の上に雲が流れ込み、稲光のあと雨が降り出して街の灯りがにじむ
  - 雨 → トンネル: 雨の中からトンネルの入口 (緑の案内標識付き) が近づき、そのまま中へ
  - トンネル → 路線図: 対向のヘッドライトが迫ってフラッシュ、電車が入ってきて停車し、カメラが引いて路線図上の光る点になる
  - 路線図 → 夜景: 駅にズームして「EXIT」の案内が出て、駅の丸が開いて地上の夜景へ
- **横画面**: iPhone を横にすると、左に大きな時計・右に曜日と日付を並べた横向きレイアウトに切り替わる。回転のたびに、枠が回り込んで新しい向きに収まり、スラットが開いて画面要素が順番にスライドインする演出が入る。起動アニメと時報も横画面に対応
- **時報を再生**: 4 拍のカウントのあと、曜日・日付・年・時刻をビートに合わせて表示し、最後に日本語で読み上げ
- **BGM**: 音楽を流しっぱなしにする (再生中は画面スリープを抑止)
- **自動時報**: 毎時 / 15分毎 / OFF。アプリを開いていて、一度タップして音を有効にした後に動作

### iPhone に入れる (ホーム画面アプリ)

一度だけネットからページを開けば、あとは iPhone の中だけで動きます (フォント・音楽・読み上げ・背景すべて端末内。サーバーとの通信なし)。

1. **公開 (最初の 1 回だけ)**: GitHub のリポジトリで Settings → Pages → Build and deployment を
   「Deploy from a branch」、ブランチ `claude/practical-goodall-jvs4hz`、フォルダ `/ (root)` にして Save
2. 数分後、iPhone の **Safari** で https://e2527.github.io/Claude_github-VScode_test/metro-chime/ を開く
3. 共有ボタン → **ホーム画面に追加**
4. ホーム画面の「Metro Chime」アイコンから起動。この時点でアプリ一式が iPhone に保存され、機内モードでも動く

更新したときは、ネットにつながった状態で 1 回起動すると裏で新しい版を取り込み、次の起動から反映されます。
ローカルの PC で試す場合は `npx http-server metro-chime` で起動し、`http://localhost:8080/` を開いてください。

## Metro Chime 3DS 版

`metro-chime-3ds/` はニンテンドー 3DS 用の homebrew 版です (C / libctru / citro2d)。Web 版と同じ曲・同じ 4 つの背景シーンとつながりのある切り替え演出を、3DS の中だけで動かします。

- **上画面**: 背景シーン・時計・時報カード・起動アニメ。**3D スライダーを上げると街並みやトンネルに奥行きが出て、時計が手前に浮きます**
- **下画面**: 週・日数・令和・経過% と、タッチで押せるボタン
- **起動**: 起動アニメのあと、そのまま時報が始まる (3DS は最初から音を出せるのでタップ不要)。B で音なしの時計だけ表示
- **時報の締め**: 3DS には読み上げ音声がないので、最後は「ピッ・ピッ・ピッ・ポーン」の時報音で知らせる
- **縦持ち**: 3DS を本のように横向きに持つと、加速度センサーで向きを検知して上下画面とも縦長レイアウトに切り替わる (時と分を 2 段に積んだ大きな時計)。回転のたびに枠が回り込んでスラットが開く演出が入る。縦持ち中は 3D 表示はオフ
- 時刻は 3DS 本体の時計を使います

| ボタン | 動作 |
|---|---|
| A / タッチ「時報を再生」 | 時報 (カウントダウン付き) |
| X | BGM の ON/OFF |
| Y | 自動時報 (毎時 / 15 分毎 / OFF) |
| R / L | 背景シーンを次へ |
| SELECT | 画面の向き: 自動 → 横固定 → 縦 (上画面が左) → 縦 (上画面が右) |
| START | 終了 |

### ビルドとインストール

1. ビルド: `./metro-chime-3ds/build.sh` (devkitPro がなければ Docker で自動取得)。GitHub Actions の Artifacts `metro-chime-3dsx` からもダウンロードできます
2. `metro-chime-3ds/metro-chime.3dsx` を SD カードの `/3ds/` にコピーし、Homebrew Launcher から起動
3. 音を出すには SD カードに `/3ds/dspfirm.cdc` が必要です (Luma3DS 環境で homebrew の **DSP1** を一度実行すると作られます)。ない場合は音なしで動きます

