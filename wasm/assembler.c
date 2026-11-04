/* assembler.c: in-memory assembly entry point for the WebAssembly build.
 *
 * Linked with pmars.c (GPL-2.0-or-later parser), sim.c and pspace.c into
 * assembler.mjs. The pMARS parser can exit() (too many errors, out of memory,
 * logic errors), so the JS wrapper runs each call in a fresh, throwaway
 * instance and never in the persistent fighter. Diagnostics are printed to
 * stderr by the parser; the wrapper captures them.
 *
 * ex_assemble(source, coreSize, cycles, processes, maxLength, minDistance, rounds)
 * returns 0 when the source assembled into a loadable warrior, 1 when the
 * parser reported errors or the result can't be fought, < 0 for bad settings.
 * ex_result() then points at int32 data:
 *   [0] count  [1] start  [2] hasPin  [3] pin  [4] errors  [5] warnings
 *   [6...] count * (opcode, modifier, aMode, aValue, bMode, bValue)
 * using the numbering of Warrior.py / insn.h (the Python binding's tuples,
 * values as the parser produced them; the fighter reduces them mod coreSize).
 * ex_name(), ex_author() and ex_message() return strings.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define PIN_APPEARED -2   /* pmars.c's tag for a warrior that declared PIN */

extern int assemble_warrior_from_string(mars_t *mars, const char *redcode_string, warrior_struct *w);
extern enum ex_op p2eOp[];
extern enum ex_modifier p2eModifier[];
extern enum ex_addr_mode p2eAddr[];
extern s32_t rng(s32_t seed);

static int32_t *result = NULL;
static char *name = NULL, *author = NULL;
static char message[MAXALLCHAR];

EX_EXPORT int32_t *ex_result(void) { return result; }
EX_EXPORT const char *ex_name(void) { return name ? name : ""; }
EX_EXPORT const char *ex_author(void) { return author ? author : ""; }
EX_EXPORT const char *ex_message(void) { return message; }

static char *copy_string(const char *s)
{
    size_t n = strlen(s ? s : "");
    char *c = (char *)malloc(n + 1);
    if (c) memcpy(c, s ? s : "", n + 1);
    return c;
}

EX_EXPORT int ex_assemble(const char *source, int coreSize, int cycles, int processes,
                          int maxLength, int minDistance, int rounds)
{
    mars_t *mars;
    warrior_struct *w;
    int i, status = 0;

    message[0] = '\0';
    if (!source || coreSize < 1 || coreSize > 65535 || cycles < 1 || processes < 1 ||
        maxLength < 1 || minDistance < 1 || maxLength > minDistance ||
        (long)minDistance * 2 > coreSize || rounds < 1) {
        snprintf(message, sizeof(message), "invalid hill settings");
        return -1;
    }
    /* Configured as python_mars.c Mars_init(2, ...), so CORESIZE, MAXLENGTH,
     * PSPACESIZE, ... and ;assert evaluate as they do for the hill. */
    mars = (mars_t *)calloc(1, sizeof(mars_t));
    w = (warrior_struct *)calloc(1, sizeof(warrior_struct));
    if (!mars || !w) {
        snprintf(message, sizeof(message), "out of memory");
        return -2;
    }
    mars->earlyExit = 1;
    mars->seed = rng(1);
    mars->rounds = rounds;
    mars->cycles = cycles;
    mars->coresize = coreSize;
    mars->processes = processes;
    mars->maxWarriorLength = maxLength;
    mars->minsep = minDistance;
    mars->nWarriors = 2;
    mars->errorcode = SUCCESS;
    mars->errorlevel = WARNING;
    if (!sim_alloc_bufs(mars)) {
        snprintf(message, sizeof(message), "sim_alloc_bufs failed");
        return -2;
    }

    if (assemble_warrior_from_string(mars, source, w)) {
        snprintf(message, sizeof(message), "%s", mars->errmsg);
        status = 1;
    } else if (w->instLen < 1) {
        snprintf(message, sizeof(message), "No instructions");
        status = 1;
    } else if (w->instLen > maxLength) {
        snprintf(message, sizeof(message), "Too many instructions");
        status = 1;
    } else if (w->offset < 0 || w->offset >= w->instLen) {
        snprintf(message, sizeof(message), "Execution starts from outside of the program");
        status = 1;
    }

    result = (int32_t *)calloc(6 + 6 * (size_t)(w->instLen > 0 ? w->instLen : 0), sizeof(int32_t));
    if (!result) {
        snprintf(message, sizeof(message), "out of memory");
        return -2;
    }
    result[0] = w->instBank ? w->instLen : 0;
    result[1] = w->offset;
    result[2] = w->pSpaceIndex == PIN_APPEARED;
    result[3] = result[2] ? (int32_t)w->pSpaceIDNumber : 0;
    result[4] = mars->errnum;
    result[5] = mars->warnum;
    for (i = 0; i < result[0]; i++) {
        /* As pmars2exhaust() and the Python binding (CMP becomes SEQ). */
        mem_struct *cell = w->instBank + i;
        FIELD_T opcode = ((FIELD_T)(cell->opcode & 0xf8)) >> 3;
        FIELD_T modifier = (cell->opcode & 0x07);
        int32_t *t = result + 6 + 6 * i;
        t[0] = p2eOp[opcode];
        t[1] = p2eModifier[modifier];
        t[2] = PM_INDIR_A(cell->A_mode) ? p2eAddr[INDIR_A_TO_SYM(cell->A_mode)] : p2eAddr[cell->A_mode];
        t[3] = cell->A_value;
        t[4] = PM_INDIR_A(cell->B_mode) ? p2eAddr[INDIR_A_TO_SYM(cell->B_mode)] : p2eAddr[cell->B_mode];
        t[5] = cell->B_value;
    }
    name = copy_string(w->name);
    author = copy_string(w->authorName);
    return status;
}
