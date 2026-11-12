# reach_map_cpp — 交通空白マップ（公共交通でどこまで行けるか、C++ / WASM）

ある停留所をある時刻に出発して、**公共交通（バス）＋徒歩で何分でどこまで行けるか**を暗い地図に塗ります。
路線に沿って色が伸び、**路線と路線のあいだは暗いまま＝交通空白**として残ります。
出発時刻を夕方・夜に進めると到達圏が一気にしぼみ、**「いつ詰むか」**が見えます。

画面はすべて C++ のソフト描画（到達圏の塗り・下地・停留所・パネル・文字）。ブラウザの JavaScript は、
データを渡すことと、クリック・スライダー・ドラッグを渡すことだけをします。重い地図エンジンも WebGL も使いません。

**デモ**: https://yomei-o.github.io/reach_map_cpp/ （公共交通オープンデータチャレンジ2026 応募予定）

- 地図を**クリック**すると、そこを出発地にして到達圏を塗り直します
- **出発時刻**スライダー・**平日/土休**・**到達の上限（分）**・**最低乗換時間**を変えられます
- 停留所にマウスを乗せると、その停留所名とそこへの所要時間

> これは課題を「解決」する道具ではありません。少ない便をどう使えるか・どこが空白か・いつ詰むかを**見える化**する道具です。

## しくみ

| ファイル | やること |
|---|---|
| `tools/build_reach.py` | GTFS（停留所・時刻表・カレンダー）から、到達圏計算に要るものだけを `data/<地域>.txt` に焼く（停留所・接続・徒歩乗換） |
| `reach.hpp` | **CSA（Connection Scan）**で、出発地・出発時刻から各停留所への最早到着時刻を計算。乗り換えは同一便は待ちなし・別便は最低乗換時間・近い停留所へは徒歩 |
| `reach_map.hpp` | 到達圏を塗る。停留所ごとに徒歩圏（円）を所要時間の min で重ねて面にし、時間で色分け。下地（県境・海岸線）も薄く描く |
| `raster.hpp` | ソフト描画（quake_map_cpp / metro_map_cpp と同じもの） |
| `wasm/` | ブラウザ用の入口とビルド |
| `main.cpp` | ネイティブで同じ画面を PNG に描く（確認用） |

### 到達圏の出し方

GTFS の時刻表を「ある停留所を何時に出て、次の停留所に何時に着く」という**接続**の列にして、発車時刻の昇順に走査します（CSA）。
- 同じ便に乗り続ける → 待ち時間なし
- 別の便に乗り換える → 最低乗換時間（スライダー、既定 3 分）を足す
- 近い停留所（250m 以内）へは徒歩で乗り換え
- 最後に、到達した各停留所から徒歩圏（既定 800m）を塗る。歩いて届かない所は空白

## ビルド

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build/Release/reach_map.exe data/reach_toyama.txt data/basemap.txt fonts/NotoSansJP-Bold-subset.ttf out/reach 1400 1000
sh wasm/build.sh   # docs/rm.js, docs/rm.wasm
```

別の地域で作り直すとき（GTFS(zip) を展開したフォルダ `<dir>` から）:

```sh
python tools/build_reach.py <dir> data/<地域>.txt <地域名>
```

## 出典とライセンス

- コード: BSD-3-Clause（`LICENSE`）
- バスの時刻表・停留所（富山）: 富山地方鉄道（gtfs-data.jp のオープンデータ）を加工して作成。`data/reach_toyama.txt` はそれを加工したもの。内容の正確さ・完全さは保証されません
- 下地（県境・海岸線）: Natural Earth（パブリックドメイン）を加工
- 文字: Noto Sans JP（SIL Open Font License 1.1、`fonts/OFL.txt`）
- stb_truetype / stb_image_write: パブリックドメイン（または MIT）
