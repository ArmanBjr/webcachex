# ui_client/main_window.py
from PyQt5.QtWidgets import QMainWindow, QTabWidget

from ui_client.tabs.client_tab import ClientTab
from ui_client.tabs.cache_tab import CacheTab
from ui_client.tabs.origin_tab import OriginTab

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("WebCacheX Dashboard")
        self.resize(1300, 750)

        tabs = QTabWidget()

        tabs.addTab(ClientTab(), "Client")
        tabs.addTab(CacheTab(), "Cache")
        tabs.addTab(OriginTab("originA"), "Origin A")
        tabs.addTab(OriginTab("originB"), "Origin B")

        self.setCentralWidget(tabs)
