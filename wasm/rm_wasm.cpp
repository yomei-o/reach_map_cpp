// WASM の入口。ページはデータ・下地・フォントを渡し、操作を渡すだけで、画面は全部ここ（reach_map.hpp）で描く。
//   build: sh wasm/build.sh → docs/rm.js, docs/rm.wasm
#include "reach_map.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <emscripten/emscripten.h>
#include <string>

static rm::Map g;
static std::string g_json;

extern "C" {
EMSCRIPTEN_KEEPALIVE int rm_init(const char* data, const char* basemap, const unsigned char* font, int fn) {
    return g.load(data, basemap, font, (size_t)fn) ? (int)g.data.stops.size() : 0;
}
EMSCRIPTEN_KEEPALIVE void rm_resize(int w, int h, double ui) { g.ui = ui; g.resize(w, h); }
EMSCRIPTEN_KEEPALIVE void rm_fit() { g.fit(); }
EMSCRIPTEN_KEEPALIVE void rm_pan(double dx, double dy) { g.pan(dx, dy); }
EMSCRIPTEN_KEEPALIVE void rm_zoom(double f, double x, double y) { g.zoom(f, x, y); }
EMSCRIPTEN_KEEPALIVE const uint8_t* rm_render() { return g.render(); }
// 操作
EMSCRIPTEN_KEEPALIVE void rm_time(int sec) { g.set_time(sec); }
EMSCRIPTEN_KEEPALIVE void rm_cal(int c) { g.set_cal(c); }
EMSCRIPTEN_KEEPALIVE void rm_minxfer(int sec) { g.set_minxfer(sec); }
EMSCRIPTEN_KEEPALIVE void rm_horizon(int sec) { g.set_horizon(sec); }
// 出発地をその点に。変わったら 1
EMSCRIPTEN_KEEPALIVE int rm_pick(double x, double y) { return g.set_origin_px(x, y) ? 1 : 0; }
// いまの状態（パネル更新用）
EMSCRIPTEN_KEEPALIVE const char* rm_info() {
    int lim = g.dep + g.horizon, reached = 0; for (int a : g.data.arr) if (a <= lim) ++reached;
    char b[256];
    snprintf(b, sizeof b, "{\"region\":\"%s\",\"dep\":%d,\"cal\":%d,\"reached\":%d,\"total\":%d,\"minxfer\":%d,\"horizon\":%d}",
             g.data.region.c_str(), g.dep, g.cal, reached, (int)g.data.stops.size(), g.minxfer, g.horizon);
    return (g_json = b).c_str();
}
// その点に近い停留所の名前（吹き出し用）。無ければ空
EMSCRIPTEN_KEEPALIVE const char* rm_at(double x, double y) {
    int s = g.nearest_stop(x, y, 12 * g.ui);
    if (s < 0) return (g_json = "").c_str();
    int a = g.data.arr[s], lim = g.dep + g.horizon;
    std::string t = g.data.stops[s].name;
    if (a <= lim) { int mn = (a - g.dep) / 60; t += "（" + std::to_string(mn) + " 分）"; }
    else t += "（この時間は行けない）";
    return (g_json = t).c_str();
}
}
