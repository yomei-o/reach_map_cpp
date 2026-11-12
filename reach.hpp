// reach.hpp — GTFS から焼いたデータ（tools/build_reach.py の出力）を読み、
// 「ある停留所・ある時刻」から公共交通＋徒歩で各停留所に最早何時に着くかを計算する（CSA）。
//
//   rm::Data d; d.load(text);
//   d.compute(origin_stop, 8*3600, /*cal*/0, /*horizon秒*/90*60, /*最低乗換秒*/180);
//   d.arr[i]  … 停留所 i への最早到着時刻（秒, 届かなければ INF）
//
// CSA（Connection Scan）: 接続を発車時刻の昇順に走査するだけ。乗り換えは
//   - 同じ便に乗り続ける: 待ち時間なし（trip_reached）
//   - 別の便に乗り換え: 最低乗換時間を足す
//   - 近い停留所へ徒歩で乗り換え: F（徒歩）で広げる
#pragma once
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace rm {

constexpr int INF = 1 << 30;

struct Stop {
    double lat = 0, lon = 0;
    double x = 0, y = 0;            // 投影後（m, 正距円筒）。load で入れる
    std::string name;
};

struct Conn {
    int dep, from, arr, to;
    int trip;
    char cal;
};

struct Data {
    std::string region = "地域";
    double clat = 0, clon = 0;
    int ntrips = 0;
    std::vector<Stop> stops;
    std::vector<Conn> conns;                              // 発車時刻の昇順
    std::vector<std::vector<std::pair<int, int>>> foot;  // 停留所 → (近い停留所, 徒歩秒)
    std::vector<int> arr;                                 // compute の結果（最早到着秒）

    bool load(const std::string& text) {
        stops.clear(); conns.clear(); foot.clear();
        std::istringstream in(text); std::string ln;
        while (std::getline(in, ln)) {
            if (ln.empty() || ln[0] == '#') continue;
            char k = ln[0];
            std::istringstream s(ln);
            std::string kind; s >> kind;
            if (k == 'm') {               // meta <region> <lat> <lon> <stops> <trips>
                s >> region >> clat >> clon;
                int ns = 0; s >> ns >> ntrips;
            } else if (k == 'S') {        // S <lat> <lon> <name>
                Stop st; s >> st.lat >> st.lon; std::getline(s >> std::ws, st.name);
                stops.push_back(std::move(st));
            } else if (k == 'C') {        // C <cal> <trip> <dep> <from> <arr> <to>
                int cal, trip, dep, from, arr_, to; s >> cal >> trip >> dep >> from >> arr_ >> to;
                conns.push_back({dep, from, arr_, to, trip, (char)cal});
            } else if (k == 'F') {        // F <a> <b> <sec>
                int a, b, w; s >> a >> b >> w;
                if (a >= 0 && b >= 0) {
                    if ((int)foot.size() <= std::max(a, b)) foot.resize(std::max(a, b) + 1);
                    foot[a].push_back({b, w}); foot[b].push_back({a, w});
                }
            }
        }
        if (foot.size() < stops.size()) foot.resize(stops.size());
        // 投影（正距円筒, 中心 clat）。y は北が負
        double kx = 111320.0 * std::cos(clat * 3.14159265358979323846 / 180.0), ky = 110574.0;
        for (auto& st : stops) { st.x = st.lon * kx; st.y = -st.lat * ky; }
        arr.assign(stops.size(), INF);
        return !stops.empty();
    }

    // 最早到着を計算。cal: 0=平日 1=土日祝。horizon: 出発からの上限秒。minxfer: 最低乗換秒
    void compute(int origin, int t0, int cal, int horizon, int minxfer) {
        size_t n = stops.size();
        arr.assign(n, INF);
        if (origin < 0 || origin >= (int)n) return;
        std::vector<char> trip_hit(ntrips, 0);
        int limit = t0 + horizon;
        arr[origin] = t0;
        for (auto& f : foot[origin]) if (t0 + f.second < arr[f.first]) arr[f.first] = t0 + f.second;
        for (const Conn& c : conns) {
            if (c.dep > limit) break;
            if (c.cal != cal) continue;
            bool board = trip_hit[c.trip] || (arr[c.from] != INF && arr[c.from] + minxfer <= c.dep);
            if (!board) continue;
            trip_hit[c.trip] = 1;
            if (c.arr < arr[c.to]) {
                arr[c.to] = c.arr;
                for (auto& f : foot[c.to]) if (c.arr + f.second < arr[f.first]) arr[f.first] = c.arr + f.second;
            }
        }
    }

    int reached_count(int t0, int horizon) const {
        int cnt = 0, limit = t0 + horizon;
        for (int a : arr) if (a <= limit) ++cnt;
        return cnt;
    }
};

}  // namespace rm
