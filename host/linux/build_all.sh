#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

cd "$repo_root"
bash host/linux/build_repeater.sh
pio run \
  -e native_linux_kiss_room_server \
  -e native_linux_kiss_companion \
  -e native_linux_kiss_broker

echo "Built native Linux KISS broker, repeater, room server, and companion."