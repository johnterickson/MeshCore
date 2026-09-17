#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compat_dir="$repo_root/.pio/meshcore-rpi-compat"
compat_commit="5f7def6f14fbd0562a81d71c18d93739b65e029c"
compat_patch="$repo_root/host/linux/meshcorerpi-stream.patch"

if [[ ! -d "$compat_dir/.git" ]]; then
  git clone --filter=blob:none --no-checkout https://github.com/dabeani/MeshCoreRPI.git "$compat_dir"
  git -C "$compat_dir" sparse-checkout set host/rpi_native_cli/include
  git -C "$compat_dir" checkout "$compat_commit"
fi

actual_commit="$(git -C "$compat_dir" rev-parse HEAD)"
if [[ "$actual_commit" != "$compat_commit" ]]; then
  echo "Unexpected compatibility checkout: $actual_commit" >&2
  exit 1
fi

apply_compat_patch() {
  (cd "$compat_dir" && { cat "$compat_patch"; printf '\n'; } | git apply "$@" -)
}

if apply_compat_patch --reverse --check >/dev/null 2>&1; then
  :
elif apply_compat_patch --check; then
  apply_compat_patch
else
  echo "Compatibility patch does not apply" >&2
  exit 1
fi

cd "$repo_root"
pio run -e native_linux_kiss_repeater