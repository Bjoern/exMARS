/* fighter.c: persistent two-warrior match API for the WebAssembly build.
 *
 * A thin C layer over mars_t, modelled on python_mars.c (Mars_init,
 * Mars_fight) without Python. Warriors are loaded once as 6-int instruction
 * tuples and fought many times on one instance. It links only sim.c and
 * pspace.c, never the pMARS parser in pmars.c, so nothing on this path calls
 * exit() or panic(). The match set-up helpers below are transcribed from
 * pmars.c (compute_positions, check_sanity, set_starting_order, ...) because
 * that file also holds the parser; wasm/test-fighter.mjs checks the results
 * against the exMARS and pMARS command-line engines round for round.
 *
 * Exports (all return int; 0 or a handle >= 0 means success, < 0 an error
 * whose message ex_error() returns):
 *   ex_configure(coreSize, cycles, processes, maxLength, minDistance)
 *   ex_load(insns, count, start, hasPin, pin)     -> handle
 *   ex_unload(handle)
 *   ex_fight(a, b, rounds, fixedPosition, out[3])  -> wins/ties/losses of a
 *   ex_fight_exact(a, b, out[3])
 *   ex_set_early_exit(on)
 *   ex_error()                                     -> const char*
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "exhaust.h"
#include "sim.h"
#include "insn.h"
#include "insn_help.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EX_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EX_EXPORT
#endif

/* Largest -F value pMARS accepts (MAXCORESIZE), as in python_mars.c. */
#define MAX_FIXED_POSITION ((INT_MAX >> 1) + 1)
/* Generous upper bounds; the JS wrapper applies the hill limits. */
#define MAX_CORESIZE 65535
#define MAX_HANDLES 1000000

enum {
    EX_OK = 0,
    EX_E_STATE = -1,     /* not configured */
    EX_E_ARG = -2,       /* invalid argument */
    EX_E_MEMORY = -3,    /* allocation failed */
    EX_E_HANDLE = -4,    /* unknown or unloaded handle */
    EX_E_SIM = -5        /* simulator returned an error */
};

typedef struct {
    int32_t *insns;      /* count * 6 ints, as loaded (values reduced mod coresize) */
    int count;
    int start;
    int have_pin;
    u32_t pin;
} slot_t;

static mars_t *mars = NULL;
static int pspaces_saved = 0;
static int early_exit = 1;
static slot_t *slots = NULL;
static int slot_cap = 0;
static int free_hint = 0;
static char err[MAXALLCHAR];

static int fail(int code, const char *msg)
{
    snprintf(err, sizeof(err), "%s", msg);
    return code;
}

static s32_t ex_rng(s32_t seed)
{
    /* pmars.c rng(), itself pMARS pos.c's generator. */
    s32_t temp = seed;
    temp = (s32_t)((u32_t)temp & 0x7fffffffu);
    if (!temp) temp = 1;
    temp = 16807 * (temp % 127773) - 2836 * (temp / 127773);
    if (temp < 0)
        temp += 2147483647;
    return temp;
}

static void free_slots(void)
{
    int i;
    for (i = 0; i < slot_cap; i++) free(slots[i].insns);
    free(slots);
    slots = NULL;
    slot_cap = 0;
    free_hint = 0;
}

static void free_mars(void)
{
    if (mars) sim_free_bufs(mars);   /* also frees mars itself */
    mars = NULL;
    pspaces_saved = 0;
}

EX_EXPORT const char *ex_error(void)
{
    return err;
}

EX_EXPORT int ex_set_early_exit(int on)
{
    early_exit = on ? 1 : 0;
    if (mars) mars->earlyExit = early_exit;
    err[0] = '\0';
    return EX_OK;
}

EX_EXPORT int ex_configure(int coreSize, int cycles, int processes, int maxLength, int minDistance)
{
    const char *fixed_error;
    mars_t *m;

    free_mars();
    free_slots();
    err[0] = '\0';
    if (coreSize < 1 || coreSize > MAX_CORESIZE || cycles < 1 || processes < 1 ||
        maxLength < 1 || minDistance < 1 || maxLength > minDistance ||
        (long)minDistance * 2 > coreSize)
        return fail(EX_E_ARG, "invalid hill settings");
    /* The queue holds nWarriors * processes + 1 pointers. */
    if ((unsigned long)processes > (0x7fffffffUL / sizeof(insn_t *) - 1) / 2)
        return fail(EX_E_ARG, "too many processes");

    m = (mars_t *)calloc(1, sizeof(mars_t));
    if (!m) return fail(EX_E_MEMORY, "failed to allocate mars_t");
    m->earlyExit = early_exit;
    m->seed = ex_rng((s32_t)time(0) * 0x1d872b41);
    m->rounds = 1;
    m->cycles = cycles;
    m->coresize = coreSize;
    m->processes = processes;
    m->maxWarriorLength = maxLength;
    m->minsep = minDistance;
    m->nWarriors = 2;
    m->errorcode = SUCCESS;
    m->errorlevel = WARNING;

    fixed_error = sim_fixed_settings_error(m);
    if (fixed_error) {
        free(m);
        return fail(EX_E_ARG, fixed_error);
    }
    if (!sim_alloc_bufs(m)) {
        /* sim_alloc_bufs can leave pointers uninitialised on a partial
         * failure, so sim_free_bufs is unsafe here: leak instead. The
         * instance stays usable with smaller settings. */
        return fail(EX_E_MEMORY, "sim_alloc_bufs failed (settings too large?)");
    }
    m->errmsg[0] = '\0';
    mars = m;
    return EX_OK;
}

EX_EXPORT int ex_load(const int32_t *insns, int count, int start, int hasPin, int pin)
{
    int i, h;
    int32_t *copy;

    err[0] = '\0';
    if (!mars) return fail(EX_E_STATE, "not configured");
    if (!insns || count < 1 || (u32_t)count > mars->maxWarriorLength)
        return fail(EX_E_ARG, "instruction count must be 1..maxLength");
    if (start < 0 || start >= count)
        return fail(EX_E_ARG, "start must be 0..count-1");
    for (i = 0; i < count; i++) {
        const int32_t *t = insns + 6 * i;
        if (t[0] < EX_DAT || t[0] > EX_STP) return fail(EX_E_ARG, "opcode out of range");
        if (t[1] < EX_mF || t[1] > EX_mI) return fail(EX_E_ARG, "modifier out of range");
        if (t[2] < EX_DIRECT || t[2] > EX_APOSTINC || t[4] < EX_DIRECT || t[4] > EX_APOSTINC)
            return fail(EX_E_ARG, "addressing mode out of range");
    }

    for (h = free_hint; h < slot_cap && slots[h].insns; h++)
        ;
    if (h == slot_cap) {
        int cap = slot_cap ? slot_cap * 2 : 64;
        slot_t *grown;
        if (cap > MAX_HANDLES) cap = MAX_HANDLES;
        if (cap <= slot_cap) return fail(EX_E_MEMORY, "too many loaded warriors");
        grown = (slot_t *)realloc(slots, sizeof(slot_t) * cap);
        if (!grown) return fail(EX_E_MEMORY, "failed to grow the warrior table");
        memset(grown + slot_cap, 0, sizeof(slot_t) * (cap - slot_cap));
        slots = grown;
        slot_cap = cap;
    }
    copy = (int32_t *)malloc(sizeof(int32_t) * 6 * count);
    if (!copy) return fail(EX_E_MEMORY, "failed to allocate warrior");
    for (i = 0; i < count; i++) {
        const int32_t *t = insns + 6 * i;
        int32_t *c = copy + 6 * i;
        c[0] = t[0]; c[1] = t[1]; c[2] = t[2];
        c[3] = MODS(t[3], mars->coresize);
        c[4] = t[4];
        c[5] = MODS(t[5], mars->coresize);
    }
    slots[h].insns = copy;
    slots[h].count = count;
    slots[h].start = start;
    slots[h].have_pin = hasPin ? 1 : 0;
    slots[h].pin = (u32_t)pin;
    free_hint = h + 1;
    return h;
}

EX_EXPORT int ex_unload(int handle)
{
    err[0] = '\0';
    if (handle < 0 || handle >= slot_cap || !slots[handle].insns)
        return fail(EX_E_HANDLE, "unknown handle");
    free(slots[handle].insns);
    memset(&slots[handle], 0, sizeof(slot_t));
    if (handle < free_hint) free_hint = handle;
    return EX_OK;
}

/* --- transcribed from pmars.c (two-warrior paths only) --------------- */

static s32_t compute_positions2(s32_t seed)
{
    u32_t avail = mars->coresize + 1 - mars->nWarriors * mars->minsep;
    mars->positions[0] = 0;
    mars->positions[1] = (field_t)(mars->minsep + seed % avail);
    return ex_rng(seed);
}

static void load_warriors2(void)
{
    unsigned int i;
    for (i = 0; i < mars->nWarriors; i++)
        sim_load_warrior(mars, mars->positions[i], mars->warriors[i].code, mars->warriors[i].len);
}

static void set_starting_order2(unsigned int round)
{
    unsigned int i;
    for (i = 0; i < mars->nWarriors; i++) {
        unsigned int j = (i + round) % (mars->nWarriors);
        mars->startPositions[i] = (field_t)((mars->positions[j] + mars->warriors[j].start) % (mars->coresize));
    }
    for (i = 0; i < mars->nWarriors; i++)
        mars->pspacesOrigin[i] = mars->pspaces[(i + round) % (mars->nWarriors)];
}

static void accumulate_results2(void)
{
    unsigned int i;
    for (i = 0; i < mars->nWarriors; i++) {
        unsigned int result = pspace_get(mars->pspaces[i], 0);
        mars->results[i * (mars->nWarriors + 1) + result]++;
    }
}

static void amalgamate_pspaces2(void)
{
    unsigned int i, j;
    for (i = 0; i < mars->nWarriors; i++)
        if (mars->warriors[i].have_pin)
            for (j = 0; j < i; j++)
                if (mars->warriors[j].have_pin && mars->warriors[j].pin == mars->warriors[i].pin)
                    pspace_share(mars->pspaces[i], mars->pspaces[j]);
}

/* check_sanity() without panic(): ex_load already enforces 1..maxLength
 * instructions, maxLength <= minDistance and 2 * minDistance <= coreSize. */
static int check_sanity2(void)
{
    unsigned int i;
    for (i = 0; i < mars->nWarriors; i++) {
        if (mars->warriors[i].len == 0) return fail(EX_E_ARG, "warrior has no code");
        if (mars->minsep < mars->warriors[i].len)
            return fail(EX_E_ARG, "minimum separation must be >= longest warrior");
    }
    if (mars->nWarriors * mars->minsep > mars->coresize)
        return fail(EX_E_ARG, "warriors too large to fit into core");
    return EX_OK;
}

/* --------------------------------------------------------------------- */

static void copy_in(int index, const slot_t *s)
{
    warrior_t *w = &mars->warriors[index];
    insn_t *in = w->code;
    int j;
    w->start = (u32_t)s->start;
    w->len = (u32_t)s->count;
    w->have_pin = s->have_pin;
    w->pin = s->pin;
    for (j = 0; j < s->count; j++, in++) {
        const int32_t *t = s->insns + 6 * j;
        int flags = j == s->start ? fl_START : 0;
        in->a = (field_t)t[3];
        in->b = (field_t)t[5];
        in->in = (flags << flPOS) | OP(t[0], t[1], t[2], t[4]);
    }
}

/* One match, exactly as python_mars.c Mars_fight(). */
static int match(int a, int b, u32_t rounds, int fixedPosition, int32_t *out)
{
    u32_t i;
    s32_t seed;
    int status;

    copy_in(0, &slots[a]);
    copy_in(1, &slots[b]);
    status = check_sanity2();
    if (status) return status;
    memset(mars->results, 0, sizeof(u32_t) * mars->nWarriors * (mars->nWarriors + 1));
    mars->rounds = rounds;
    mars->fixedPosition = fixedPosition;
    mars->earlyExit = early_exit;

    if (mars->fixedPosition)
        seed = mars->fixedPosition - mars->minsep;
    else
        seed = ex_rng(mars->seed);

    /* Fresh P-space per match (see Mars_fight): undo the previous match's
     * per-round rotation, drop PIN sharing, clear, then share by PIN. */
    if (pspaces_saved)
        memcpy(mars->pspacesOrigin, mars->pspaces, sizeof(pspace_t *) * mars->nWarriors);
    sim_reset_pspaces(mars);
    memcpy(mars->pspaces, mars->pspacesOrigin, sizeof(pspace_t *) * mars->nWarriors);
    pspaces_saved = 1;
    amalgamate_pspaces2();

    for (i = 0; i < rounds; i++) {
        int nalive;
        sim_clear_core(mars);
        seed = compute_positions2(seed);
        load_warriors2();
        set_starting_order2(i);
        nalive = sim_mw(mars, mars->startPositions, mars->deaths);
        if (nalive < 0) {
            mars->seed = seed;
            return fail(EX_E_SIM, mars->errmsg[0] ? mars->errmsg : "simulator error");
        }
        accumulate_results2();
    }
    mars->seed = seed;
    out[0] += (int32_t)mars->results[0 * 3 + 1];
    out[1] += (int32_t)mars->results[0 * 3 + 2];
    out[2] += (int32_t)mars->results[0 * 3 + 0];
    return EX_OK;
}

static int check_pair(int a, int b, int32_t *out)
{
    if (!mars) return fail(EX_E_STATE, "not configured");
    if (!out) return fail(EX_E_ARG, "missing result buffer");
    if (a < 0 || a >= slot_cap || !slots[a].insns || b < 0 || b >= slot_cap || !slots[b].insns)
        return fail(EX_E_HANDLE, "unknown handle");
    return EX_OK;
}

EX_EXPORT int ex_fight(int a, int b, int rounds, int fixedPosition, int32_t *out)
{
    int status;
    err[0] = '\0';
    status = check_pair(a, b, out);
    if (status) return status;
    if (rounds < 1) return fail(EX_E_ARG, "rounds must be >= 1");
    if (fixedPosition != 0 && (fixedPosition < (int)mars->minsep || fixedPosition > MAX_FIXED_POSITION))
        return fail(EX_E_ARG, "fixedPosition must be 0 or minDistance..MAXCORESIZE, like pMARS -F");
    out[0] = out[1] = out[2] = 0;
    return match(a, b, (u32_t)rounds, fixedPosition, out);
}

/* cwopt's exact_pair(): every legal offset p of b relative to a, one round
 * with a starting (a at 0, b at p) and one with b starting (b at 0, a at
 * coreSize - p, the same relative placement). Each is a fresh match. */
EX_EXPORT int ex_fight_exact(int a, int b, int32_t *out)
{
    int status, p;
    int32_t back[3];
    int c, d;
    err[0] = '\0';
    status = check_pair(a, b, out);
    if (status) return status;
    c = (int)mars->coresize;
    d = (int)mars->minsep;
    out[0] = out[1] = out[2] = 0;
    for (p = d; p <= c - d; p++) {
        status = match(a, b, 1, p, out);
        if (status) return status;
        back[0] = back[1] = back[2] = 0;
        status = match(b, a, 1, (c - p) % c, back);
        if (status) return status;
        out[0] += back[2];   /* b's losses are a's wins */
        out[1] += back[1];
        out[2] += back[0];
    }
    return EX_OK;
}
