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

### Raspberry Pi で LED を光らせる

```
GPIO17 (11番ピン) ── 330Ω ── LED1(長い足→短い足) ── GND (9番ピン)
GPIO27 (13番ピン) ── 330Ω ── LED2(長い足→短い足) ── GND (14番ピン)
```

```sh
cd receiver
python3 -m robot3ds.led
```

| 3DS の操作 | LED |
|---|---|
| A を押している間 | LED1 点灯 |
| 下画面を触る (左→右) | LED2 の明るさ 0% → 100% |
| スライドパッドを上に倒す | LED2 の明るさ (倒すほど明るい) |
| 通信が 0.5 秒途切れる | 両方消灯 |

ピンは `--led-pin` / `--pwm-pin` で変更できます。

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
