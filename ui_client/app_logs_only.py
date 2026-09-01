import sys

from PyQt5.QtWidgets import QApplication, QMainWindow

from utils.csv_loader import load_cache_log, load_origin_log
from utils.paths import CACHE_LOG, ORIGIN_A_LOG, ORIGIN_B_LOG
from widgets.log_viewer_widget import LogViewerWidget


def build_rows():
    rows = []
    rows += load_cache_log(str(CACHE_LOG))
    rows += load_origin_log(str(ORIGIN_A_LOG), component="originA")
    rows += load_origin_log(str(ORIGIN_B_LOG), component="originB")
    return rows


if __name__ == "__main__":
    app = QApplication(sys.argv)

    w = LogViewerWidget(loader_fn=build_rows)
    w.reload_logs() 
    w.show()



    win = QMainWindow()
    win.setWindowTitle("WebCacheX — Logs (Stage 3)")
    win.setCentralWidget(w)
    win.resize(1200, 650)
    win.show()

    sys.exit(app.exec_())
