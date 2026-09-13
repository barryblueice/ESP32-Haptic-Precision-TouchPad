from dataclasses import dataclass
from queue import Empty, Queue
from threading import Event, Lock
from time import monotonic

import hid
from PySide6.QtCore import QThread, Signal


TARGET_DEVICES = [
    (0x0D00, 0x072A),
    (0x0D00, 0x072B),
    (0x0D00, 0x072C),
    (0x0D00, 0x072D),
]
TARGET_INTERFACE = 0
REPORT_SIZE = 64
SCAN_INTERVAL = 0.5


@dataclass(frozen=True)
class USBDeviceInfo:
    path: bytes
    vendor_id: int
    product_id: int
    product_string: str
    serial_number: str


class USBCommunicatorThread(QThread):
    devices_changed = Signal(list)
    scan_finished = Signal(bool, str)
    # Path and selection generation keep delayed results tied to their request.
    device_event = Signal(object, int, bool, str)
    packet_result = Signal(object, int, bool, str)
    status_message = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._commands = Queue()
        self._stopping = Event()
        self._selection_lock = Lock()
        self._requested_path = None
        self._generation = 0
        # Everything below is owned exclusively by run(), including HID handles.
        self._devices = None
        self._scan_ok = False
        self._dev = None
        self._active_path = None
        self._active_generation = 0

    def request_refresh(self):
        with self._selection_lock:
            if not self._stopping.is_set():
                self._commands.put(("refresh", None, 0))

    def select_device(self, path):
        with self._selection_lock:
            if not self._stopping.is_set():
                self._generation += 1
                self._requested_path = path
                self._commands.put(("select", path, self._generation))
            return self._generation

    def send_packet(self, path, generation):
        with self._selection_lock:
            if not self._stopping.is_set():
                self._commands.put(("send", path, generation))

    def _is_current(self, path, generation):
        with self._selection_lock:
            return (
                not self._stopping.is_set()
                and path == self._requested_path
                and generation == self._generation
            )

    def _close_device(self, message):
        dev, path, generation = self._dev, self._active_path, self._active_generation
        self._dev = None
        self._active_path = None
        if dev is not None:
            try:
                dev.close()
            except Exception as exc:
                message = f"{message}; Close failed: {exc}"
        if path is not None:
            self.device_event.emit(path, generation, False, message)

    def _scan(self):
        try:
            devices = {}
            for vid, pid in TARGET_DEVICES:
                for info in hid.enumerate(vid, pid):
                    if (
                        info.get("interface_number") != TARGET_INTERFACE
                        or info.get("vendor_id") != vid
                        or info.get("product_id") != pid
                        or not info.get("path")
                    ):
                        continue
                    path = info["path"]
                    devices[path] = USBDeviceInfo(
                        path, vid, pid,
                        info.get("product_string") or "USB HID device",
                        info.get("serial_number") or "",
                    )
            device_list = sorted(
                devices.values(),
                key=lambda device: (device.vendor_id, device.product_id, device.path),
            )
        except Exception as exc:
            # Preserve the last list/handle, but disallow writes until a good scan.
            self._scan_ok = False
            self.scan_finished.emit(False, f"Scan failed: {exc}")
            return

        self._scan_ok = True
        if self._active_path is not None and self._active_path not in devices:
            self._close_device("Device disconnected")
        if device_list != self._devices:
            self._devices = device_list
            self.devices_changed.emit(device_list)
        self.scan_finished.emit(True, f"Scan complete: {len(device_list)} matching device(s) found")

    def _select_device(self, path, generation):
        if not self._is_current(path, generation):
            return
        self._close_device("Device selection changed")
        if path is None:
            return
        device_info = next((d for d in self._devices or [] if d.path == path), None)
        if not self._scan_ok or device_info is None:
            self.device_event.emit(path, generation, False, "Device unavailable. Refresh and select it again.")
            return
        self._active_path = path
        self._active_generation = generation
        try:
            self._dev = hid.device()
            self._dev.open_path(path)
            self._dev.set_nonblocking(True)
        except Exception as exc:
            self._close_device(f"Open Failed: {exc}")
            return
        if not self._is_current(path, generation):
            self._close_device("Device selection changed")
            return
        self.device_event.emit(
            path, generation, True,
            f"Device connected: (VID:{hex(device_info.vendor_id)} PID:{hex(device_info.product_id)})",
        )

    def _send_packet(self, path, generation):
        if not self._is_current(path, generation):
            self.packet_result.emit(path, generation, False, "Device selection changed. Send cancelled.")
            return
        # Check for unplugging since the last periodic scan before every write.
        self._scan()
        if (
            not self._is_current(path, generation)
            or not self._scan_ok
            or self._dev is None
            or self._active_path != path
            or self._active_generation != generation
        ):
            self.packet_result.emit(path, generation, False, "Target device unavailable. DFU command not sent.")
            return
        packet = bytes([0xFF]) * (REPORT_SIZE + 1)
        try:
            written = self._dev.write(packet)
        except Exception as exc:
            message = f"Write error: {exc}"
            self._close_device(message)
            self.packet_result.emit(path, generation, False, message)
            return
        if written != len(packet):
            self.packet_result.emit(
                path, generation, False,
                f"Incomplete write: {written}/{len(packet)} bytes. Check the device before retrying.",
            )
            return
        self.packet_result.emit(path, generation, True, "DFU command sent (65 bytes)")

    def run(self):
        self.status_message.emit("USB Monitor started...")
        next_scan = 0.0
        try:
            while not self._stopping.is_set():
                if monotonic() >= next_scan:
                    self._scan()
                    next_scan = monotonic() + SCAN_INTERVAL
                try:
                    command, path, generation = self._commands.get(
                        timeout=max(0.0, next_scan - monotonic())
                    )
                except Empty:
                    continue
                if self._stopping.is_set():
                    break
                if command == "refresh":
                    self._scan()
                    next_scan = monotonic() + SCAN_INTERVAL
                elif command == "select":
                    self._select_device(path, generation)
                elif command == "send":
                    self._send_packet(path, generation)
        except Exception as exc:
            self._stopping.set()
            self.scan_finished.emit(False, f"USB worker error: {exc}")
            self.status_message.emit(f"USB worker stopped: {exc}")
        finally:
            self._close_device("USB connection closed")

    def stop(self):
        with self._selection_lock:
            self._stopping.set()
            self._commands.put(("stop", None, 0))
        self.wait()
