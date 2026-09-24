#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compat_dir="$repo_root/host/linux/MeshCoreRPI"

git -C "$repo_root" submodule update --init --recursive host/linux/MeshCoreRPI
if [[ ! -f "$compat_dir/host/rpi_native_cli/include/Arduino.h" ]]; then
  echo "MeshCoreRPI compatibility headers are missing" >&2
  exit 1
fi

cd "$repo_root"
pio run -e native_linux_kiss_repeater