// ネイティブで到達圏を PNG に描く（取っておいた GTFS 焼きデータで確認する用）。
//   reach_map <data.txt> <basemap.txt> <font.ttf> <out_prefix> [W H]
#include <chrono>
#include <fstream>
#include <sstream>

#include "reach_map.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_truetype.h"
#include "stb_image_write.h"

static std::string read_all(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

int main(int argc, char** argv) {
    std::string data_p = argc > 1 ? argv[1] : "data/reach_toyama.txt";
    std::string base_p = argc > 2 ? argv[2] : "data/basemap.txt";
    std::string font_p = argc > 3 ? argv[3] : "fonts/NotoSansJP-Bold-subset.ttf";
    std::string out = argc > 4 ? argv[4] : "out/reach";
    int W = argc > 5 ? atoi(argv[5]) : 1400, H = argc > 6 ? atoi(argv[6]) : 1000;
    std::string data = read_all(data_p), base = read_all(base_p), font = read_all(font_p);
    rm::Map m;
    auto t0 = std::chrono::steady_clock::now();
    if (!m.load(data, base, (const unsigned char*)font.data(), font.size())) { fprintf(stderr, "load failed (%s)\n", data_p.c_str()); return 1; }
    if (argc > 7) { std::string a = read_all(argv[7]); if (!a.empty()) printf("admin rings %d\n", m.load_admin(a)); }   // 都県の外周・区界（任意）
    auto t1 = std::chrono::steady_clock::now();
    printf("%s: stops %zu, conns %zu, trips %d (load %.0f ms)\n", m.data.region.c_str(), m.data.stops.size(), m.data.conns.size(), m.data.ntrips,
           std::chrono::duration<double, std::milli>(t1 - t0).count());
    m.resize(W, H); m.fit();
    struct Shot { const char* name; int sec; };
    for (Shot s : {Shot{"0800", 8 * 3600}, Shot{"2000", 20 * 3600}, Shot{"2200", 22 * 3600}}) {
        m.set_time(s.sec);
        auto a = std::chrono::steady_clock::now();
        const uint8_t* px = m.render();
        auto b = std::chrono::steady_clock::now();
        int lim = m.dep + m.horizon, reached = 0; for (int v : m.data.arr) if (v <= lim) ++reached;
        std::string f = out + "_" + s.name + ".png";
        stbi_write_png(f.c_str(), W, H, 4, px, W * 4);
        printf("  %s: reached %d/%zu (%.0f ms) -> %s\n", s.name, reached, m.data.stops.size(),
               std::chrono::duration<double, std::milli>(b - a).count(), f.c_str());
    }
    return 0;
}
