/* fighter.c: persistent match API for the WebAssembly build.
 *
 * A thin C layer over mars_t, modelled on python_mars.c (Mars_init,
 * Mars_fight) without Python. Warriors are loaded once as 6-int instruction
 * tuples and fought many times on one instance. It links only sim.c,
 * pspace.c and match.c (the rounds of a match, shared with the CLI and the
 * other bindings), never the pMARS parser in pmars.c, so nothing on this
 * path calls exit() or panic(). wasm/test-fighter.mjs checks the results
 * against the exMARS and pMARS command-line engines round for round.
 *
 * Exports (all return int; 0 or a handle >= 0 means success, < 0 an error
 * whose message ex_error() returns):
 *   ex_configure(coreSize, cycles, processes, maxLength, minDistance)
 *   ex_load(insns, count, start, hasPin, pin)     -> handle
 *   ex_unload(handle)
 *   ex_fight(a, b, rounds, fixedPosition, out[3])  -> wins/ties/losses of a
 *   ex_fight_exact(a, b, fixedPosition, out[3])   pMARS -P: every placement
 *       in both starting orders once, one match (P-space kept between rounds)
 *   ex_fight_many(handles, n, rounds, fixedPosition, out[n * (n + 1)])
 *       -> per warrior i: out[i*(n+1)] rounds dead, out[i*(n+1)+k] rounds
 *          alive with k survivors (k = 1..n), as mars->results
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
#include "match.h"

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
/* pMARS's MAXWARRIOR in its default (non-DOS) builds. */
#define MAX_WARRIORS 36

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

typedef struct {
    int coreSize, cycles, processes, maxLength, minDistance;
} hill_t;

/* One simulator per warrior count, created on first use; [2] always exists
 * once configured. */
static mars_t *mars_n[MAX_WARRIORS + 1];
static hill_t hill;
static int configured = 0;
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
    int n;
    for (n = 0; n <= MAX_WARRIORS; n++) {
        if (mars_n[n]) sim_free_bufs(mars_n[n]);   /* also frees the mars_t */
        mars_n[n] = NULL;
    }
    configured = 0;
}

/* Allocates the simulator for n warriors, as Mars_init(n, ...). */
static mars_t *new_mars(int n)
{
    const char *fixed_error;
    mars_t *m = (mars_t *)calloc(1, sizeof(mars_t));
    if (!m) { fail(EX_E_MEMORY, "failed to allocate mars_t"); return NULL; }
    m->earlyExit = early_exit;
    m->seed = rng((s32_t)time(0) * 0x1d872b41);
    m->rounds = 1;
    m->cycles = hill.cycles;
    m->coresize = hill.coreSize;
    m->processes = hill.processes;
    m->maxWarriorLength = hill.maxLength;
    m->minsep = hill.minDistance;
    m->nWarriors = n;
    m->errorcode = SUCCESS;
    m->errorlevel = WARNING;
    fixed_error = sim_fixed_settings_error(m);
    if (fixed_error) {
        free(m);
        fail(EX_E_ARG, fixed_error);
        return NULL;
    }
    if (!sim_alloc_bufs(m)) {
        /* sim_alloc_bufs can leave pointers uninitialised on a partial
         * failure, so sim_free_bufs is unsafe here: leak instead. */
        fail(EX_E_MEMORY, "sim_alloc_bufs failed (settings too large?)");
        return NULL;
    }
    m->errmsg[0] = '\0';
    return m;
}

static mars_t *get_mars(int n)
{
    if (!mars_n[n]) mars_n[n] = new_mars(n);
    return mars_n[n];
}

EX_EXPORT const char *ex_error(void)
{
    return err;
}

EX_EXPORT int ex_set_early_exit(int on)
{
    int n;
    early_exit = on ? 1 : 0;
    for (n = 0; n <= MAX_WARRIORS; n++)
        if (mars_n[n]) mars_n[n]->earlyExit = early_exit;
    err[0] = '\0';
    return EX_OK;
}

EX_EXPORT int ex_configure(int coreSize, int cycles, int processes, int maxLength, int minDistance)
{
    free_mars();
    free_slots();
    err[0] = '\0';
    if (coreSize < 1 || coreSize > MAX_CORESIZE || cycles < 1 || processes < 1 ||
        maxLength < 1 || minDistance < 1 || maxLength > minDistance ||
        (long)minDistance * 2 > coreSize)
        return fail(EX_E_ARG, "invalid hill settings");
    /* The queue holds nWarriors * processes + 1 pointers. */
    if ((unsigned long)processes > (0x7fffffffUL / sizeof(insn_t *) - 1) / MAX_WARRIORS)
        return fail(EX_E_ARG, "too many processes");
    hill.coreSize = coreSize;
    hill.cycles = cycles;
    hill.processes = processes;
    hill.maxLength = maxLength;
    hill.minDistance = minDistance;
    if (!get_mars(2)) return err[0] == '\0' ? fail(EX_E_MEMORY, "allocation failed") : EX_E_MEMORY;
    configured = 1;
    return EX_OK;
}

EX_EXPORT int ex_load(const int32_t *insns, int count, int start, int hasPin, int pin)
{
    int i, h;
    int32_t *copy;

    err[0] = '\0';
    if (!configured) return fail(EX_E_STATE, "not configured");
    if (!insns || count < 1 || count > hill.maxLength)
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
        c[3] = MODS(t[3], hill.coreSize);
        c[4] = t[4];
        c[5] = MODS(t[5], hill.coreSize);
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

static void copy_in(mars_t *m, int index, const slot_t *s)
{
    warrior_t *w = &m->warriors[index];
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

/* One match of n = len(handles) warriors (match.c, as python_mars.c
 * Mars_fight()); the histogram is left in m->results. */
static int match(int n, const int *handles, u32_t rounds, int fixedPosition, int permutate)
{
    mars_t *m = get_mars(n);
    int status, k;
    size_t len;

    if (!m) return EX_E_MEMORY;
    for (k = 0; k < n; k++) copy_in(m, k, &slots[handles[k]]);
    m->rounds = permutate ? mars_permutations(m) : rounds;
    m->fixedPosition = fixedPosition;
    m->earlyExit = early_exit;
    m->permutate = permutate;
    m->errmsg[0] = '\0';
    status = mars_match(m);
    if (status == MATCH_OK) return EX_OK;
    len = strlen(m->errmsg);
    while (len && m->errmsg[len - 1] == '\n') m->errmsg[--len] = '\0';
    return fail(status == MATCH_E_ARG ? EX_E_ARG : status == MATCH_E_MEMORY ? EX_E_MEMORY : EX_E_SIM,
                len ? m->errmsg : "simulator error");
}

static int check_handle(int h)
{
    return h >= 0 && h < slot_cap && slots[h].insns;
}

static int check_pair(int a, int b, int32_t *out)
{
    if (!configured) return fail(EX_E_STATE, "not configured");
    if (!out) return fail(EX_E_ARG, "missing result buffer");
    if (!check_handle(a) || !check_handle(b)) return fail(EX_E_HANDLE, "unknown handle");
    return EX_OK;
}

static int check_position(int fixedPosition)
{
    if (fixedPosition != 0 && (fixedPosition < hill.minDistance || fixedPosition > MAX_FIXED_POSITION))
        return fail(EX_E_ARG, "fixedPosition must be 0 or minDistance..MAXCORESIZE, like pMARS -F");
    return EX_OK;
}

/* Adds warrior 0's wins/ties/losses of the last 2-warrior match to out. */
static void add_pair_result(int32_t *out)
{
    const u32_t *r = mars_n[2]->results;
    out[0] += (int32_t)r[0 * 3 + 1];
    out[1] += (int32_t)r[0 * 3 + 2];
    out[2] += (int32_t)r[0 * 3 + 0];
}

EX_EXPORT int ex_fight(int a, int b, int rounds, int fixedPosition, int32_t *out)
{
    int status, pair[2];
    err[0] = '\0';
    status = check_pair(a, b, out);
    if (status) return status;
    if (rounds < 1) return fail(EX_E_ARG, "rounds must be >= 1");
    status = check_position(fixedPosition);
    if (status) return status;
    out[0] = out[1] = out[2] = 0;
    pair[0] = a; pair[1] = b;
    status = match(2, pair, (u32_t)rounds, fixedPosition, 0);
    if (status) return status;
    add_pair_result(out);
    return EX_OK;
}

/* pMARS -P: one match of 2 * (coreSize - 2 * minDistance + 1) rounds, each
 * a not yet used (position, starting warrior) pair, so every placement in
 * both starting orders once; P-space persists between the rounds as in
 * pMARS. fixedPosition seeds the order like -F (0 = random); results differ
 * between orders only for warriors that use P-space. */
EX_EXPORT int ex_fight_exact(int a, int b, int fixedPosition, int32_t *out)
{
    int status, pair[2];
    err[0] = '\0';
    status = check_pair(a, b, out);
    if (status) return status;
    status = check_position(fixedPosition);
    if (status) return status;
    out[0] = out[1] = out[2] = 0;
    pair[0] = a; pair[1] = b;
    status = match(2, pair, 0, fixedPosition, 1);
    if (status) return status;
    add_pair_result(out);
    return EX_OK;
}

/* One match of n warriors (2..36, repeats allowed) in input order. */
EX_EXPORT int ex_fight_many(const int *handles, int n, int rounds, int fixedPosition, int32_t *out)
{
    int status, k;
    mars_t *m;
    err[0] = '\0';
    if (!configured) return fail(EX_E_STATE, "not configured");
    if (!handles || !out) return fail(EX_E_ARG, "missing buffer");
    if (n < 2 || n > MAX_WARRIORS) return fail(EX_E_ARG, "warrior count must be 2..36");
    if ((long)n * hill.minDistance > hill.coreSize)
        return fail(EX_E_ARG, "warriors * minDistance exceeds coreSize");
    for (k = 0; k < n; k++)
        if (!check_handle(handles[k])) return fail(EX_E_HANDLE, "unknown handle");
    if (rounds < 1) return fail(EX_E_ARG, "rounds must be >= 1");
    status = check_position(fixedPosition);
    if (status) return status;
    status = match(n, handles, (u32_t)rounds, fixedPosition, 0);
    if (status) return status;
    m = mars_n[n];
    for (k = 0; k < n * (n + 1); k++) out[k] = (int32_t)m->results[k];
    return EX_OK;
}
