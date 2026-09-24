# Linux KISS RF Hardware Validation

This document records the hardware tests used to validate Linux KISS SNR reporting and radio gain/TX configuration. It distinguishes measured RF behavior from source-level or build-only validation.

## Test setup

- KISS modem: Heltec V4 OLED (GC1109 FEM), ESP32-S3
- Linux host: Raspberry Pi running `meshcore-brokered.service`
- Remote companion: Heltec V3 companion radio
- Approximate separation: 100 ft
- Radio profile: 927.875 MHz, 62.5 kHz bandwidth, SF7, CR5
- Linux topology: one RF repeater endpoint, two virtual rooms, and two virtual companions
- Explicit round-trip trace path: `7f,36,7f`; the final `7f` is the return hop to the companion

The remote companion was configured for TX 22. The Heltec V4 SX1262 drive was set to 10, which the board configuration documents as approximately 22 dBm after its external PA.

## SNR scaling defect

### Wire format

MeshCore stores and transports SNR as a signed byte in quarter-dB units:

- `+12.0 dB` is encoded as `48`.
- `-2.0 dB` is encoded as `-8`.
- RSSI is already a signed whole-dBm value and is not scaled.

The KISS modem correctly encoded `radio_driver.getLastSNR() * 4`. The Linux `KissRadio` adapter originally treated that signed byte as whole dB. `Dispatcher` then encoded it into quarter-dB again. Values near +12 dB therefore overflowed an `int8_t` and appeared near -16 dB in trace results.

The conversion is now centralized on `mesh::Packet`:

```cpp
Packet::snrToDb(int8_t quarter_db)
Packet::snrFromDb(float db)
```

Packet/protocol storage uses `snrFromDb()`. Calculations and display code use float dB. KISS decoding and direct Linux radio register decoding use `snrToDb()`.

### Measurements before the fix

Six controlled traces through the physical companion produced:

- Companion to KISS repeater: median -17.0 dB, range -19.0 to -15.0 dB
- KISS repeater to companion: median +11.75 dB, range +11.5 to +12.25 dB
- Success rate: 5/6

A repeater-only run removed both virtual rooms and both virtual companions:

- Companion to KISS repeater: median -15.5 dB, range -17.0 to -14.0 dB
- KISS repeater to companion: median +12.0 dB, range +11.5 to +12.5 dB
- Success rate: 10/10

The small change was normal RF variation. Virtual-node traffic was not responsible for the apparent directional SNR problem.

### Measurements after the fix

Six equivalent traces produced:

- Companion to KISS repeater: median +11.75 dB, range +11.5 to +12.5 dB
- KISS repeater to companion: median +11.62 dB, range +11.5 to +12.25 dB
- Success rate: 6/6

After moving all conversions to the shared `Packet` helpers, three additional traces produced:

| Trace | Companion to repeater | Repeater to companion |
| --- | ---: | ---: |
| 1 | +12.25 dB | +11.50 dB |
| 2 | +11.75 dB | +11.75 dB |
| 3 | +12.00 dB | +12.00 dB |

The radio link was operating symmetrically. The directional problem was reporting and integer overflow, not degraded RF reception.

## RX gain and FEM behavior

Direct KISS hardware queries reported:

- Device: Heltec V4 OLED
- KISS protocol version: 2.0
- RX boosted gain: enabled
- FEM RX software flag: disabled
- FEM TX software flag: disabled

The Heltec V4 OLED identified here uses the GC1109 FEM. On this revision, receive and transmit modes are selected automatically by the board hooks rather than an independently controllable LNA flag:

- `onBeforeTransmit()` selects the full PA transmit path.
- `onAfterTransmit()` restores receive mode.
- The FEM RX preference being reported as off does not mean the GC1109 receive path is powered off.

The SX1262 build also enables:

- `SX126X_RX_BOOSTED_GAIN=1`
- register patch `0x8B5 |= 0x01`

A temporary gain-off test was restored immediately afterward. It did not explain the original 4x SNR discrepancy.

## Noise floor reporting

The Linux adapter initially displayed `-120 dBm`, but a direct modem query reported approximately `-95 dBm`.

`-120 dBm` is the Linux-side initial/clamped value, not proof that RX boosted gain is disabled. Noise-floor telemetry should not be used to diagnose the FEM until the Linux host actively queries and refreshes the modem value.

## Receive errors

One direct modem query reported cumulative counters of:

- RX: 1340
- TX: 680
- receive errors: 662

The modem increments receive errors when RadioLib `readData()` fails, such as CRC or header failures. Linux's local packet counters did not originally expose this modem-side cumulative value. These counters were useful diagnostics but did not explain the trace asymmetry, which was resolved by correcting SNR units.

## TX power behavior

The imported repeater preference requested TX 28. SX1262 RadioLib accepts only -9 through 22 and does not automatically clamp this call because the wrapper passes no clipping output pointer. It returns `RADIOLIB_ERR_INVALID_OUTPUT_POWER` and leaves the prior hardware value unchanged.

The KISS callback previously ignored that return value while caching and acknowledging the requested value. The result was misleading:

- Reported KISS preference: 28
- Effective radio value: prior initialized value

The repeater preference was changed to 10. Direct KISS queries after broker configuration confirmed TX 10 in modem RAM.

Configured power is not a conducted-power measurement. External PA gain, supply voltage, antenna/feed-line loss, thermal state, and regulatory limits remain physical test variables.

## KISS reboot and configuration replay

KISS supports `HW_CMD_REBOOT` (`0x18`). It was tested directly and returned `HW_RESP_OK` before reboot.

State before reboot:

- Radio: 927.875 MHz, 62.5 kHz, SF7, CR5
- TX: 10
- RX boosted gain: enabled

Fresh boot before a host connected:

- KISS radio cache: zero
- KISS TX cache: zero
- RX boosted gain: enabled from compile-time defaults
- modem statistics: zeroed

After `meshcore-brokered.service` started, direct queries confirmed that the broker/repeater reapplied:

- 927.875 MHz, 62.5 kHz, SF7, CR5
- TX 10
- RX boosted gain enabled

Host-provided radio and TX settings are RAM-only on the modem. They are not loaded from persistent KISS preferences. The Linux repeater sends them at startup, and the broker remembers and replays physical configuration after a USB reconnect.

## AGC reset experiment

Hardware repeater preference `agc.reset.interval=0` disables the repeater role's `Dispatcher` reset timer. The KISS modem has a separate modem-side full SX1262 reset/calibration loop every 30 seconds.

A temporary KISS build disabled the modem-side reset. With the host-configured radio profile present, the modem received zero packets during the observation window. The experiment was reverted and normal firmware restored. This falsified the hypothesis that repeated modem-side AGC resets caused the apparent -16 dB trace values.

The exact RadioLib state that requires recovery after host-side configuration was not isolated. The normal 30-second reset remains enabled.

## Validation commands

Focused Linux KISS tests:

```bash
pio test -e native_linux_kiss_radio
```

Result: 14/14 passed.

Embedded Heltec V4 KISS build:

```bash
pio run -e heltec_v4_kiss_modem
```

Native Linux roles and broker:

```bash
bash host/linux/build_all.sh
```

All native targets built successfully:

- `native_linux_kiss_repeater`
- `native_linux_kiss_room_server`
- `native_linux_kiss_companion`
- `native_linux_kiss_broker`

Runtime validation used framed companion-protocol trace requests through a physical companion radio and confirmed approximately +12 dB in both directions after the fix.

## Same-device firmware matrix

The same Heltec V4 was tested on 2026-09-24 with five source/runtime combinations:

| Scenario | Source state | Runtime | Firmware SHA-256 |
| --- | --- | --- | --- |
| Baseline | `d92964352441e53b93e8667b802e04f6e072b39e` | Hardware repeater | `c03c669bc8ae29351e6054f907df745b60b4e6246a8b7903de7746ac200119fd` |
| A | `37ad672a5ed6696d095ca6c240a5a73016f46acc` | Hardware repeater | `3a69f99f632d8a9b05298000715e6b67ec2bd20c2b687babff7825a97e912703` |
| B | `37ad672a5ed6696d095ca6c240a5a73016f46acc` | KISS modem and Linux repeater | `776ecbe1bbd004ddb11a8e142fc8e447808eadc644e4e51038e9a2c863243e74` |
| C | A with `09e4f64c9c52cf0e056ca5c39fe0b1e479c13324` reverted | Hardware repeater | `3580f2b9b32176c468b31e4bbc34d3a497d9ae91d65ccdc6b77d3b1067a18f02` |
| D | B with `09e4f64c9c52cf0e056ca5c39fe0b1e479c13324` reverted | KISS modem and Linux repeater | `1740259b13038628c571404d6542d555ab96d2f190991b7a3e3c7a0edc98a300` |

C and D were built in an isolated worktree by applying `git revert --no-commit` to `09e4f64`. All 20 paths touched by that commit matched its parent after the inverse was applied.

Each scenario used the same persisted radio profile and TX setting. Discovery had a full 20-second response window. Every neighbor with discovery SNR strictly greater than -3 dB received five trace attempts. A trace through this repeater used the explicit round-trip path `7f,N,7f`, including the return hop.

Discovery results use the signed quarter-dB value in the final `neighbors` field:

| Scenario | Qualifying neighbors | Excluded neighbors |
| --- | --- | --- |
| Baseline | `CA0C12D6` +2.25 dB; `368B0DB0` +2.00 dB | None |
| A | `CA0C12D6` +0.75 dB | `1A92096C` -9.25 dB |
| B | `CA0C12D6` +1.75 dB | None |
| C | `368B0DB0` +6.00 dB | `1A92096C` -8.50 dB |
| D | `CA0C12D6` +1.50 dB; `368B0DB0` +6.75 dB | `1A92096C` -8.75 dB |

Trace SNR positions for `7f,N,7f` are companion-to-`7f`, `7f`-to-`N`, `N`-to-`7f`, and final `7f`-to-companion:

| Scenario | Neighbor and path | Success | Median SNRs | Ranges |
| --- | --- | ---: | --- | --- |
| Baseline | `CA0C12D6`, `7f,ca,7f` | 5/5 | +12.00, +5.75, +2.25, +12.25 dB | +11.75..+12.50, +5.50..+6.00, -0.25..+4.25, +12.00..+12.25 dB |
| Baseline | `368B0DB0`, `7f,36,7f` | 4/5 | +11.88, -3.25, +7.38, +12.00 dB | +11.75..+12.25, -4.25..-0.25, +4.75..+7.75, +11.75..+12.50 dB |
| A | `CA0C12D6`, `7f,ca,7f` | 5/5 | +12.00, +5.25, +3.75, +11.50 dB | +11.75..+12.50, +3.25..+6.00, -2.50..+4.50, +11.50..+12.00 dB |
| B | `CA0C12D6`, `7f,ca,7f` | 4/5 | +12.25, +5.25, -0.25, +12.00 dB | +12.25..+12.50, +5.00..+5.50, -2.75..+1.50, +11.50..+12.50 dB |
| C | `368B0DB0`, `7f,36,7f` | 1/5 | +12.25, -2.25, +8.00, +12.75 dB | Single successful trace |
| D | `CA0C12D6`, `7f,ca,7f` | 4/5 | +12.25, +5.38, +1.62, +11.50 dB | +12.00..+12.50, +5.00..+5.50, +0.50..+2.50, +11.25..+11.75 dB |
| D | `368B0DB0`, `7f,36,7f` | 5/5 | +12.00, -3.25, +6.50, +11.50 dB | +11.50..+12.25, -4.00..-2.00, +5.75..+7.50, +11.25..+11.75 dB |

The common `CA0C12D6` forward-link median remained close across Baseline, A, B, and D. The weaker `368B0DB0` link had materially different response rates between runs, so discovery SNR and one short trace batch are not sufficient to attribute that variation to firmware.

Commit `09e4f64` was intentionally present in A/B and reverted in C/D. In B, both the KISS default and the persisted Linux repeater preference requested FEM RX on. This Heltec V4 uses the GC1109, however, for which `canControlLoRaFemLna()` is false: RX mode is selected automatically, `setLoRaFemLnaEnabled(true)` returns false, and readback remains off because there is no independently controllable software LNA switch. D's reverted Linux/KISS stack did not support querying FEM state at all. The matrix therefore compares the requested source states, including the `09e4f64` protocol and application paths, but this particular board cannot produce a physical FEM-LNA-on versus FEM-LNA-off A/B. A Heltec V4.3 with the controllable KCT8103L FEM would be required for that isolation.

After the matrix, the `37ad672` KISS image was restored. `meshcore-brokered.service` was active with ports 5000 and 5001 listening, `mctomqtt.service` remained inactive and disabled, and the host firewall INPUT policy was `ACCEPT`. No claim is made here about conducted power, spectral quality, or regulatory compliance.
