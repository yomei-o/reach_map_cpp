# GTFS から、到達圏計算（CSA）に要るものだけを data/<name>.txt に焼く。
#
#   python tools/build_reach.py <gtfs_dir> <out.txt> [地域名]
#
# <gtfs_dir> は GTFS(zip) を展開したフォルダ（stops.txt, stop_times.txt, trips.txt, calendar.txt など）。
# 出力（ブラウザ側 WASM が読んで、クリックした停留所・時刻から到達圏を計算して塗る）:
#   meta <地域名> <中心lat> <中心lon> <停留所数> <便数>
#   S <lat> <lon> <停留所名>                      index = 出てくる順（0..）
#   C <cal> <便index> <発秒> <発stop> <着秒> <着stop>   接続。発秒で昇順。cal 0=平日 1=土日祝
#   F <stopA> <stopB> <秒>                        徒歩乗り換え（近い停留所どうし。両向き）
# 秒は真夜中から（24時以降は 86400 超）。標準ライブラリだけ。
import csv, sys, os, math
from collections import defaultdict

TRANSFER_MAX = 250.0     # m  徒歩乗り換えの上限
WALK_SPEED = 1.1         # m/s

def sec(t):
    p = t.split(":"); return int(p[0]) * 3600 + int(p[1]) * 60 + int(p[2])

def rd(d, f):
    with open(os.path.join(d, f), encoding="utf-8-sig", newline="") as fp:
        return list(csv.DictReader(fp))

def main(gdir, out, region="地域"):
    stops = rd(gdir, "stops.txt")
    sidx = {s["stop_id"]: i for i, s in enumerate(stops)}
    lat = [float(s["stop_lat"]) for s in stops]
    lon = [float(s["stop_lon"]) for s in stops]
    nm = [s["stop_name"] for s in stops]
    n = len(stops)

    # service_id -> cal（0=平日, 1=土日祝）。平日=月曜運行、土日祝=日曜運行で判定
    cal_of = {}
    for c in rd(gdir, "calendar.txt"):
        if c.get("monday") == "1":
            cal_of[c["service_id"]] = 0
        elif c.get("sunday") == "1" or c.get("saturday") == "1":
            cal_of[c["service_id"]] = 1
    trip_srv = {t["trip_id"]: t["service_id"] for t in rd(gdir, "trips.txt")}

    # 便に通し番号（採用するサービスのみ）
    trip_idx = {}
    def tid(t):
        if t not in trip_idx:
            trip_idx[t] = len(trip_idx)
        return trip_idx[t]

    rows_by_trip = defaultdict(list)
    for r in rd(gdir, "stop_times.txt"):
        srv = trip_srv.get(r["trip_id"])
        if srv in cal_of and r["stop_id"] in sidx:
            rows_by_trip[r["trip_id"]].append(r)

    conns = []  # (dep, cal, trip, dep_idx, arr, arr_idx)
    for t, rows in rows_by_trip.items():
        rows.sort(key=lambda r: int(r["stop_sequence"]))
        cal = cal_of[trip_srv[t]]; ti = tid(t)
        for a, b in zip(rows, rows[1:]):
            da, ab = a.get("departure_time"), b.get("arrival_time")
            if not da or not ab:
                continue
            conns.append((sec(da), cal, ti, sidx[a["stop_id"]], sec(ab), sidx[b["stop_id"]]))
    conns.sort()

    # 徒歩乗り換え（近い停留所）: グリッド近傍
    lat0 = sum(lat) / n
    kx = 111320.0 * math.cos(math.radians(lat0)); ky = 110574.0
    X = [lon[i] * kx for i in range(n)]; Y = [lat[i] * ky for i in range(n)]
    cell = TRANSFER_MAX
    grid = defaultdict(list)
    for i in range(n):
        grid[(int(X[i] // cell), int(Y[i] // cell))].append(i)
    foots = []
    for i in range(n):
        gx, gy = int(X[i] // cell), int(Y[i] // cell)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for j in grid.get((gx + dx, gy + dy), []):
                    if j <= i:
                        continue
                    d = math.hypot(X[i] - X[j], Y[i] - Y[j])
                    if d <= TRANSFER_MAX:
                        foots.append((i, j, int(d / WALK_SPEED)))

    o = []
    o.append(f"meta {region} {lat0:.6f} {sum(lon)/n:.6f} {n} {len(trip_idx)}")
    for i in range(n):
        o.append(f"S {lat[i]:.6f} {lon[i]:.6f} {nm[i]}")
    for dep, cal, ti, di, arr, ai in conns:
        o.append(f"C {cal} {ti} {dep} {di} {arr} {ai}")
    for a, b, w in foots:
        o.append(f"F {a} {b} {w}")
    sys.stderr.write(f"stops {n} trips {len(trip_idx)} conns {len(conns)} foots {len(foots)}\n")
    with open(out, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("\n".join(o))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "地域")
