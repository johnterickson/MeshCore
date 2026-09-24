#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
device="${1:-/dev/ttyUSB0}"
companion_port="${MESHCORE_COMPANION_PORT:-5000}"
room_count="${MESHCORE_ROOM_COUNT:-1}"
companion_count="${MESHCORE_COMPANION_COUNT:-1}"
socket_dir="${MESHCORE_KISS_SOCKET_DIR:-/tmp/meshcore-kiss}"
pty_dir="${MESHCORE_PTY_DIR:-/tmp/meshcore-pty}"
pty_group="${MESHCORE_PTY_GROUP:-dialout}"
data_root="${MESHCORE_DATA_ROOT:-$HOME/.local/share}"
pids=()

if [[ ! "$room_count" =~ ^[0-9]+$ || ! "$companion_count" =~ ^[0-9]+$ ||
      $((room_count + companion_count + 1)) -gt 64 ]]; then
  echo "Room and companion counts must be non-negative integers with at most 64 total endpoints" >&2
  exit 2
fi
if ((companion_port < 1 || companion_port + companion_count - 1 > 65535)); then
  echo "Companion TCP port range is invalid" >&2
  exit 2
fi

cleanup() {
  trap - INT TERM EXIT
  if ((${#pids[@]} > 0)); then
    kill "${pids[@]}" 2>/dev/null || true
    wait "${pids[@]}" 2>/dev/null || true
  fi
}
trap cleanup INT TERM EXIT

cd "$repo_root"
endpoints=(repeater)
broker_args=(--device "$device" --socket-dir "$socket_dir" --rf-endpoint repeater --endpoint repeater)
for ((instance = 1; instance <= room_count; instance++)); do
  endpoints+=("room-$instance")
  broker_args+=(--endpoint "room-$instance")
done
for ((instance = 1; instance <= companion_count; instance++)); do
  endpoints+=("companion-$instance")
  broker_args+=(--endpoint "companion-$instance")
done

mkdir -p "$socket_dir"
rm -f "$socket_dir"/*.sock
mkdir -p "$pty_dir"
rm -f "$pty_dir"/repeater "$pty_dir"/room-* "$pty_dir"/companion-*
.pio/build/native_linux_kiss_broker/program "${broker_args[@]}" &
pids+=("$!")

for endpoint in "${endpoints[@]}"; do
  for ((attempt = 0; attempt < 100; attempt++)); do
    if [[ -S "$socket_dir/$endpoint.sock" ]]; then break; fi
    sleep 0.02
  done
  if [[ ! -S "$socket_dir/$endpoint.sock" ]]; then
    echo "Broker endpoint did not appear: $socket_dir/$endpoint.sock" >&2
    exit 1
  fi
done

MESHCORE_DATA_DIR="$data_root/meshcore-repeater" \
  .pio/build/native_linux_kiss_repeater/program \
  --device "unix:$socket_dir/repeater.sock" \
  --pty "$pty_dir/repeater" --pty-group "$pty_group" &
pids+=("$!")

for ((instance = 1; instance <= room_count; instance++)); do
  room_data="$data_root/meshcore-room-server"
  if ((instance > 1)); then room_data="${room_data}-$instance"; fi
  room_args=(--device "unix:$socket_dir/room-$instance.sock"
             --pty "$pty_dir/room-$instance" --pty-group "$pty_group")
  if [[ ! -f "$room_data/prefs.json" ]]; then
    room_args+=(--name "LinuxRoom-$instance")
  fi
  MESHCORE_DATA_DIR="$room_data" \
    .pio/build/native_linux_kiss_room_server/program \
    "${room_args[@]}" &
  pids+=("$!")
done

for ((instance = 1; instance <= companion_count; instance++)); do
  companion_data="$data_root/meshcore-companion"
  if ((instance > 1)); then companion_data="${companion_data}-$instance"; fi
  instance_port=$((companion_port + instance - 1))
  MESHCORE_DATA_DIR="$companion_data" \
    .pio/build/native_linux_kiss_companion/program \
    --device "unix:$socket_dir/companion-$instance.sock" --port "$instance_port" \
    --name "LinuxCompanion-$instance" \
    --pty "$pty_dir/companion-$instance" --pty-group "$pty_group" &
  pids+=("$!")
done

echo "Brokered MeshCore stack running with $room_count room(s) and $companion_count companion(s)"
echo "Role PTYs: $pty_dir"
if ((companion_count > 0)); then
  echo "Companion TCP ports: $companion_port-$((companion_port + companion_count - 1))"
fi
if wait -n "${pids[@]}"; then
  exit_status=1
else
  exit_status=$?
fi
echo "A MeshCore process exited; stopping the brokered stack" >&2
exit "$exit_status"