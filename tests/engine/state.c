/* Differential state test. Compile against original and candidate engines,
 * then compare stdout. Padding bytes are deliberately excluded from hashes.
 * Exercises every legal opcode/modifier/address-mode combination, aliasing,
 * wrapping, tiny queues, multiple warriors and P-space over two rounds. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "exhaust.h"
#include "sim.h"
#include "insn.h"

static uint32_t state = 123456789;
static uint32_t random32(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
static uint64_t hash = UINT64_C(14695981039346656037);
static void digest(uint32_t value) {
    hash = (hash ^ value) * UINT64_C(1099511628211);
}
int main(void) {
    const unsigned sizes[] = {80, 800, 8000, 8192, 32768};
    unsigned test, round, i, j;
    /* Independent one-instruction oracle for the new shortcut, including
     * every source/destination alias and wrap in an 80-cell core. */
    for (i = 0; i < 80; ++i) {
        for (j = 0; j < 80; ++j) {
            mars_t *m = calloc(1, sizeof(*m));
            insn_t expected[80];
            unsigned k;
            m->nWarriors = 1;
            m->coresize = 80;
            m->processes = 1;
            m->cycles = 1;
            m->maxWarriorLength = 1;
            if (!sim_alloc_bufs(m)) return 2;
            for (k = 0; k < 80; ++k) {
                m->coreMem[k].in = OP(EX_DAT, EX_mF, EX_DIRECT, EX_DIRECT);
                m->coreMem[k].a = k;
                m->coreMem[k].b = 79 - k;
            }
            m->coreMem[i].in = OP(EX_MOV, EX_mI, EX_IMMEDIATE, EX_DIRECT);
            m->coreMem[i].a = (i + j) % 80;
            m->coreMem[i].b = j;
            for (k = 0; k < 80; ++k) expected[k] = m->coreMem[k];
            expected[(i + j) % 80] = m->coreMem[i];
            m->positions[0] = i;
            if (sim_mw(m, m->positions, m->deaths) != 1) return 3;
            for (k = 0; k < 80; ++k) {
                if (m->coreMem[k].in != expected[k].in ||
                    m->coreMem[k].a != expected[k].a ||
                    m->coreMem[k].b != expected[k].b) return 4;
            }
            if (*m->warTab[0].head != &m->coreMem[(i + 1) % 80]) return 5;
            sim_free_bufs(m);
        }
    }
    puts("6400 single-instruction oracle cases passed");
    for (test = 0; test < 18 * 7 * 8 * 8; ++test) {
        mars_t *m = calloc(1, sizeof(*m));
        unsigned n = test;
        unsigned am = n % 8; n /= 8;
        unsigned bm = n % 8; n /= 8;
        unsigned mod = n % 7; n /= 7;
        unsigned op = n;
        m->nWarriors = 1 + test % 4;
        m->coresize = sizes[test % 5];
        m->processes = 1 + test % 33;
        m->cycles = 20 + test % 200;
        m->maxWarriorLength = 16;
        if (!sim_alloc_bufs(m)) return 2;
        for (round = 0; round < 2; ++round) {
            for (i = 0; i < m->coresize; ++i) {
                insn_t *p = &m->coreMem[i];
                p->in = (random32() % 18 << 9) | (random32() % 7 << 6)
                        | (random32() % 8 << 3) | (random32() % 8);
                p->a = random32() % m->coresize;
                p->b = random32() % m->coresize;
            }
            for (i = 0; i < m->nWarriors; ++i) {
                unsigned pos = i * m->coresize / m->nWarriors;
                m->positions[i] = pos;
                m->coreMem[pos].in = (op << 9) | (mod << 6) | (bm << 3) | am;
                /* Frequent zero offsets stress operand self-aliasing. */
                m->coreMem[pos].a = test % 3 ? 0 : m->coresize - 1;
                m->coreMem[pos].b = test % 2 ? 0 : 1;
            }
            int alive = sim_mw(m, m->positions, m->deaths);
            digest(alive);
            for (i = 0; i < m->nWarriors - alive; ++i) digest(m->deaths[i]);
            for (i = 0; i < m->coresize; ++i) {
                digest(m->coreMem[i].in);
                digest(m->coreMem[i].a);
                digest(m->coreMem[i].b);
            }
            for (i = 0; i < m->nWarriors; ++i) {
                digest(m->warTab[i].nprocs);
                digest(m->pspacesOrigin[i]->lastresult);
                for (j = 1; j < m->pspaceSize; ++j)
                    digest(pspace_get(m->pspacesOrigin[i], j));
            }
        }
        sim_free_bufs(m);
    }
    printf("8064 cases, 16128 rounds: %016llx\n", (unsigned long long)hash);
    return 0;
}
