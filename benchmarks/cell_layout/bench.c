/* Include the existing assembler, with its CLI entry point renamed. */
#define main exmars_cli_main
#include "pmars.c"
#undef main
#include <stdint.h>
#include <time.h>

static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static uint64_t digest = UINT64_C(14695981039346656037);
static void hash(unsigned v) { digest = (digest ^ v) * UINT64_C(1099511628211); }

int main(int argc, char **argv) {
    if (argc < 7) return 2;
    unsigned core = atoi(argv[1]), early = atoi(argv[2]), repeats = atoi(argv[3]);
    unsigned count = argc - 4, rounds = core == 80 ? 142 : 32;
    if (!repeats || (core != 80 && core != 8000)) return 2;
    mars_t *m = calloc(1, sizeof(*m));
    m->nWarriors = 2; m->coresize = core; m->cycles = core * 10;
    m->processes = core; m->maxWarriorLength = core == 80 ? 5 : 100;
    m->minsep = m->maxWarriorLength; m->earlyExit = early;
    m->errorlevel = WARNING;
    if (!sim_alloc_bufs(m)) return 3;
    save_pspaces(m);
    warrior_struct **parsed = calloc(count, sizeof(*parsed));
    for (unsigned i = 0; i < count; ++i) {
        parsed[i] = calloc(1, sizeof(**parsed));
        if (assemble_warrior(m, argv[i + 4], parsed[i])) return 4;
    }
#ifdef PACKED_BITS
    /* Fail on compilers whose bitfield ABI does not match scalar shifts. */
    insn_t probe = {0};
    probe.a = 37; probe.b = 71; probe.in = 0xa563;
    if (cell_a(&probe) != 37 || cell_b(&probe) != 71 || cell_in(&probe) != 0xa563) return 5;
#endif
    double elapsed = 0;
    unsigned battles = 0;
    for (unsigned rep = 0; rep < repeats; ++rep)
    for (unsigned i = 0; i < count; ++i)
    for (unsigned j = i + 1; j < count; ++j)
    for (unsigned order = 0; order < 2; ++order) {
        warrior_struct *pair[2] = {parsed[order ? j : i], parsed[order ? i : j]};
        pmars2exhaust(m, pair, 2);
        sim_reset_pspaces(m);
        amalgamate_pspaces(m);
        double start = seconds();
        for (unsigned r = 0; r < rounds / 2; ++r) {
            unsigned pos = core == 80 ? 5 + r : 100 + (r * 487 + 3900) % 7801;
            field_t starts[2] = {m->warriors[0].start, (pos + m->warriors[1].start) % core};
            u32_t deaths[2] = {0, 0};
            sim_clear_core(m);
            sim_load_warrior(m, 0, m->warriors[0].code, m->warriors[0].len);
            sim_load_warrior(m, pos, m->warriors[1].code, m->warriors[1].len);
            int alive = sim_mw(m, starts, deaths);
            if (alive < 1 || alive > 2) return 6;
            hash(alive); if (alive == 1) hash(deaths[0]);
            ++battles;
        }
        elapsed += seconds() - start;
        for (unsigned w = 0; w < 2; ++w)
            for (unsigned p = 0; p < m->pspaceSize; ++p) hash(pspace_get(m->pspacesOrigin[w], p));
    }
    printf("{\"seconds\":%.9f,\"battles\":%u,\"cell_bytes\":%zu,\"digest\":\"%016llx\"}\n",
           elapsed, battles, sizeof(insn_t), (unsigned long long)digest);
    sim_free_bufs(m);
    /* Assembler allocations have process lifetime, outside measured regions. */
    return 0;
}
