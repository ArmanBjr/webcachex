# ui_client/utils/csv_loader.py
from __future__ import annotations

import csv
from pathlib import Path
from typing import Any, Dict, List

from .normalizers import normalize_cache_row, normalize_origin_row

def _read_csv_rows(path: str) -> List[Dict[str, str]]:
    p = Path(path)
    if not p.exists():
        return []
    # newline="" مهمه برای csv در پایتون
    with p.open("r", encoding="utf-8", newline="") as f:
        # اگر فایل خالی باشه DictReader fieldnames=None میشه و iteration چیزی نمی‌ده
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            return []
        rows: List[Dict[str, str]] = []
        for row in reader:
            # DictReader ممکنه None بده؛ اینجا تمیزش می‌کنیم
            if row is None:
                continue
            rows.append(row)
        return rows

def load_cache_log(path: str) -> List[Dict[str, Any]]:
    """
    Returns standardized records for cache_log.csv
    """
    raw_rows = _read_csv_rows(path)
    out: List[Dict[str, Any]] = []
    for r in raw_rows:
        out.append(normalize_cache_row(r))
    return out

def load_origin_log(path: str, component: str) -> List[Dict[str, Any]]:
    """
    Returns standardized records for origin log CSVs.
    component: "originA" or "originB"
    """
    raw_rows = _read_csv_rows(path)
    out: List[Dict[str, Any]] = []
    for r in raw_rows:
        out.append(normalize_origin_row(r, component=component))
    return out
