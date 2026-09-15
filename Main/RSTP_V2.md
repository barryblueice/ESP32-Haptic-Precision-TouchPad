# RSTP v2 firmware

## Implemented behavior

USB RSTP advertises firmware 2.0.0, configuration version 2 and capabilities
`0x3ff` (runtime hardware failures can remove existing capabilities). The packet
version remains 1. Configuration reads/writes contain exactly 52 bytes; a
32-byte write is rejected. Existing Feature reports `0x40` and `0x41` remain.

Configuration byte 6 is `sleep | (pointToEdgeMask << 1)`: bit 0 is sleep,
bits 1–4 are top-left, top-right, bottom-left, bottom-right conversion switches.
Bits 5–7 must be zero. Conversion requires both edges (`0x020`) and points
(`0x100`); its own capability is `0x200`. Each point keeps its conversion setting
and step when disabled. A disabled point does not claim input.

The first confident single contact inside an enabled corner immediately emits
the selected action, without a pressure click. The origin and point settings
are fixed for that contact. Moving into another corner does not select it.
The physical radius accepts 1–30% of the short side (default 5%); the existing descriptor
dimensions (1149 × 766 units, logical range 2302 × 1532) normalize the axes.
Portrait swaps both dimensions. Out-of-range raw positions cannot trigger a
corner after coordinate clamping.

Held-point repetition begins at 400 ms and repeats every 100 ms. A delayed task
emits one repeat, with no catch-up burst. When that point permits conversion,
`max(abs(dx)/W, abs(dy)/H) * 100 >= step` ends repetition and starts edge parsing
at the current position. Earlier movement is not replayed. Outside an enabled
edge the contact becomes ordinary movement. Ordinary contacts outside enabled
points retain their previous behavior. Edge continuation still requires movement;
it is not timed auto-repeat.

Lift, additional contacts, invalid contact identity/confidence, connection/mode
changes and pipeline recovery cancel repetition. A quick lift keeps the first
queued action but discards later repeats. Accepted keyboard/Consumer transfers
are followed by a release, including uncertain transfer failures. Queue entries
older than 100 ms expire rather than replaying stale input.

Only PTP mode gains corner gestures on USB, BLE and 2.4 GHz. Mouse mode retains
its previous behavior (including existing USB edges in simulated mouse mode).
The existing BLE mouse build remains the default. Enable `BLE_ENABLE_PTP_MODE`
for BLE PTP and use a compatible host; new gesture support does not automatically
switch a mouse build into PTP.

## Saving and upgrading

- Configure the touchpad over USB. Saved point, edge and rotation settings are
  used by all three PTP transports. Wireless configuration writes are not added.
- NVS `storage/rstp_config` contains an 8-byte `RSCF` header and 52-byte v2 data.
  Valid v1 records are migrated with corner/conversion defaults; a failed
  migration commit leaves the original record available for retry. Unknown or
  corrupt records are retained, with defaults used in memory.
- A committed USB write returns status 7, then restarts after response completion.
  Reconnect and read back to confirm the value. A failed commit returns status 6
  without activating the requested configuration.
- Update the ESP32-S3 touchpad and ESP32-S2 receiver together. The new PTP radio
  path requires receiver direction synchronization before input. An old receiver
  cannot acknowledge it and is not supported for this PTP path.
- The receiver updates the haptic PTP descriptor before acknowledging a new
  portrait/landscape orientation; changing dimensions disconnects/re-enumerates
  USB. The receiver does not rotate already transformed coordinates again.
- BLE initializes its descriptor from the saved orientation. After upgrading or
  changing descriptor dimensions, remove the old host pairing and pair again for
  validation. PTP needs all input subscriptions and an ATT MTU of at least 37;
  undersized packets are not truncated into invalid HID input.

## Radio extension ABI

All extensions retain the existing 38-byte envelope: little-endian uint32 type
at byte 0, 34-byte payload at byte 4, and zero unused tail. Existing types 0–4
retain their layout. The definitions shared by both firmwares are in
`main/SYS/wireless_extension.h`.

| Type | Fields in payload, in order |
| --- | --- |
| 5: action | session uint32, sequence uint32, action uint8, steps int16 |
| 6: surface | extension version uint8 = 1, rotation uint8, nonzero session uint32 |
| 7: surface ACK | same payload as the accepted surface record |

Actions 1–6 use the existing edge action categories and signed direction.
Action 0 with zero steps cancels queued actions. Other steps are nonzero and
bounded to −127…127. Sequences are nonzero and increase within the session.
The receiver rejects duplicate/out-of-order actions, incorrect sessions and
actions from another peer. The receiver generates USB key down/up locally;
wheel reports do not change the receiver's PTP/mouse mode.

The touchpad sends surface information once per second. Without a recent ACK
(2.5 seconds), it pauses PTP input and clears pending actions. A live receiver
session belongs to one transmitter. Existing heartbeats and mode commands remain.

## Verification

Use the **same ESP-IDF setup selected in VS Code**, and the existing `build`
directories. Do not run `fullclean` or change SDK paths to test these changes.
This workspace uses `D:/Espressif/v6.0/esp-idf` with the compiler/Python from
`C:/Espressif/tools`; these paths were checked against both CMake caches.

From Main, with native Windows clang available (not esp-clang):

```powershell
python tests/run_host_tests.py --clang $env:HOST_CLANG
python tests/run_ble_tests.py $env:HOST_CLANG
python tests/run_receiver_tests.py $env:HOST_CLANG
```

The receiver's existing `tools/receiver/host_checks.py` also invokes the expanded
receiver suite and preserves its existing result-file location.

In the VS Code ESP-IDF terminal, run `idf.py build` separately in Main and 2.4G.
For BLE PTP compile/link verification without rebuilding SDK libraries or changing
the default sdkconfig, run `python tests/build_ble_variant.py` after the normal
Main build. It reads the existing compile commands, recompiles seven project
objects, replaces those members in a copy of libmain, and links against the
cached SDK libraries. Its test image is under `build/validation/ble-ptp`.

Automated validation covers 16 core scenarios, 25 receiver scenarios (including
22 existing regressions) and 4 BLE scenarios. These contain the 32 sleep/mask
combinations, 64 start-point/mask combinations, all action/reverse pairs, physical
circle boundaries, rotations, handoff, repetition, cancellation, migration and
commit failure, queued releases, receiver synchronization and BLE backpressure.
The protocol fixture is copied from the revised configurator document: point
conversion mask `0x5` plus sleep gives byte 6 `0x0b`.

### Hardware acceptance still required

No device was flashed or hardware behavior certified by these software checks.
For each transport in PTP mode, verify:

1. USB save/readback/reconnect and power-cycle retention of all four switches.
2. Each corner and circle boundary in all four orientations; disabled corners
   preserve normal clicks/edges and their saved settings return when reenabled.
3. All 12 bindings and reverse options, with no extra ordinary click.
4. 400/100 ms repetition, immediate lift stop, and no continued key or wheel input
   after reconnect, mode changes or input recovery.
5. Per-point conversion threshold and edge takeover, including motion into another
   corner and conversion where no edge is enabled.
6. BLE pairing, full PTP input, all auxiliary subscriptions and congestion recovery;
   receiver direction synchronization and USB re-enumeration.

Primary images: `Main/build/ESP32_HAPTIC_PRECISION_TOUCHPAD.bin` and
`2.4G/build/ESP32-Haptic-2.4G-Receiver.bin`. The BLE PTP validation image is separate
from the default BLE mouse build. Do not interpret successful host tests as
successful hardware acceptance.
