from utils.csv_loader import load_cache_log, load_origin_log
from utils.paths import CACHE_LOG, ORIGIN_A_LOG, ORIGIN_B_LOG

cache = load_cache_log(str(CACHE_LOG))
a = load_origin_log(str(ORIGIN_A_LOG), component="originA")
b = load_origin_log(str(ORIGIN_B_LOG), component="originB")

print("cache rows:", len(cache))
print("A rows:", len(a))
print("B rows:", len(b))

print("sample cache row:", cache[-1] if cache else None)
print("sample originA row:", a[-1] if a else None)
