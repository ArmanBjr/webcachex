from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, List

from PyQt5.QtCore import Qt, QByteArray
from PyQt5.QtGui import QPixmap
from PyQt5.QtNetwork import QTcpSocket, QAbstractSocket
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox,
    QPushButton, QPlainTextEdit, QTabWidget, QFileDialog,
    QMessageBox, QCheckBox, QSplitter, QFrame
)


@dataclass
class HttpResponse:
    raw: bytes
    header_text: str
    body: bytes
    content_type: str


def parse_http_response(raw: bytes) -> HttpResponse:
    sep = raw.find(b"\r\n\r\n")
    if sep == -1:
        header_bytes = raw
        body = b""
    else:
        header_bytes = raw[:sep]
        body = raw[sep + 4:]

    header_text = header_bytes.decode("iso-8859-1", errors="replace")

    content_type = ""
    for line in header_text.split("\r\n"):
        if line.lower().startswith("content-type:"):
            content_type = line.split(":", 1)[1].strip()
            break

    return HttpResponse(raw=raw, header_text=header_text, body=body, content_type=content_type)


class ClientTab(QWidget):
    def __init__(self, cache_host: str = "127.0.0.1", cache_port: int = 8080):
        super().__init__()
        self.cache_host = cache_host
        self.cache_port = cache_port

        # socket for normal requests
        self.sock = QTcpSocket(self)
        self.sock.connected.connect(self._on_connected)
        self.sock.readyRead.connect(self._on_ready_read)
        self.sock.errorOccurred.connect(self._on_error)
        self.sock.stateChanged.connect(self._on_state)

        # separate socket for list fetching (avoids interfering with user request)
        self.list_sock = QTcpSocket(self)
        self.list_sock.connected.connect(self._on_list_connected)
        self.list_sock.readyRead.connect(self._on_list_ready_read)
        self.list_sock.errorOccurred.connect(self._on_list_error)
        self.list_sock.stateChanged.connect(self._on_list_state)

        self._rx = bytearray()
        self._pending_request = ""
        self._last_response: Optional[HttpResponse] = None

        self._list_rx = bytearray()
        self._pending_list_request = ""

        # ---------- UI ----------
        self.host_combo = QComboBox()
        self.host_combo.addItems(["a.local", "b.local"])

        # Path as editable dropdown (filled from /__list)
        self.path_combo = QComboBox()
        self.path_combo.setEditable(True)
        self.path_combo.setInsertPolicy(QComboBox.NoInsert)
        self.path_combo.setMinimumWidth(260)
        self.path_combo.setEditText("/index.html")

        self.close_chk = QCheckBox("Connection: close")
        self.close_chk.setChecked(True)

        self.btn_refresh_list = QPushButton("Refresh List")
        self.btn_refresh_list.clicked.connect(self.refresh_list)

        self.btn_send = QPushButton("Send")
        self.btn_send.clicked.connect(self.send_request)

        self.btn_save = QPushButton("Save Body…")
        self.btn_save.clicked.connect(self.save_body)
        self.btn_save.setEnabled(False)

        # Raw request preview
        self.req_preview = QPlainTextEdit()
        self.req_preview.setReadOnly(True)
        self.req_preview.setMaximumBlockCount(2000)

        # Response views
        self.headers_view = QPlainTextEdit()
        self.headers_view.setReadOnly(True)

        self.body_text_view = QPlainTextEdit()
        self.body_text_view.setReadOnly(True)

        self.body_image = QLabel("No image")
        self.body_image.setAlignment(Qt.AlignCenter)
        self.body_image.setFrameShape(QFrame.StyledPanel)

        self.body_tabs = QTabWidget()
        self.body_tabs.addTab(self.body_text_view, "Body (text)")
        self.body_tabs.addTab(self.body_image, "Body (image)")

        self.status = QLabel("Idle")

        top = QHBoxLayout()
        top.addWidget(QLabel("Host:"))
        top.addWidget(self.host_combo, 1)
        top.addWidget(QLabel("Path:"))
        top.addWidget(self.path_combo, 3)
        top.addWidget(self.close_chk)
        top.addWidget(self.btn_refresh_list)
        top.addWidget(self.btn_send)
        top.addWidget(self.btn_save)

        left = QWidget()
        left_l = QVBoxLayout(left)
        left_l.addWidget(QLabel("Request (raw):"))
        left_l.addWidget(self.req_preview)

        right = QWidget()
        right_l = QVBoxLayout(right)
        right_l.addWidget(QLabel("Response headers:"))
        right_l.addWidget(self.headers_view, 1)
        right_l.addWidget(QLabel("Response body:"))
        right_l.addWidget(self.body_tabs, 2)

        splitter = QSplitter()
        splitter.addWidget(left)
        splitter.addWidget(right)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 2)

        layout = QVBoxLayout(self)
        layout.addLayout(top)
        layout.addWidget(splitter)
        layout.addWidget(self.status)

        # signals
        self.host_combo.currentTextChanged.connect(lambda _: self._update_request_preview())
        self.path_combo.currentTextChanged.connect(lambda _: self._update_request_preview())
        self.path_combo.lineEdit().textChanged.connect(lambda _: self._update_request_preview())
        self.close_chk.stateChanged.connect(lambda _: self._update_request_preview())

        # auto-load list when host changes
        self.host_combo.currentTextChanged.connect(lambda _: self.refresh_list())

        self._update_request_preview()
        self.refresh_list()  # initial list

    def _build_request(self, path: str) -> str:
        host = self.host_combo.currentText().strip()
        p = (path or "/").strip()
        if not p.startswith("/"):
            p = "/" + p
        conn = "close" if self.close_chk.isChecked() else "keep-alive"
        return (
            f"GET {p} HTTP/1.1\r\n"
            f"Host: {host}\r\n"
            f"Connection: {conn}\r\n"
            f"\r\n"
        )

    def _build_current_request(self) -> str:
        path = self.path_combo.currentText().strip()
        if not path:
            path = self.path_combo.lineEdit().text().strip()
        return self._build_request(path)

    def _update_request_preview(self):
        self.req_preview.setPlainText(self._build_current_request())

    # ---------------- normal request ----------------
    def send_request(self):
        if self.sock.state() != QAbstractSocket.UnconnectedState:
            self.sock.abort()

        self._rx.clear()
        self._last_response = None
        self.btn_save.setEnabled(False)

        self.headers_view.clear()
        self.body_text_view.clear()
        self.body_image.setText("No image")
        self.body_image.setPixmap(QPixmap())

        self._pending_request = self._build_current_request()
        self.status.setText(f"Connecting to cache {self.cache_host}:{self.cache_port} ...")
        self.sock.connectToHost(self.cache_host, self.cache_port)

    def _on_connected(self):
        self.status.setText("Connected. Sending request...")
        self.sock.write(QByteArray(self._pending_request.encode("ascii", errors="replace")))

    def _on_ready_read(self):
        self._rx += bytes(self.sock.readAll())
        self.status.setText(f"Receiving... ({len(self._rx)} bytes)")

    def _on_state(self, st):
        if st == QAbstractSocket.UnconnectedState and self._rx:
            self._finalize_response()

    def _finalize_response(self):
        resp = parse_http_response(bytes(self._rx))
        self._last_response = resp

        self.headers_view.setPlainText(resp.header_text)

        ctype = (resp.content_type or "").lower()

        if ctype.startswith("image/"):
            pix = QPixmap()
            if pix.loadFromData(resp.body):
                self.body_image.setPixmap(pix)
                self.body_tabs.setCurrentIndex(1)
            else:
                self.body_image.setText("Failed to decode image")
                self.body_tabs.setCurrentIndex(1)
        else:
            try:
                text = resp.body.decode("utf-8", errors="replace")
            except Exception:
                text = resp.body.decode("iso-8859-1", errors="replace")
            self.body_text_view.setPlainText(text)
            self.body_tabs.setCurrentIndex(0)

        self.btn_save.setEnabled(True)
        self.status.setText("Done.")

    def save_body(self):
        if not self._last_response:
            return
        path, _ = QFileDialog.getSaveFileName(self, "Save response body", "response.bin")
        if not path:
            return
        try:
            with open(path, "wb") as f:
                f.write(self._last_response.body)
        except Exception as e:
            QMessageBox.critical(self, "Save failed", str(e))

    def _on_error(self, _):
        self.status.setText(f"Socket error: {self.sock.errorString()}")

    # ---------------- list request (/__list) ----------------
    def refresh_list(self):
        if self.list_sock.state() != QAbstractSocket.UnconnectedState:
            self.list_sock.abort()

        self._list_rx.clear()
        self._pending_list_request = self._build_request("/__list")
        self.status.setText("Fetching /__list ...")
        self.list_sock.connectToHost(self.cache_host, self.cache_port)

    def _on_list_connected(self):
        self.list_sock.write(QByteArray(self._pending_list_request.encode("ascii", errors="replace")))

    def _on_list_ready_read(self):
        self._list_rx += bytes(self.list_sock.readAll())

    def _on_list_state(self, st):
        if st == QAbstractSocket.UnconnectedState and self._list_rx:
            self._apply_list_response()

    def _apply_list_response(self):
        resp = parse_http_response(bytes(self._list_rx))
        # body is lines of paths
        try:
            text = resp.body.decode("utf-8", errors="replace")
        except Exception:
            text = resp.body.decode("iso-8859-1", errors="replace")

        paths: List[str] = []
        for line in text.splitlines():
            line = line.strip()
            if not line:
                continue
            if not line.startswith("/"):
                line = "/" + line
            paths.append(line)

        # optional: sort to look nicer
        paths.sort()

        # update combo
        current = self.path_combo.currentText().strip() or self.path_combo.lineEdit().text().strip()
        self.path_combo.blockSignals(True)
        self.path_combo.clear()
        self.path_combo.addItems(paths)
        if current:
            self.path_combo.setEditText(current)
        self.path_combo.blockSignals(False)

        self.status.setText(f"/__list loaded: {len(paths)} paths")

    def _on_list_error(self, _):
        self.status.setText(f"List error: {self.list_sock.errorString()}")
