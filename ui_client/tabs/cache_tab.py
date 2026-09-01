# ui_client/tabs/cache_tab.py
from PyQt5.QtWidgets import QWidget, QVBoxLayout
from ui_client.widgets.log_viewer_widget import LogViewerWidget
from ui_client.utils.csv_loader import load_cache_log

class CacheTab(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout(self)

        viewer = LogViewerWidget(
            loader_fn=lambda: load_cache_log(
                "../cache_server/cache_log.csv"
            )
        )
        layout.addWidget(viewer)
