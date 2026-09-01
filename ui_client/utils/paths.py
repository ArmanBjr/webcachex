from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CACHE_LOG = ROOT / "cache_server" / "cache_log.csv"
ORIGIN_A_LOG = ROOT / "web_server" / "origin_a_log.csv"
ORIGIN_B_LOG = ROOT / "web_server" / "origin_b_log.csv"
