from collections import Counter
from hashlib import sha256
import sys

from PySide6.QtCore import Qt, QSignalBlocker
from PySide6.QtWidgets import (
    QApplication, QComboBox, QHBoxLayout, QLabel, QMainWindow,
    QPushButton, QStatusBar, QVBoxLayout, QWidget,
)

from usb_module import USBCommunicatorThread


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self._selected_path = None
        self._selection_generation = 0
        self._connected = False
        self._connection_failed = False
        self._scan_ok = False
        self._sending = False
        self._refresh_pending = True
        self._devices = None

        self.setWindowTitle("ESP32 PTP DFU")
        self.setStatusBar(QStatusBar(self))
        self.statusBar().showMessage("Initializing...")

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)
        layout.setContentsMargins(10, 10, 10, 10)

        device_row = QHBoxLayout()
        device_row.addWidget(QLabel("USB device:"))
        self.device_combo = QComboBox()
        self.device_combo.setMinimumWidth(380)
        self.device_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        self.device_combo.addItem("Scanning...", None)
        self.device_combo.setEnabled(False)
        self.device_combo.currentIndexChanged.connect(self.on_device_selected)
        self.device_combo.activated.connect(self.on_device_activated)
        device_row.addWidget(self.device_combo, 1)
        self.btn_refresh = QPushButton("Refresh devices")
        self.btn_refresh.clicked.connect(self.on_refresh_clicked)
        device_row.addWidget(self.btn_refresh)
        layout.addLayout(device_row)

        self.btn_action = QPushButton("Waiting for connect...")
        self.btn_action.setMinimumHeight(50)
        self.btn_action.setEnabled(False)
        self.btn_action.clicked.connect(self.on_button_clicked)
        layout.addWidget(self.btn_action)

        self.usb_thread = USBCommunicatorThread(self)
        self.usb_thread.devices_changed.connect(self.handle_devices_changed)
        self.usb_thread.scan_finished.connect(self.handle_scan_finished)
        self.usb_thread.device_event.connect(self.handle_device_event)
        self.usb_thread.packet_result.connect(self.handle_packet_result)
        self.usb_thread.status_message.connect(self.statusBar().showMessage)
        self.usb_thread.start()
        self.resize(720, self.sizeHint().height())

    def handle_devices_changed(self, devices):
        if devices == self._devices:
            return
        self._devices = devices
        previous_path = self._selected_path
        name_counts = Counter(device.product_string for device in devices)
        with QSignalBlocker(self.device_combo):
            self.device_combo.clear()
            self.device_combo.addItem("Select a device" if devices else "No matching devices found", None)
            selected_index = 0
            for index, device in enumerate(devices, start=1):
                text = (
                    f"{device.product_string} | "
                    f"{device.vendor_id:04X}:{device.product_id:04X} | "
                    f"SN: {device.serial_number or 'N/A'}"
                )
                if not device.serial_number or name_counts[device.product_string] > 1:
                    text += f" | Path ID: {sha256(device.path).hexdigest()[:12]}"
                self.device_combo.addItem(text, device.path)
                self.device_combo.setItemData(
                    index, device.path.decode("utf-8", errors="replace"),
                    Qt.ItemDataRole.ToolTipRole,
                )
                if device.path == previous_path:
                    selected_index = index
            self.device_combo.setCurrentIndex(selected_index)
        self.device_combo.setEnabled(bool(devices))
        self._update_path_tooltip()
        if previous_path is not None and selected_index == 0:
            self.on_device_selected()
            self.statusBar().showMessage("Device disconnected")

    def _update_path_tooltip(self):
        self.device_combo.setToolTip(
            self.device_combo.currentData(Qt.ItemDataRole.ToolTipRole) or ""
        )

    def _update_send_button(self):
        self.btn_action.setEnabled(
            self._selected_path is not None
            and self._connected and self._scan_ok and not self._sending
        )
        self.btn_action.setText("Enter DFU Mode" if self._connected else "Waiting for connect...")

    def on_device_selected(self, _index=None):
        self._selected_path = self.device_combo.currentData()
        self._connected = False
        self._connection_failed = False
        self._sending = False
        self._selection_generation = self.usb_thread.select_device(self._selected_path)
        self._update_path_tooltip()
        self._update_send_button()
        self.statusBar().showMessage(
            "Connecting to selected device..." if self._selected_path is not None else "Select a device"
        )

    def on_device_activated(self, _index):
        # Choosing the same item does not emit currentIndexChanged.
        if self._connection_failed and self._selected_path is not None:
            self.on_device_selected()

    def on_refresh_clicked(self):
        self._refresh_pending = True
        self.btn_refresh.setEnabled(False)
        self.statusBar().showMessage("Scanning USB devices...")
        self.usb_thread.request_refresh()

    def handle_scan_finished(self, success, message):
        was_ok = self._scan_ok
        self._scan_ok = success
        # Routine successful polls must not erase send results or connection errors.
        if not success or self._refresh_pending or not was_ok:
            self.statusBar().showMessage(message)
        self._refresh_pending = False
        self.btn_refresh.setEnabled(True)
        self._update_send_button()

    def _matches_selection(self, path, generation):
        return path == self._selected_path and generation == self._selection_generation

    def handle_device_event(self, path, generation, connected, message):
        if self._matches_selection(path, generation):
            self._connected = connected
            self._connection_failed = not connected
            self.statusBar().showMessage(message)
            self._update_send_button()

    def on_button_clicked(self):
        if not self.btn_action.isEnabled():
            return
        self._sending = True
        self._update_send_button()
        self.statusBar().showMessage("Sending DFU command...")
        self.usb_thread.send_packet(self._selected_path, self._selection_generation)

    def handle_packet_result(self, path, generation, success, message):
        if self._matches_selection(path, generation):
            self._sending = False
            self.statusBar().showMessage(message)
            self._update_send_button()

    def closeEvent(self, event):
        self.usb_thread.stop()
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
