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

Corner actions have no direction-reversal option: select the desired direction
directly from the 12 bindings. Each corner record remains 5 bytes
(`enabled, action, reserved, radius, step`); the former reversal byte is reserved
and must be zero. Nonzero reserved bytes are rejected. Edge reversal is unchanged.

The first confident single contact inside an enabled corner immediately emits
the selected action, without a pressure click. The origin and point settings
are fixed for that contact. Moving into another corner does not select it.
Initial untrusted samples wait for the same contact's first trusted, in-bounds
position. A trusted origin outside the points stays ordinary input for that
contact. Identity uses `contact_id`, not the array slot. Multiple contacts or an
identity change while waiting stop selection until lift; confidence loss after
claiming a point cancels it until lift.
The physical radius accepts 1–30% of the short side (default 5%); the existing descriptor
dimensions (1149 × 766 units, logical range 2302 × 1532) normalize the axes.
Portrait swaps both dimensions. Out-of-range raw positions cannot trigger a
corner after coordinate clamping.

With point repetition enabled, keyboard and Consumer actions send one key down
and remain held until the contact ends. They emit no firmware-generated repeat
or intermediate release reports. Keyboard repeat delay/rate follow the host's
settings; volume/brightness hold behavior depends on the host's Consumer support.
With repetition disabled, the point emits one press/release pair.

Wheel/pan actions emit one immediate step, wait 600 ms, then repeat every 200 ms.
`POINT_WHEEL_HOLD_DELAY_MS` and `POINT_WHEEL_REPEAT_MS` in `point_gesture.h` set
these timings. A delayed timer emits one step without catch-up bursts, and busy
output skips repeats without accumulating a backlog. When that point permits conversion,
`max(abs(dx)/W, abs(dy)/H) * 100 >= step` ends repetition and starts edge parsing
at the current position. Earlier movement is not replayed. Outside an enabled
edge the contact becomes ordinary movement. Ordinary contacts outside enabled
points retain their previous behavior. Edge continuation still requires movement;
it is not timed auto-repeat.

Lift, additional contacts, invalid contact identity/confidence, connection/mode
changes and pipeline recovery release held keys and cancel wheel repetition.
A quick lift keeps the first queued action as a tap and discards later repeats.
Uncertain accepted transfer failures schedule a release. Queue entries older
than 100 ms expire rather than replaying stale input; completed held keys do not
expire while the contact remains valid.

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
- Update the ESP32-S3 touchpad and ESP32-S2 receiver together. Radio extension
  version 2 adds key-hold semantics and requires a matching receiver handshake.
  A version-1 receiver cannot acknowledge it and is not supported for this PTP path.
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
| 5: action | session uint32, sequence uint32, action uint8, steps int16, hold uint8 |
| 6: surface | extension version uint8 = 2, rotation uint8, nonzero session uint32 |
| 7: surface ACK | same payload as the accepted surface record |

Actions 1–6 use the existing edge action categories and signed direction.
Action 0 with zero steps and hold=0 cancels queued actions and releases held keys.
hold=1 (envelope byte 15) is allowed only for actions 1, 2, 5, 6 with steps ±1;
it sends one key down, released by the next cancellation/recovery. hold=0 keeps
the existing discrete action behavior, with nonzero steps bounded to −127…127.
Sequences are nonzero and increase within the session.
The receiver rejects duplicate/out-of-order actions, incorrect sessions and
actions from another peer. The receiver generates USB key down/up locally;
completed holds also release when the radio link times out or the mode changes.
Wheel reports do not change the receiver's PTP/mouse mode.

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
python tests/run_input_irq_tests.py $env:HOST_CLANG
```

The receiver's existing `tools/receiver/host_checks.py` also invokes the expanded
receiver suite and preserves its existing result-file location.

In the VS Code ESP-IDF terminal, run `idf.py build` separately in Main and 2.4G.
For BLE PTP compile/link verification without rebuilding SDK libraries or changing
the default sdkconfig, run `python tests/build_ble_variant.py` after the normal
Main build. It reads the existing compile commands, recompiles seven project
objects, replaces those members in a copy of libmain, and links against the
cached SDK libraries. Its test image is under `build/validation/ble-ptp`.

Automated validation covers 23 core scenarios, 32 receiver scenarios (including
27 existing regressions) and 4 BLE scenarios. These contain the 32 sleep/mask
combinations, 64 start-point/mask combinations, all 12 corner bindings, physical
circle boundaries, rotations, handoff, repetition, cancellation, migration and
commit failure, queued releases, receiver synchronization and BLE backpressure.
The core suite also checks initial confidence recovery, contact replacement and
array reordering, and a physical-circle grid against an independent integer oracle
for all four corners, radii 1–30%, and both surface orientations. Hold cases check
one press/no intermediate release, quick lift before/during/after submission,
failed transfers/releases, recovery, and radio hold/cancel propagation. Wheel
cases cover the initial/repeat deadlines, clock wrap, and output backpressure.
Five touch IRQ scenarios cover a pending signal before interrupt enable, idle and
duplicate wakes, coalesced reports, an enable-time edge race, and bounded batches
with read-error retry.

### Startup input diagnostics

The touch IRQ reader starts after haptic initialization and before transport
initialization. It checks the active-low INT line after enabling the interrupt
and drains pending reports in bounded batches. This avoids waiting for another
falling edge when INT was already low or several notifications were coalesced.
It does not synthesize an all-up report from an idle GPIO level.

`SURFACE_HAPTIC: Initialized` only confirms haptic initialization. The later
`IRQ_TP_INT: Touch reader enabled, pending=...` confirms touch capture startup;
host readiness still depends on USB enumeration or wireless connection.

The periodic `INPUT` statistics now include `link`, `mode`, `recovering`,
`all_up`, `releases`, and `mode_pending`. `recover` is a cumulative reset count,
including normal initialization and connection changes. `recovering=1 all_up=0`
means no all-up sample has been observed since reset; nonzero `releases` means
host release completion is outstanding. `link=0` means the host input path is not
ready. Logging does not release the gate. Physical idle alone does not imply that
an all-up report has been read. Hardware startup/reconnect acceptance remains
required, including starting with and without a finger held on the surface.
The protocol fixture follows the configurator document with corner reversal bytes
cleared to reflect the removed option: point
conversion mask `0x5` plus sleep gives byte 6 `0x0b`.

### Hardware acceptance still required

No device was flashed or hardware behavior certified by these software checks.
For each transport in PTP mode, verify:

1. USB save/readback/reconnect and power-cycle retention of all four switches.
2. Each corner and circle boundary in all four orientations; disabled corners
   preserve normal clicks/edges and their saved settings return when reenabled.
3. All 12 corner bindings, with no extra ordinary click.
4. Keyboard holds follow the host repeat settings; Consumer holds use the host's
   volume/brightness behavior. Wheels wait 600 ms then repeat every 200 ms.
   Lifting stops both; reconnect, mode changes and recovery leave no stuck keys.
5. Per-point conversion threshold and edge takeover, including motion into another
   corner and conversion where no edge is enabled.
6. BLE pairing, full PTP input, all auxiliary subscriptions and congestion recovery;
   receiver direction synchronization and USB re-enumeration.

Primary images: `Main/build/ESP32_HAPTIC_PRECISION_TOUCHPAD.bin` and
`2.4G/build/ESP32-Haptic-2.4G-Receiver.bin`. The BLE PTP validation image is separate
from the default BLE mouse build. Do not interpret successful host tests as
successful hardware acceptance.
