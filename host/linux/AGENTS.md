# Native Linux Agent Memory

## Architecture

- `KissBroker` is the sole owner of the physical KISS serial modem. Native role processes connect through independent Unix sockets under `/tmp/meshcore-kiss`.
- The named RF endpoint (normally `repeater`) is the only role connected to physical RF. Physical RX and hardware configuration are restricted to it.
- Non-RF endpoints share a virtual local medium: their TX succeeds locally and is delivered to every peer, including the repeater, without reaching USB.
- Preserve FIFO physical TX serialization, sender-specific TxDone routing, successful repeater-TX local echo, and role socket continuity across USB disconnects.
- Repeater and room-server PTYs are text consoles. Companion PTYs use the framed binary companion protocol and must remain available alongside companion TCP.
- Stable PTY links default to `/tmp/meshcore-pty`; slaves use mode `0660` and group `dialout`.
- Keep role data directories and persisted identities independent. Do not overwrite a persisted node name on restart.
- Native loops are event-driven through `poll()` with a bounded timeout for MeshCore timers. Avoid high-frequency sleep or busy-poll loops.
- `LinuxRTCClock` derives Unix time from the host system clock and advances with the monotonic clock.

## Compatibility Layer

- `MeshCoreRPI` is a Git submodule backed by `johnterickson/MeshCoreRPI`; the parent repository pins the revision used by native builds.
- Apply Linux compatibility changes directly in that fork, push them, and update the submodule commit here. Do not restore a local patching step.
- `build_repeater.sh` initializes the pinned submodule before compiling.

## Validation

- Focused native transport tests: `pio test -e native_linux_kiss_radio`
- Build every native role and broker: `bash host/linux/build_all.sh`
- Shell-check launcher edits: `bash -n host/linux/run_brokered.sh`
- Run `git diff --check` before finishing.

## Launcher Contracts

- `run_brokered.sh` supports multiple room and companion instances while one broker owns the modem.
- Preserve the environment overrides documented in `README.md`, including KISS socket, PTY, data-root, role-count, and companion-port settings.
- Do not introduce a dependency on `socat`; PTYs are created by `LinuxPty`.