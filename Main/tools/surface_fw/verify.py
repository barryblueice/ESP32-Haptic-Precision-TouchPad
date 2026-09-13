"""Validate the imported Surface image without ESP-IDF or an external checkout.

Run: python -B tools/surface_fw/verify.py --self-test --write-report
--source-root additionally compares against the original extraction directory.
Default execution is read-only. --write-report refreshes waveforms.csv/validation.json.
"""
import argparse
import csv
import hashlib
import json
import re
import struct
import unittest
from pathlib import Path

from test_policy import run_policy_tests, verify_policy_reference
from test_runtime import run_runtime_tests, verify_interfaces
from test_service import run_service_tests
from test_input import run_input_tests

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DRIVER = ROOT / "main/I2C/SUB_DEV/mcu-drivers"
IMAGE_C = DRIVER / "fw_img/cs40l25_fw_img.c"
REFERENCE = json.loads((HERE / "reference.json").read_text(encoding="utf-8"))
REGIONS = (("XM", 0x02800B60, 2408, 25), ("YM", 0x03400000, 4048, 53))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def c_array(path):
    text = path.read_text(encoding="utf-8-sig")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    match = re.search(r"const\s+uint8_t\s+cs40l25_fw_img\[\].*?=\s*\{(.*?)\};", text, re.S)
    require(match is not None, "Missing cs40l25_fw_img array")
    return bytes(int(item.strip(), 0) for item in match[1].split(",") if item.strip())


def take(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset, "Truncated image")
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, take(data, offset, struct.calcsize(fmt)))


def checksum(data):
    lo = hi = 0
    for (word,) in struct.iter_unpack("<H", data):
        lo = (lo + word) % 65535
        hi = (hi + lo) % 65535
    return hi << 16 | lo


def parse_image(data, check_hash=True):
    header = unpack("<10I", data, 0)
    magic, version, size, nsyms, nalgs, fw_id, revision, nblocks, maximum, release = header
    require((magic, version, size, fw_id, revision) ==
            (0x54B998FF, 2, len(data), 0x1400E1, 0x0A0603), "Invalid image identity/header")
    require((size, nsyms, nalgs, nblocks, maximum, release) ==
            (38576, 33, 3, 167, 240, 0), "Unexpected image layout")
    symbols = {}
    pos = 40
    for _ in range(nsyms):
        symbol, address = unpack("<II", data, pos)
        require(symbol not in symbols and address != 0, "Invalid symbol entry")
        symbols[symbol] = address
        pos += 8
    require(symbols == {int(k): v for k, v in REFERENCE["symbols"].items()},
            "Symbol map differs from new sdk_surface image")
    algorithms = unpack("<3I", data, pos)
    require(algorithms == (0x1400E1, 0xBD, 0x111), "Unexpected algorithms")
    pos += 4 * nalgs
    blocks = []
    memory = {}
    for index in range(nblocks):
        size, address = unpack("<II", data, pos)
        require(0 < size <= maximum and size % 4 == 0 and address % 4 == 0,
                f"Invalid block boundary: {index}")
        payload = take(data, pos + 8, size)
        blocks.append((address, payload))
        memory.update((address + offset, value) for offset, value in enumerate(payload))
        pos += 8 + size
    require(pos + 8 == len(data), "Unexpected footer position")
    footer, stored_checksum = unpack("<II", data, pos)
    require(footer == 0x936BE2A6, "Invalid footer magic")
    require(stored_checksum == checksum(data[:-4]), "Fletcher checksum mismatch")
    actual = [[address, len(payload), digest(payload)] for address, payload in blocks]
    require(actual == REFERENCE["blocks"], "Ordered DSP writes differ from original extraction")
    if check_hash:
        require(digest(data) == REFERENCE["image_sha256"], "Image SHA256 mismatch")
    return symbols, blocks, memory


def parse_table(data, bank, base_index, expected_count=None):
    require(len(data) % 4 == 0, f"{bank}: unaligned table")
    words = unpack(">" + str(len(data) // 4) + "I", data, 0)
    pos = 0
    descriptors = []
    while pos < len(words) and words[pos] != 0xFFFFFF:
        require(pos + 3 <= len(words), f"{bank}: truncated descriptor")
        kind, offset, size = words[pos:pos + 3]
        require(kind == 8, f"{bank}: unsupported waveform type")
        descriptors.append((kind, offset, size))
        pos += 3
    require(pos < len(words) and words[pos] == 0xFFFFFF, f"{bank}: missing terminator")
    require(descriptors, f"{bank}: empty table")
    if expected_count is not None:
        require(len(descriptors) == expected_count, f"{bank}: unexpected count")
    expected_offset = pos + 1
    result = []
    for local, (kind, offset, size) in enumerate(descriptors):
        require(size > 0 and offset == expected_offset and offset + size <= len(words),
                f"{bank} entry {local}: invalid offset/length")
        payload = data[offset * 4:(offset + size) * 4]
        result.append(dict(index=base_index + local, bank=bank, local_index=local,
                           type=kind, offset_words=offset, length_words=size,
                           payload_sha256=digest(payload)))
        expected_offset = offset + size
    require(expected_offset == len(words), f"{bank}: unaccounted payload bytes")
    return result


def collect_tables(memory):
    tables, entries = [], []
    for bank, address, size, expected_count in REGIONS:
        require(all(a in memory for a in range(address, address + size)),
                f"{bank}: missing programmed memory")
        data = bytes(memory[a] for a in range(address, address + size))
        parsed = parse_table(data, bank, len(entries), expected_count)
        tables.append(data)
        entries.extend(parsed)
    require([e["index"] for e in entries] == list(range(78)), "Invalid index coverage")
    return tables, entries


def defines(path):
    text = path.read_text(encoding="utf-8")
    return {name: int(value, 0) for name, value in re.findall(
        r"^#define\s+(\w+)\s+\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)[uU]?\s*\)?\s*$",
        text, re.M)}


def verify_binding():
    actual = defines(DRIVER / "cs40l25/cs40l25_sym.h")
    expected = {
        "CS40L25_SYM_FIRMWARE_HALO_STATE": 1,
        "CS40L25_SYM_FIRMWARE_HALO_HEARTBEAT": 2,
        "CS40L25_SYM_FIRMWARE_GAIN_CONTROL": 0x10,
        "CS40L25_SYM_FIRMWARE_GPIO_ENABLE": 0x11,
        "CS40L25_SYM_FIRMWARE_POWERSTATE": 0x12,
        "CS40L25_SYM_VIBEGEN_TIMEOUT_MS": 0x1D,
        "CS40L25_SYM_VIBEGEN_COMPENSATION_ENABLE": 0x1E,
        "CS40L25_SYM_DYNAMIC_F0_DYNAMIC_F0_ENABLED": 0x201,
    }
    require(all(actual.get(k) == v for k, v in expected.items()), "Driver symbol IDs mismatch")
    for name in re.findall(r"\bCS40L25_SYM_[A-Z0-9_]+", (
            DRIVER / "cs40l25/cs40l25.c").read_text(encoding="utf-8")):
        require(name in actual and actual[name] in map(int, REFERENCE["symbols"]),
                f"Driver needs unavailable symbol: {name}")
    metadata = defines(DRIVER / "cs40l25/bsp/surface_fw_metadata.h")
    # Independent WMFW metadata: algo 0xBD, XM base=723, YM base=0.
    expected_metadata = {
        "SURFACE_FW_SIZE_BYTES": 38576, "SURFACE_FW_ID": 0x1400E1,
        "SURFACE_FW_REVISION": 0x0A0603, "SURFACE_FW_XM_WAVES": 25, "SURFACE_FW_YM_WAVES": 53,
        "SURFACE_VIBEGEN_ENABLE_REG": 0x02800000 + (723 + 0) * 4,
        "SURFACE_VIBEGEN_STATUS_REG": 0x02800000 + (723 + 1) * 4,
        "SURFACE_VIBEGEN_NUM_WAVES_REG": 0x02800000 + (723 + 3) * 4,
        "SURFACE_VIBEGEN_WAVETABLE_XM_REG": 0x02800000 + (723 + 5) * 4,
        "SURFACE_VIBEGEN_WAVETABLE_YM_REG": 0x03400000,
    }
    require(all(metadata.get(k) == v for k, v in expected_metadata.items()), "WMFW metadata mismatch")
    symbols_text = (DRIVER / "cs40l25/cs40l25_sym.h").read_text()
    require(not re.search(r"^#define CS40L25_ALGORITHM_(CLAB|DVL)\b", symbols_text, re.M),
            "Absent algorithm enabled")
    test_defines = defines(ROOT / "main/surface_haptic_test.c")
    require(test_defines.get("TEST_DURATION_MS") == 0 and
            test_defines.get("TEST_INTERVAL_MS") == 2000, "Policy test defaults changed")
    main_text = (ROOT / "main/surface_haptic_test.c").read_text(encoding="utf-8")
    settings = re.search(r"s_test_settings\[\]\s*=\s*\{([^}]+)\}", main_text)
    require(settings is not None, "Missing automatic policy test sequence")
    settings = [int(value.strip(), 0) for value in settings[1].split(",")]
    require(settings == [0, 25, 63, 75, 100], "Automatic policy settings changed")
    rows = verify_policy_reference()
    return [dict(setting=setting, enabled=setting != 0,
                 press_index=int(rows[setting]["press_index"]),
                 release_index=int(rows[setting]["release_index"])) for setting in settings]


def run_negative_tests(data, tables):
    class RejectionTests(unittest.TestCase):
        def test_truncated_image(self):
            with self.assertRaises(ValueError):
                parse_image(data[:-8])

        def test_corrupt_payload(self):
            altered = bytearray(data)
            altered[1024] ^= 1
            with self.assertRaisesRegex(ValueError, "checksum"):
                parse_image(altered)

        def test_wrong_symbol_even_with_repaired_checksum(self):
            altered = bytearray(data)
            struct.pack_into("<I", altered, 40, 0x1E)
            struct.pack_into("<I", altered, len(altered) - 4, checksum(altered[:-4]))
            with self.assertRaisesRegex(ValueError, "symbol|Symbol"):
                parse_image(altered, check_hash=False)

        def test_missing_terminator(self):
            altered = bytearray(tables[0])
            struct.pack_into(">I", altered, 25 * 12, 8)
            with self.assertRaises(ValueError):
                parse_table(altered, "XM", 0)

        def test_offset_inside_header(self):
            altered = bytearray(tables[0])
            struct.pack_into(">I", altered, 4, 0)
            with self.assertRaisesRegex(ValueError, "offset/length"):
                parse_table(altered, "XM", 0)

        def test_payload_out_of_bounds(self):
            altered = bytearray(tables[1])
            struct.pack_into(">I", altered, 8, 0xFFFFFF)
            with self.assertRaisesRegex(ValueError, "offset/length"):
                parse_table(altered, "YM", 25)

        def test_wrong_wave_type(self):
            altered = bytearray(tables[1])
            struct.pack_into(">I", altered, 0, 0xFE)
            with self.assertRaisesRegex(ValueError, "type"):
                parse_table(altered, "YM", 25)

        def test_wrong_entry_count(self):
            # A valid shorter table still must not pass the expected-count contract.
            short = struct.pack(">4I", 8, 4, 1, 0xFFFFFF) + b"\0\0\0\0"
            self.assertEqual(len(parse_table(short, "XM", 0)), 1)
            with self.assertRaisesRegex(ValueError, "count"):
                parse_table(short, "XM", 0, 25)

    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(RejectionTests))
    require(result.wasSuccessful(), "Negative tests failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--source-root", type=Path, help="External mcu-drivers checkout (read only)")
    args = parser.parse_args()
    data = c_array(IMAGE_C)
    symbols, blocks, memory = parse_image(data)
    tables, entries = collect_tables(memory)
    policy_sequence = verify_binding()
    if args.source_root:
        source = args.source_root / "targetbin/unpacked/cs40l25"
        require(data == (source / "sdk_surface/cs40l25_fw_img.bin").read_bytes(),
                "Imported image differs from external binary")
        require(data == c_array(source / "sdk_surface/cs40l25_fw_img.c"),
                "Imported image differs from external C array")
        source_blocks = json.loads((source / "haptic_blocks.json").read_text())
        require(REFERENCE["blocks"] == [[b["address"], b["size"], b["sha256"]] for b in source_blocks],
                "Reference block snapshot differs from extraction")
    policy_tests = None
    integration_tests = None
    worker_tests = None
    input_tests = None
    verify_interfaces()
    if args.self_test:
        run_negative_tests(data, tables)
        policy_tests = run_policy_tests()
        integration_tests = run_runtime_tests()
        worker_tests = run_service_tests()
        input_tests = run_input_tests()
    report = dict(image_size=len(data), image_sha256=digest(data),
                  firmware_id="0x1400e1", firmware_revision="0x0a0603",
                  symbols=len(symbols), ordered_blocks=len(blocks),
                  fletcher_checksum_verified=True, original_block_hashes_match=True,
                  xm_waves=25, ym_waves=53, total_waves=len(entries),
                  first_index=0, last_index=77,
                  automatic_test=dict(mode="surface_press_release_policy",
                                      sequence=policy_sequence, events=["PRESS", "RELEASE"],
                                      event_slot_ms=2000, nominal_round_ms=20000,
                                      playback_mode="MBOX1 index playback",
                                      duration_argument_ms=0,
                                      cp_attenuation=0, gpi_attenuation=0,
                                      gpio_triggers=False, disabled_setting_skips_io=True),
                  policy_host_tests=policy_tests,
                  normal_mode=dict(default=True, strength_default=63, queue_capacity=8,
                                   stale_event_ms=100, event_poll_ms=10, heartbeat_timeout_ms=2000),
                  integration_host_tests=integration_tests,
                  worker_host_tests=worker_tests,
                  input_host_tests=input_tests,
                  standalone_board_test="previous five-setting test passed, user reported",
                  integration_board_test="not performed; no flashing")
    if args.write_report:
        (HERE / "validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        with (HERE / "waveforms.csv").open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=list(entries[0]))
            writer.writeheader()
            writer.writerows(entries)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
