from __future__ import annotations

from typing import Any, Dict, List

from PyQt5.QtCore import QAbstractTableModel, QModelIndex, Qt

from utils.normalizers import INTERNAL_COLUMNS



class LogTableModel(QAbstractTableModel):
    def __init__(self, rows: List[Dict[str, Any]] | None = None, parent=None):
        super().__init__(parent)
        self._rows: List[Dict[str, Any]] = rows or []
        self._columns = INTERNAL_COLUMNS[:]  # keep order stable

    def set_rows(self, rows: List[Dict[str, Any]]) -> None:
        self.beginResetModel()
        self._rows = rows
        self.endResetModel()

    def rowCount(self, parent=QModelIndex()) -> int:
        return 0 if parent.isValid() else len(self._rows)

    def columnCount(self, parent=QModelIndex()) -> int:
        return 0 if parent.isValid() else len(self._columns)

    def headerData(self, section: int, orientation: Qt.Orientation, role: int = Qt.DisplayRole):
        if role != Qt.DisplayRole:
            return None
        if orientation == Qt.Horizontal:
            return self._columns[section]
        return str(section + 1)

    def data(self, index: QModelIndex, role: int = Qt.DisplayRole):
        if not index.isValid():
            return None

        row = self._rows[index.row()]
        col_name = self._columns[index.column()]
        value = row.get(col_name, "")

        if role == Qt.DisplayRole:
            return "" if value is None else str(value)

        # Optional: align numbers right
        if role == Qt.TextAlignmentRole:
            if isinstance(value, int):
                return int(Qt.AlignVCenter | Qt.AlignRight)
            return int(Qt.AlignVCenter | Qt.AlignLeft)

        return None
    def set_rows(self, rows):
        self.beginResetModel()
        self._rows = rows
        self.endResetModel()

