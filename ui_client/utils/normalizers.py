# ui_client/utils/normalizers.py
from __future__ import annotations

from typing import Any, Dict, Optional

INTERNAL_COLUMNS = [
    "timestamp",
    "component",
    "host",
    "path",
    "result",
    "status_code",
    "bytes",
    "origin_selected",
    "response_time_ms",
    "client_ip",
    "client_port",
    "server_port",
]

def empty_record() -> Dict[str, Any]:
    """Create an empty standardized record with all internal columns present."""
    r: Dict[str, Any] = {}
    for k in INTERNAL_COLUMNS:
        r[k] = ""  # default blank
    # for numeric-ish fields, you may prefer None:
    # r["status_code"] = None
    # r["bytes"] = None
    # r["response_time_ms"] = None
    # r["client_port"] = None
    # r["server_port"] = None
    return r

def _to_int_or_blank(x: Any) -> Any:
    if x is None:
        return ""
    s = str(x).strip()
    if s == "":
        return ""
    try:
        return int(s)
    except ValueError:
        return ""

def normalize_cache_row(row: Dict[str, str]) -> Dict[str, Any]:
    """
    Expects cache CSV header like:
      timestamp,host,path,result,origin_selected,response_time_ms
    """
    rec = empty_record()
    rec["component"] = "cache"
    rec["timestamp"] = row.get("timestamp", "").strip()
    rec["host"] = row.get("host", "").strip()
    rec["path"] = row.get("path", "").strip()
    rec["result"] = row.get("result", "").strip()
    rec["origin_selected"] = row.get("origin_selected", "").strip()
    rec["response_time_ms"] = _to_int_or_blank(row.get("response_time_ms"))
    # cache doesn't have these:
    rec["status_code"] = ""
    rec["bytes"] = ""
    rec["client_ip"] = ""
    rec["client_port"] = ""
    rec["server_port"] = ""
    return rec

def normalize_origin_row(row: Dict[str, str], component: str) -> Dict[str, Any]:
    """
    Expects origin CSV header like:
      timestamp,server_port,client_ip,client_port,method,path,status_code,bytes
    (Host/result/origin_selected/response_time_ms not available here)
    """
    rec = empty_record()
    rec["component"] = component
    rec["timestamp"] = row.get("timestamp", "").strip()
    rec["path"] = row.get("path", "").strip()

    rec["server_port"] = _to_int_or_blank(row.get("server_port"))
    rec["client_ip"] = row.get("client_ip", "").strip()
    rec["client_port"] = _to_int_or_blank(row.get("client_port"))

    rec["status_code"] = _to_int_or_blank(row.get("status_code"))
    rec["bytes"] = _to_int_or_blank(row.get("bytes"))

    # origin doesn't have:
    rec["host"] = ""
    rec["result"] = ""
    rec["origin_selected"] = ""
    rec["response_time_ms"] = ""
    return rec
