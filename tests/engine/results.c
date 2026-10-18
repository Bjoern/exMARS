/* Compare survivors, death order and complete persistent P-space, not final
 * transient core/queue state. Each scenario runs twice without resetting P-space. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "exhaust.h"
#include "sim.h"
#include "insn.h"
static uint64_t hash = UINT64_C(14695981039346656037);
static void digest(unsigned value) { hash = (hash ^ value) * UINT64_C(1099511628211); }
static insn_t instruction(unsigned op, unsigned mod, unsigned am, unsigned a, unsigned bm, unsigned b) {
    return (insn_t){a, b, OP(op, mod, am, bm)};
}
int main(void) {
    const unsigned limits[] = {1, 2, 3, 31, 8000};
    const unsigned lengths[] = {1, 2, 3, 4, 7, 33, 63, 64, 65, 2049, 4096, 4097, 8192};
    unsigned cases = 0;
    for (unsigned nw = 1; nw <= 4; ++nw)
    for (unsigned size = 80; size <= 8000; size *= 100)
    for (unsigned p = 0; p < 5; ++p)
    for (unsigned c = 0; c < sizeof(lengths)/sizeof(lengths[0]); ++c)
    for (unsigned scene = 0; scene < 8; ++scene) {
        mars_t *m = calloc(1, sizeof(*m));
        m->nWarriors = nw; m->coresize = size; m->processes = limits[p];
        m->cycles = lengths[c]; m->maxWarriorLength = 8;
#ifdef ENABLE_EARLY
        m->earlyExit = 1;
#endif
        if (!sim_alloc_bufs(m)) return 2;
        for (unsigned round = 0; round < 2; ++round) {
            sim_clear_core(m);
            for (unsigned i = 0; i < nw; ++i) {
                unsigned pos = i * size / nw;
                m->positions[i] = pos;
                insn_t *code = m->coreMem + pos;
                code[0] = instruction(EX_MOV, EX_mI, EX_IMMEDIATE, i + round, EX_DIRECT, 1);
                if (scene == 1) /* mix canonical direct and immediate imps */
                    code[0] = instruction(EX_MOV, EX_mI, EX_DIRECT, 0, EX_DIRECT, 1);
                if (scene == 2) { /* dormant non-imp process behind an imp */
                    code[0] = instruction(EX_SPL, EX_mB, EX_DIRECT, 4, EX_DIRECT, 0);
                    code[1] = instruction(EX_MOV, EX_mI, EX_IMMEDIATE, 0, EX_DIRECT, 1);
                }
                if (scene == 3) /* not a one-cell imp: do not classify it as one */
                    code[0] = instruction(EX_MOV, EX_mI, EX_IMMEDIATE, 0, EX_DIRECT, 2);
                if (scene == 4) {
                    code[0] = instruction(EX_SPL, EX_mB, EX_DIRECT, 1, EX_DIRECT, 0);
                    code[1] = instruction(EX_STP, EX_mAB, EX_IMMEDIATE, 7 + round, EX_IMMEDIATE, 1);
                    code[2] = instruction(EX_JMP, EX_mB, EX_DIRECT, size - 1, EX_DIRECT, 0);
                }
                if (scene == 5) { /* A self-copy-looking instruction with addressing side effects */
                    code[0] = instruction(EX_MOV, EX_mI, EX_BPOSTINC, 0, EX_DIRECT, 1);
                }
                if (scene == 6) { /* saturated SPL queues and large process-count differences */
                    code[0] = instruction(EX_SPL, EX_mB, EX_DIRECT, 0, EX_DIRECT, 0);
                    code[1] = instruction(EX_JMP, EX_mB, EX_DIRECT, size - 1, EX_DIRECT, 0);
                    if (i == 0) code[0] = instruction(EX_JMP, EX_mB, EX_DIRECT, 0, EX_DIRECT, 0);
                }
                if (scene == 7) { /* create executing STP by MOV.I from an otherwise dormant cell */
                    code[0] = instruction(EX_SPL, EX_mB, EX_DIRECT, 1, EX_DIRECT, 0);
                    code[1] = instruction(EX_MOV, EX_mI, EX_DIRECT, 4, EX_DIRECT, 1);
                    code[5] = instruction(EX_STP, EX_mAB, EX_IMMEDIATE, 11 + round, EX_IMMEDIATE, 1);
                }
            }
            int alive = sim_mw(m, m->positions, m->deaths);
            digest(alive);
            for (unsigned i = 0; i < nw - alive; ++i) digest(m->deaths[i]);
            for (unsigned i = 0; i < nw; ++i)
                for (unsigned j = 0; j < m->pspaceSize; ++j)
                    digest(pspace_get(m->pspacesOrigin[i], j));
            if (nw == 2 && scene == 4 && lengths[c] == 2 && limits[p] > 1 &&
                (pspace_get(m->pspacesOrigin[0], 1) != 7 + round ||
                 pspace_get(m->pspacesOrigin[1], 1) != 7 + round)) return 3;
        }
        sim_free_bufs(m);
        ++cases;
    }
    printf("%u cases, %u rounds: results/P-space %016llx\n", cases, cases * 2, (unsigned long long)hash);
    return 0;
}
