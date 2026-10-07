#!/usr/bin/env bash
# Metro Chime (3DS 版) をビルドする。
#   - devkitPro がインストール済み (DEVKITARM が設定済み) ならそれを使う
#   - なければ Docker で devkitPro 入りのイメージを取得してビルドする
# 引数はそのまま make に渡す (例: ./build.sh clean)
set -euo pipefail

# .devcontainer/devcontainer.json と .github/workflows/build.yml も同じイメージを使う
IMAGE="${DEVKITARM_IMAGE:-devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5}"

cd "$(dirname "$0")"

if [ -n "${DEVKITARM:-}" ]; then
	exec make "$@"
fi

if ! command -v docker >/dev/null 2>&1; then
	echo "devkitPro も Docker も見つかりません。" >&2
	echo "https://devkitpro.org/wiki/Getting_Started で devkitPro を入れるか、Docker を入れてください。" >&2
	exit 1
fi

exec docker run --rm \
	-v "$PWD":/src -w /src \
	-u "$(id -u):$(id -g)" \
	"$IMAGE" make "$@"
