"""Writes the 2 font files of the PDF export.

    python3 tools/font-subset.py <Inter[opsz,wght].ttf> src/export/font

Input: the Inter variable font, version 4.001, SIL Open Font License 1.1.
Output: static instances at weight 400 and 700, optical size 14, reduced to
282 characters: Basic Latin, Latin-1 Supplement, General Punctuation and the
symbols listed in UNICODES. Needs fontTools; 4.64.0 wrote the files in the
repository.
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

src, out = sys.argv[1], sys.argv[2]
UNICODES = (
    list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + list(range(0x2000, 0x2070))
    + [0x394, 0x3A9, 0x3BC, 0x3C0, 0x20AC, 0x2190, 0x2191, 0x2192, 0x2193, 0x2194, 0x2212, 0x2248, 0x2260, 0x2264, 0x2265]
)
for name, wght in (('Regular', 400), ('Bold', 700)):
    font = TTFont(src)
    static = instancer.instantiateVariableFont(font, {'wght': wght, 'opsz': 14}, updateFontNames=True)
    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.notdef_outline = True
    options.name_IDs = [0, 1, 2, 3, 4, 6, 13, 14]
    options.name_languages = [0x409]
    options.glyph_names = False
    options.drop_tables += ['DSIG', 'STAT', 'GDEF', 'GPOS', 'GSUB', 'meta']
    sub = subset.Subsetter(options)
    sub.populate(unicodes=UNICODES)
    sub.subset(static)
    path = f'{out}/Inter-{name}-subset.ttf'
    static.save(path)
    check = TTFont(path)
    print(path, len(check.getBestCmap()), 'characters', sorted(check.keys()))
    print([(n.nameID, str(n)) for n in check['name'].names if n.nameID in (1, 2, 4, 6)])
