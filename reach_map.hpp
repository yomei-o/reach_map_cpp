// reach_map.hpp — 到達圏（公共交通＋徒歩で行ける範囲）を暗い地図に塗る。描画は全部ここ（C++）。
//
//   rm::Map m; m.load(data_text, basemap_text, font_bytes, n);
//   m.resize(1200, 900); m.fit();
//   m.set_origin_px(x, y);  m.set_time(8*3600);  m.set_cal(0);
//   const uint8_t* rgba = m.render();
//
// 到達圏は reach.hpp の CSA で各停留所の最早到着を出し、停留所ごとに徒歩圏（円）を
// 時間の min で重ねて面にする。路線に沿って伸び、路線の間は「交通空白」として暗いまま残る。
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "reach.hpp"
#include "raster.hpp"

namespace rm {

struct Map {
    Data data;
    Font font;
    int W = 0, H = 0;
    double ui = 1;
    double cx = 0, cy = 0;
    double scale = 0.03;   // 1 m あたりの px
    Canvas base;
    std::vector<uint8_t> rgba;
    std::vector<float> ftime;     // 各ピクセルの総所要（秒, INF=空白）
    std::vector<std::vector<V2>> basemap;   // 県境・海岸線（世界座標 m）

    // 操作の状態
    int origin = 0;
    int dep = 8 * 3600;
    int cal = 0;
    int horizon = 90 * 60;     // 到達の上限（秒）
    int minxfer = 180;         // 最低乗換時間（秒）
    double walk_speed = 1.1;   // m/s
    double walk_max = 800.0;   // m（停留所から塗る徒歩上限）
    bool dirty = true;         // 再計算＆再描画が要る

    bool load(const std::string& data_text, const std::string& basemap_text, const unsigned char* font_bytes, size_t font_n) {
        if (!data.load(data_text)) return false;
        if (font_bytes && font_n) font.load(font_bytes, font_n);
        load_basemap(basemap_text);
        origin = pick_busiest();
        recompute();
        return true;
    }

    // 地域を切り替える（下地・フォントはそのまま、GTFS データだけ入れ替え）
    int load_data(const std::string& data_text) {
        if (!data.load(data_text)) return 0;
        origin = pick_busiest();
        dirty = true;
        return (int)data.stops.size();
    }

    // 下地（Natural Earth の県境・陸地）。lon,lat を停留所と同じ投影へ
    void load_basemap(const std::string& text) {
        basemap.clear();
        double kx = 111320.0 * std::cos(data.clat * 3.14159265358979323846 / 180.0), ky = 110574.0;
        std::istringstream in(text); std::string ln;
        while (std::getline(in, ln)) {
            if (ln.rfind("ring ", 0) != 0) continue;
            std::vector<V2> r; std::istringstream s(ln.substr(5)); std::string tok;
            while (s >> tok) { size_t c = tok.find(','); if (c == std::string::npos) continue;
                double lo = std::stod(tok.substr(0, c)), la = std::stod(tok.substr(c + 1));
                r.push_back({(float)(lo * kx), (float)(-la * ky)}); }
            if (r.size() >= 2) basemap.push_back(std::move(r));
        }
    }

    int pick_busiest() {
        // 接続の端点に多く出る停留所（＝路線が集まる所）を既定の出発地に
        std::vector<int> cnt(data.stops.size(), 0);
        for (auto& c : data.conns) { if (c.from < (int)cnt.size()) cnt[c.from]++; }
        int best = 0; for (size_t i = 0; i < cnt.size(); ++i) if (cnt[i] > cnt[best]) best = (int)i;
        return best;
    }

    void resize(int w, int h) { W = w; H = h; base.resize(w, h); rgba.assign((size_t)w * h * 4, 255); ftime.assign((size_t)w * h, (float)INF); dirty = true; }

    void fit() {
        if (data.stops.empty()) return;
        // 中央値を中心に、遠い外れ停留所（25km 超）は除いて外接矩形を取る
        std::vector<double> xs, ys; xs.reserve(data.stops.size()); ys.reserve(data.stops.size());
        for (auto& s : data.stops) { xs.push_back(s.x); ys.push_back(s.y); }
        std::vector<double> sx = xs, sy = ys;
        std::nth_element(sx.begin(), sx.begin() + sx.size() / 2, sx.end());
        std::nth_element(sy.begin(), sy.begin() + sy.size() / 2, sy.end());
        double mx = sx[sx.size() / 2], my = sy[sy.size() / 2], R = 25000.0;
        double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
        for (auto& s : data.stops) {
            if (std::abs(s.x - mx) > R || std::abs(s.y - my) > R) continue;
            x0 = std::min(x0, s.x); y0 = std::min(y0, s.y); x1 = std::max(x1, s.x); y1 = std::max(y1, s.y);
        }
        if (x0 > x1) { x0 = mx - R; x1 = mx + R; y0 = my - R; y1 = my + R; }
        cx = (x0 + x1) / 2; cy = (y0 + y1) / 2;
        scale = std::min(W / std::max(1.0, x1 - x0), H / std::max(1.0, y1 - y0)) * 0.92;
        dirty = true;
    }
    V2 to_screen(double x, double y) const { return {(float)((x - cx) * scale + W / 2.0), (float)((y - cy) * scale + H / 2.0)}; }
    void to_world(double sx, double sy, double& x, double& y) const { x = cx + (sx - W / 2.0) / scale; y = cy + (sy - H / 2.0) / scale; }

    void pan(double dx, double dy) { cx -= dx / scale; cy -= dy / scale; dirty = true; }
    void zoom(double f, double sx, double sy) {
        double wx, wy; to_world(sx, sy, wx, wy);
        scale = std::clamp(scale * f, 0.003, 2.0);
        double wx2, wy2; to_world(sx, sy, wx2, wy2);
        cx += wx - wx2; cy += wy - wy2; dirty = true;
    }

    void set_time(int sec) { dep = sec; dirty = true; }
    void set_cal(int c) { cal = c ? 1 : 0; dirty = true; }
    void set_minxfer(int sec) { minxfer = sec; dirty = true; }
    void set_horizon(int sec) { horizon = sec; dirty = true; }

    int nearest_stop(double sx, double sy, double maxpx) const {
        double wx, wy; to_world(sx, sy, wx, wy);
        int best = -1; double bd = maxpx / scale; bd *= bd;
        for (size_t i = 0; i < data.stops.size(); ++i) {
            double dx = data.stops[i].x - wx, dy = data.stops[i].y - wy, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = (int)i; }
        }
        return best;
    }
    bool set_origin_px(double sx, double sy) {
        int s = nearest_stop(sx, sy, 24 * ui);
        if (s < 0) return false;
        origin = s; dirty = true; return true;
    }

    void recompute() { data.compute(origin, dep, cal, horizon, minxfer); }

    // 到達圏を塗る（停留所ごとに徒歩圏の円を時間の min でスタンプ）
    void build_fill() {
        std::fill(ftime.begin(), ftime.end(), (float)INF);
        int limit = dep + horizon;
        double Rpx = walk_max * scale;
        double mPerPx = 1.0 / scale;
        for (size_t i = 0; i < data.stops.size(); ++i) {
            int a = data.arr[i];
            if (a > limit) continue;
            V2 c = to_screen(data.stops[i].x, data.stops[i].y);
            float base_s = (float)(a - dep);
            int xlo = std::max(0, (int)(c.x - Rpx)), xhi = std::min(W - 1, (int)(c.x + Rpx));
            int ylo = std::max(0, (int)(c.y - Rpx)), yhi = std::min(H - 1, (int)(c.y + Rpx));
            for (int y = ylo; y <= yhi; ++y) {
                float dy = y - c.y;
                for (int x = xlo; x <= xhi; ++x) {
                    float dx = x - c.x, dpx = std::sqrt(dx * dx + dy * dy);
                    if (dpx > Rpx) continue;
                    float tot = base_s + (float)(dpx * mPerPx / walk_speed);
                    if (tot <= horizon) { float* f = &ftime[(size_t)y * W + x]; if (tot < *f) *f = tot; }
                }
            }
        }
    }

    // 所要時間 → 色（近い=緑青、遠い=紫）。sRGB
    Rgb ramp(float t) const {
        float u = std::clamp(t / (float)horizon, 0.f, 1.f);
        return mix(mix(Rgb{0.15f, 0.9f, 0.75f}, Rgb{0.35f, 0.5f, 0.95f}, std::min(1.f, u * 1.6f)),
                   Rgb{0.7f, 0.2f, 0.65f}, std::max(0.f, (u - 0.5f) * 2.f));
    }

    const uint8_t* render() {
        if (dirty) { recompute(); build_fill(); dirty = false; }
        Rgb bg{0.03f, 0.04f, 0.06f};
        base.clear(lin(bg));
        // 下地（薄く）
        for (auto& r : basemap) {
            std::vector<V2> s; s.reserve(r.size());
            for (auto& p : r) s.push_back(to_screen(p.x, p.y));
            polyline(base, s, std::max(1.f, (float)ui), {0.22f, 0.27f, 0.38f}, 0.5f);
        }
        // 到達圏の塗り（線形で直接書き込み）
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            float t = ftime[(size_t)y * W + x];
            if (t > horizon) continue;
            Rgb c = lin(ramp(t));
            float* p = &base.px[((size_t)y * W + x) * 3];
            p[0] = c.r; p[1] = c.g; p[2] = c.b;
        }
        // 下地をもう一度うっすら上に（海岸線が塗りの上でも分かるよう）
        for (auto& r : basemap) {
            std::vector<V2> s; s.reserve(r.size());
            for (auto& p : r) s.push_back(to_screen(p.x, p.y));
            polyline(base, s, std::max(1.f, (float)ui), {0.5f, 0.6f, 0.75f}, 0.18f);
        }
        // 停留所の点
        int limit = dep + horizon;
        for (size_t i = 0; i < data.stops.size(); ++i) {
            V2 p = to_screen(data.stops[i].x, data.stops[i].y);
            if (p.x < -4 || p.y < -4 || p.x > W + 4 || p.y > H + 4) continue;
            bool reach = data.arr[i] <= limit;
            disc(base, p, reach ? 1.6f * (float)ui : 1.1f * (float)ui, reach ? Rgb{0.95f, 0.98f, 1.f} : Rgb{0.35f, 0.4f, 0.5f}, reach ? 0.9f : 0.5f);
        }
        // 出発地
        V2 o = to_screen(data.stops[origin].x, data.stops[origin].y);
        disc(base, o, 6.f * (float)ui, {1.f, 0.3f, 0.3f}, 0.35f);
        disc(base, o, 3.2f * (float)ui, {1.f, 0.35f, 0.35f}, 1.f);
        draw_panels();
        base.to_rgba(rgba.data());
        return rgba.data();
    }

    void draw_panels() {
        if (!font.ok) return;
        auto hhmm = [](int s) { s = ((s % 86400) + 86400) % 86400; char b[8]; snprintf(b, sizeof b, "%02d:%02d", s / 3600, (s / 60) % 60); return std::string(b); };
        int pad = (int)(12 * ui), fp = (int)std::lround(15 * ui), sp = (int)std::lround(12 * ui);
        // 左上：地域・出発・到達数
        int lim = dep + horizon, reached = 0; for (int a : data.arr) if (a <= lim) ++reached;
        std::string l1 = data.region + "　" + hhmm(dep) + " 発（" + (cal ? "土休" : "平日") + "）";
        char b2[96]; snprintf(b2, sizeof b2, "%d 分以内に行ける停留所 %d / %d", horizon / 60, reached, (int)data.stops.size());
        float w1 = font.width(l1, fp), w2 = font.width(b2, sp);
        float bw = std::max(w1, w2) + pad * 2;
        round_rect(base, (float)pad, (float)pad, pad + bw, pad + fp + sp + pad * 1.6f, 8.f, {0.05f, 0.07f, 0.1f}, 0.82f);
        font.draw(base, pad * 2, pad + fp, l1, fp, {0.95f, 0.97f, 1.f}, {0.01f, 0.015f, 0.02f}, 2.f);
        font.draw(base, pad * 2, pad + fp + sp + (int)(6 * ui), b2, sp, {0.6f, 0.95f, 0.85f}, {0.01f, 0.015f, 0.02f}, 2.f);
        // 凡例（下）: 到達時間の色帯
        int lw = (int)(220 * ui), lh = (int)(12 * ui), lx = pad, ly = H - pad - lh - (int)(16 * ui);
        for (int i = 0; i < lw; ++i) { Rgb c = ramp((float)i / lw * horizon); for (int yy = 0; yy < lh; ++yy) { float* p = &base.px[((size_t)(ly + yy) * W + (lx + i)) * 3]; Rgb lc = lin(c); p[0] = lc.r; p[1] = lc.g; p[2] = lc.b; } }
        font.draw(base, lx, ly - (int)(3 * ui), "近い", (int)(11 * ui), {0.9f, 0.95f, 1.f}, {0.01f, 0.015f, 0.02f}, 2.f);
        std::string far = std::to_string(horizon / 60) + "分";
        font.draw(base, lx + lw - (int)font.width(far, (int)(11 * ui)), ly - (int)(3 * ui), far, (int)(11 * ui), {0.9f, 0.9f, 1.f}, {0.01f, 0.015f, 0.02f}, 2.f);
        font.draw(base, lx, ly + lh + (int)(13 * ui), "暗い所＝この時間では公共交通で行けない（交通空白）", (int)(11 * ui), {0.75f, 0.8f, 0.9f}, {0.01f, 0.015f, 0.02f}, 2.f);
    }
};

}  // namespace rm
