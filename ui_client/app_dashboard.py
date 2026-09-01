import sys
from PyQt5.QtWidgets import QApplication, QMainWindow, QTabWidget

from utils.csv_loader import load_cache_log, load_origin_log
from utils.paths import CACHE_LOG, ORIGIN_A_LOG, ORIGIN_B_LOG
from widgets.log_viewer_widget import LogViewerWidget
from tabs.client_tab import ClientTab


def load_cache_rows():
    return load_cache_log(str(CACHE_LOG))


def load_origin_a_rows():
    return load_origin_log(str(ORIGIN_A_LOG), component="originA")


def load_origin_b_rows():
    return load_origin_log(str(ORIGIN_B_LOG), component="originB")


def main():
    app = QApplication(sys.argv)

    tabs = QTabWidget()

    # Tab 1: Client
    tabs.addTab(ClientTab(cache_host="127.0.0.1", cache_port=8080), "Client")

    # Tab 2: Cache Logs
    w_cache = LogViewerWidget(loader_fn=load_cache_rows)
    w_cache.reload_logs()
    tabs.addTab(w_cache, "Cache Logs")

    # Tab 3: Origin A Logs
    w_a = LogViewerWidget(loader_fn=load_origin_a_rows)
    w_a.reload_logs()
    tabs.addTab(w_a, "Origin A Logs")

    # Tab 4: Origin B Logs
    w_b = LogViewerWidget(loader_fn=load_origin_b_rows)
    w_b.reload_logs()
    tabs.addTab(w_b, "Origin B Logs")

    win = QMainWindow()
    win.setWindowTitle("WebCacheX — Dashboard")
    win.setCentralWidget(tabs)
    win.resize(1350, 750)
    win.show()

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
