from __future__ import annotations

from typing import Any, Dict, List

from PyQt5.QtCore import QTimer
from PyQt5.QtCore import QSortFilterProxyModel, Qt
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QLabel, QTableView
)

from widgets.log_table_model import LogTableModel



class LogViewerWidget(QWidget):
    def __init__(self,  loader_fn, parent=None):
        super().__init__(parent)

        self.model = LogTableModel([])
        self.proxy = QSortFilterProxyModel(self)
        self.proxy.setSourceModel(self.model)
        self.proxy.setFilterCaseSensitivity(Qt.CaseInsensitive)
        self.proxy.setFilterKeyColumn(-1)  # filter across ALL columns

        self.search = QLineEdit()
        self.search.setPlaceholderText("Search in logs (host/path/result/status...)")
        self.search.textChanged.connect(self.proxy.setFilterFixedString)

        top = QHBoxLayout()
        top.addWidget(QLabel("Filter:"))
        top.addWidget(self.search)

        self.table = QTableView()
        self.table.setModel(self.proxy)
        self.table.setSortingEnabled(True)
        self.table.setAlternatingRowColors(True)
        self.table.horizontalHeader().setStretchLastSection(True)
        self._loader_fn = loader_fn

        layout = QVBoxLayout()
        layout.addLayout(top)
        layout.addWidget(self.table)
        self.setLayout(layout)
        self._timer = QTimer(self)
        self._timer.setInterval(1000)  # 1s
        self._timer.timeout.connect(self.reload_logs)
        self._timer.start()


    def set_rows(self, rows: List[Dict[str, Any]]) -> None:
        self.model.set_rows(rows)
    
    def reload_logs(self):
        rows = self._loader_fn()   # تابعی که از app می‌گیریم (build_rows)
        self.model.set_rows(rows)  # باید داخل model پیاده‌سازی بشه

