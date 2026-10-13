#!/usr/bin/env bash
# Cross-compile the host collector for Windows / macOS / Linux.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="${HOST_DIST:-$ROOT/../dist/host}"
mkdir -p "$OUT"

targets=(
  darwin/amd64
  darwin/arm64
  linux/amd64
  linux/arm64
  windows/amd64
  windows/arm64
)

for t in "${targets[@]}"; do
  os="${t%/*}"
  arch="${t#*/}"
  ext=""
  if [[ "$os" == windows ]]; then
    ext=".exe"
  fi
  dest="$OUT/rainmonitor-${os}-${arch}${ext}"
  echo "building $dest"
  # macOS BLE needs CGO/CoreBluetooth. Windows BLE is WinRT and cross-compiles
  # with CGO off. Linux BLE still needs a native BlueZ build.
  cgo=0
  tags=""
  host_os="$(uname -s)"
  host_arch="$(go env GOARCH)"
  if [[ "$os" == darwin && "$host_os" == Darwin && "$arch" == "$host_arch" ]]; then
    cgo=1
  elif [[ "$os" != windows ]]; then
    tags="-tags=nobile"
  fi
  CGO_ENABLED="$cgo" GOOS="$os" GOARCH="$arch" go build -C "$ROOT" -trimpath -ldflags="-s -w" ${tags:+$tags} -o "$dest" .
done

echo "$OUT"
