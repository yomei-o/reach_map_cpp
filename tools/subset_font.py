"""停留所名と画面の文字だけに絞った太字フォントを作る。

    python tools/subset_font.py <NotoSansJP[wght].ttf> <out.ttf> data/reach_toyama.txt [data/reach_toei.txt ...]

元のフォント: https://github.com/google/fonts/tree/main/ofl/notosansjp （SIL OFL 1.1）
"""
import sys, re, os
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

src, out = sys.argv[1:3]
datas = sys.argv[3:]
chars = set(chr(c) for c in range(0x20, 0x7F))
# 各地域の停留所名（S 行）と地域名（meta 行）
for data in datas:
    for l in open(data, encoding="utf-8"):
        if l.startswith("S "):
            t = l.rstrip("\n").split(" ", 3)
            if len(t) >= 4:
                chars.update(t[3])
        elif l.startswith("meta "):
            t = l.split()
            if len(t) >= 2:
                chars.update(t[1])
# 画面の文字：reach_map.hpp の文字列
hpp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "reach_map.hpp")
for lit in re.findall(r'"([^"]*)"', open(hpp, encoding="utf-8").read()):
    chars.update(lit)
# パネル・凡例などで使う文字（念のため）
chars.update("０１２３４５６７８９交通空白マップ出発時刻曜日平日土休到達分乗換停留所以内行ける"
             "近い暗い所この時間では公共交通で行けない地域発着方面前後駅から先："
             "・（）〜「」、。富山")
f = instancer.instantiateVariableFont(TTFont(src), {"wght": 700})
opt = subset.Options(); opt.layout_features = []; opt.notdef_outline = True; opt.name_IDs = ["*"]; opt.name_languages = ["*"]
opt.hinting = False
s = subset.Subsetter(opt); s.populate(text="".join(chars)); s.subset(f)
f.save(out)
print(len(chars), "chars ->", out)
