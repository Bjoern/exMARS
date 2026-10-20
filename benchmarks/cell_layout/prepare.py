"""Generate isolated scalar-cell experiments; never edit the production engine."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/cell-layout'


def prepare(name, bits=0):
    dest = OUT / name
    dest.mkdir(parents=True, exist_ok=True)
    for pattern in ('*.h', '*.c', '*.inc'):
        for source in ROOT.glob(pattern):
            (dest / source.name).write_text(source.read_text())
    if not bits:
        return dest
    width = 16 if bits == 64 else 8
    typ = f'uint{bits}_t'
    header = dest / 'exhaust.h'
    text = header.read_text()
    # Keep assembler/test field syntax through a union view. The interpreter
    # itself uses only explicit scalar masks/shifts, generated below.
    cell = f'''#include <stdint.h>
typedef union insn_st {{
    struct {{ {typ} a : {width}, b : {width}, in : 16; }};
    {typ} word;
}} insn_t;
_Static_assert(sizeof(insn_t) == {bits // 8}, "cell size");
'''
    for field, shift, mask in [('a', 0, (1 << width)-1), ('b', width, (1 << width)-1), ('in', width*2, 65535)]:
        cell += f'''static inline unsigned cell_{field}(const insn_t *p) {{
    return (unsigned)((p->word >> {shift}) & {mask}u);
}}
static inline void cell_set_{field}(insn_t *p, unsigned v) {{
    p->word = (p->word & ~(({typ}){mask}u << {shift})) | ((({typ})v & {mask}u) << {shift});
}}
'''
    text, count = re.subn(r'typedef struct insn_st \{.*?\} insn_t;', cell, text, flags=re.S)
    assert count == 1
    header.write_text(text)
    for name in ('sim.c', 'sim_loop.inc'):
        p = dest / name
        text = p.read_text()
        # Retain the original target cell across postincrement addressing.
        def post(match):
            ptr, field, body = match.groups()
            return '{insn_t* f = ' + ptr + ';' + body.replace('*f', 'f->' + field) + '}'
        text, count = re.subn(r'\{field_t\* f = &\((pta|ptb)->([ab])\);(.*?)\}', post, text, flags=re.S)
        assert count == (6 if name == 'sim_loop.inc' else 0)
        # Macro updates must work on scalar cells, not an addressable subfield.
        for op in ('INCMOD', 'DECMOD', 'ADDMOD', 'SUBMOD'):
            pattern = rf'{op}\((\w+)->([ab])([^;]*?)\);'
            def update(m):
                ptr, field, args = m.groups()
                return f'{{ unsigned cv = cell_{field}({ptr}); {op}(cv{args}); cell_set_{field}({ptr}, cv); }}'
            text = re.sub(pattern, update, text)
        # Assignments first, then remaining reads. Only known engine cell
        # expressions are accepted, so source changes fail visibly below.
        expr = r'(?:(?:coreMem|core|code)\[[^\]]+\]\.|(?:pta|ptb|ip|f|blocker)->)'
        def address(s):
            return s[:-2] if s.endswith('->') else '&' + s[:-1]
        text = re.sub(r'(' + expr + r')(a|b|in)\s*=\s*([^;]+);',
                      lambda m: f'cell_set_{m[2]}({address(m[1])}, {m[3]});', text)
        text = re.sub(r'(' + expr + r')(a|b|in)\b',
                      lambda m: f'cell_{m[2]}({address(m[1])})', text)
        # Whole-cell MOV.I must also be an explicit scalar operation.
        text = text.replace('*ptb = *ip;', 'ptb->word = ip->word;')
        assert not re.search(expr + r'(a|b|in)\b', text)
        if bits == 32 and name == 'sim.c':
            text = text.replace('int sim_alloc_bufs(mars_t* mars) {', '''int sim_alloc_bufs(mars_t* mars) {
    if (mars->coresize > 256) {
        snprintf(mars->errmsg, sizeof(mars->errmsg), "32-bit cells require core <= 256");
        return 0;
    }''')
        p.write_text(text)
    return dest

if __name__ == '__main__':
    for name, bits in [('baseline', 0), ('packed64', 64), ('packed32', 32)]:
        print(prepare(name, bits))
