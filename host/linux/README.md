# Native Linux KISS roles

These targets run the existing repeater, room-server, and companion roles as Linux processes using a MeshCore KISS modem.

## Build

```bash
bash host/linux/build_repeater.sh
```

Build every role and the shared-modem broker with:

```bash
bash host/linux/build_all.sh
```

The first build initializes the pinned `johnterickson/MeshCoreRPI` submodule containing the Linux compatibility headers. The executable is written to:

```text
.pio/build/native_linux_kiss_repeater/program
```

## Run

```bash
MESHCORE_DATA_DIR="$HOME/.local/share/meshcore-repeater" \
  .pio/build/native_linux_kiss_repeater/program --device /dev/ttyUSB0
```

The serial link defaults to 115200 baud, 8N1, with no flow control. Use `--baud RATE` only if the modem is configured differently.

The compiled radio defaults match the West Coast Mesh SoCal profile: 927.875 MHz, 62.5 kHz bandwidth, SF7, CR5, and 20 dBm TX power. Existing values in `$MESHCORE_DATA_DIR/prefs.json` take precedence.

The process must have read/write access to the serial device. On distributions that use the `dialout` group:

```bash
sudo usermod -aG dialout "$USER"
```

Log out and back in after changing group membership. `MESHCORE_DATA_DIR` stores the repeater identity, preferences, and packet log; keep it stable between runs.

## Shared-modem broker

Run all three roles behind one modem:

```bash
bash host/linux/run_brokered.sh /dev/ttyUSB0
```

Run multiple room servers and companions by setting instance counts:

```bash
MESHCORE_ROOM_COUNT=2 MESHCORE_COMPANION_COUNT=2 \
  bash host/linux/run_brokered.sh /dev/ttyUSB0
```

The broker exclusively owns the serial device and exposes one Unix-domain KISS endpoint per role instance under `/tmp/meshcore-kiss`. The `repeater` endpoint is the only RF gateway. Room servers and companions share a virtual in-house radio medium:

- A room or companion transmission is delivered to every other local role, including the repeater, and is not written directly to the physical modem.
- Physical RF packets are delivered only to the repeater. Other local roles hear them only when the repeater's normal Mesh forwarding policy retransmits them.
- A successful repeater transmission is written to the physical modem and echoed to every local virtual role.
- KISS radio-configuration commands from virtual roles are kept local; only the repeater can configure the physical modem.

This models rooms and companions as co-located nodes whose only path to the broader mesh is through the repeater. The broker accepts up to 64 named endpoints. `run_brokered.sh` selects `repeater` as the RF endpoint explicitly; manual broker launches can select another endpoint with `--rf-endpoint NAME`.

Role state remains independent:

```text
~/.local/share/meshcore-repeater
~/.local/share/meshcore-room-server
~/.local/share/meshcore-room-server-2
~/.local/share/meshcore-companion
~/.local/share/meshcore-companion-2
```

Companion instances listen on consecutive TCP ports beginning at 5000. For example, two companions use ports 5000 and 5001. Override the first port with `MESHCORE_COMPANION_PORT`; override the socket or data roots with `MESHCORE_KISS_SOCKET_DIR` and `MESHCORE_DATA_ROOT`.

Each role also gets a stable pseudo-terminal under `/tmp/meshcore-pty`:

```text
/tmp/meshcore-pty/repeater
/tmp/meshcore-pty/room-1
/tmp/meshcore-pty/companion-1
```

Repeater and room-server PTYs are text consoles. They accept the firmware CLI commands terminated by carriage return and mirror packet logs and command responses. For example, configure MeshCoreToMQTT to use the repeater without giving it ownership of the physical modem:

```toml
[serial]
ports = ["/tmp/meshcore-pty/repeater"]
```

Companion PTYs carry the binary companion protocol using the same framing as the TCP interface: inbound frames begin with `<` and outbound frames with `>`, followed by a little-endian 16-bit payload length. They are not text consoles. Companion TCP remains available alongside the PTY.

PTY slaves are mode `0660` and use the `dialout` group by default. Override the path root with `MESHCORE_PTY_DIR` or the group with `MESHCORE_PTY_GROUP`. Treat text consoles as privileged interfaces because CLI commands can read or change node configuration, including private keys.

Instances advertise as `LinuxRoom-1`, `LinuxRoom-2`, `LinuxCompanion-1`, and so on. Each instance has its own persisted public/private identity. Room servers keep their upstream default of forwarding disabled. They still serve room clients; use `set repeat on` if a room identity should also repeat transit traffic.