# ui_client/tabs/origin_tab.py
from PyQt5.QtWidgets import QWidget, QVBoxLayout
from ui_client.widgets.log_viewer_widget import LogViewerWidget
from ui_client.utils.csv_loader import load_origin_log

class OriginTab(QWidget):
    def __init__(self, component: str):
        super().__init__()
        layout = QVBoxLayout(self)

        path = "../web_server/origin_a_log.csv" if component == "originA" \
               else "../web_server/origin_b_log.csv"

        viewer = LogViewerWidget(
            loader_fn=lambda: load_origin_log(path, component=component)
        )
        layout.addWidget(viewer)
