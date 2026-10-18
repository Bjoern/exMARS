/* sim.c: simulator functions
 * $Id: sim.c,v 1.3 2003/08/19 15:59:53 martinus Exp $
 */

/* This file is part of `exhaust', a memory array redcode simulator.
 * Copyright (C) 2002 M Joonas Pihlaja
 * Public Domain.
 */

/*
 * Thanks go to the pMARS authors and Ken Espiritu whose ideas have
 * been used in this simulator.  Especially Ken's effective addressing
 * calculation code in pMARS 0.8.6 has been adapted for use here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust.h"
#include "sim.h"
#include "insn.h"

/* Should we strip flags from instructions when loading?  By default,
   yes.  If so, then the simulator won't bother masking them off.  */
#ifndef SIM_STRIP_FLAGS
#define SIM_STRIP_FLAGS 1
#endif


/* DEBUG level:
 *     0: none
 *   >=1: disassemble each instruction (no output)
 *     2: print each instruction as it is executed
 */

/*
 * File scoped stuff
 */


#define DEF_MAX_WARS 2
#define DEF_CORESIZE 8000
#define DEF_PROCESSES 8000
#define DEF_CYCLES 80000

/*static unsigned int modes[8] = {0, 0, 0, 0, 0, 0, 0, 0};*/

/* protos */
static int sim_proper(mars_t* mars, const field_t * const war_pos_tab, u32_t *death_tab);

/*---------------------------------------------------------------
 * Simulator memory management
 */

void
sim_clear_core(mars_t* mars)
{
    memset(mars->coreMem, 0, mars->coresize*sizeof(insn_t));
}

/* NAME
 *     sim_alloc_bufs, sim_alloc_bufs2, sim_free_bufs --
 *              alloc and free buffers used in simulation
 * 
 * SYNOPSIS
 *     insn_t *sim_alloc_bufs( unsigned int nwars, unsigned int coresize,
 *                             unsigned int processes, unsigned int cycles );
 *     insn_t *sim_alloc_bufs2( unsigned int nwars, unsigned int coresize,
 *              unsigned int processes, unsigned int cycles,
 *              unsigned int pspacesize );
 *     void sim_free_bufs();
 * 
 * INPUTS
 *     nwar        -- number of warriors
 *     coresize    -- size of core
 *     processes   -- max no of processes / warrior
 *     cycles      -- the number of cycles to play before tie.
 *     pspacesize  -- size of p-space per warrior.  For sim_alloc_bufs(),
 *            it defaults to min(1,coresize/16).
 * 
 * RESULTS
 *     These functions manage the core, queue, and w_t warrior info
 *     struct array memories.  
 *
 *     Core_Mem, Queue_Mem, War_Tab and PSpace_mem memories are allocated
 *     or freed as requested.  Any earlier memories are freed before
 *     new allocations are made.
 * 
 * RETURN VALUE
 *     sim_alloc_bufs(): the address of core memory, or NULL if
 *           out of memory.
 *     sim_free_bufs(): none
 *
 * GLOBALS
 *     All file scoped globals.
 */

void sim_free_bufs(void* m)
{
    mars_t* mars = (mars_t*)m;
    u32_t i;
    if (mars->pspacesOrigin) {
        for (i=0; i<mars->nWarriors; ++i) {
            pspace_free(mars->pspacesOrigin[i]);
        }
    }
    for (i=0; i<mars->nWarriors; ++i) {
        free(mars->warriors[i].code);
    }
    free(mars->coreMem);
    free(mars->deaths);
    free(mars->positions);
    free(mars->pspaces);
    free(mars->pspacesOrigin);
    free(mars->queueMem);
    free(mars->results);
    free(mars->startPositions);
    free(mars->warriors);
    free(mars->warTab);
    free(mars);
}


/* allocate everything needed by simulator */
int sim_alloc_bufs(mars_t* mars) {
    const char *fixed_error = sim_fixed_settings_error(mars);
    if (fixed_error) {
        snprintf(mars->errmsg, sizeof(mars->errmsg), "%s", fixed_error);
        return 0;
    }
    if ((mars->warriors = (warrior_t*)malloc(sizeof(warrior_t)*mars->nWarriors))) {
        u32_t n;
        for (n=0; n<mars->nWarriors; ++n) {
            if (!(mars->warriors[n].code = (insn_t*)malloc(sizeof(insn_t)*mars->maxWarriorLength))) {
                return 0;
            }
        }
    }
    else return 0;

    mars->positions = (field_t*)malloc(sizeof(field_t)*mars->nWarriors);
    mars->startPositions = (field_t*)malloc(sizeof(field_t)*mars->nWarriors);
    mars->deaths = (u32_t*)malloc(sizeof(unsigned int)*mars->nWarriors);
    mars->results = (u32_t*)malloc(sizeof(u32_t)*mars->nWarriors*(mars->nWarriors+1));

    mars->pspaceSize = mars->coresize/16;
    if (mars->pspaceSize <= 0) mars->pspaceSize = 1;

    mars->coreMem = (insn_t*)malloc(sizeof(insn_t) * mars->coresize);
    mars->queueMem = (insn_t**)malloc(sizeof(insn_t*)*(mars->nWarriors * mars->processes + 1));
    mars->warTab = (w_t*)malloc(sizeof(w_t)*mars->nWarriors);
    mars->pspaces = (pspace_t**)malloc(sizeof(pspace_t*)*mars->nWarriors);
    if ((mars->pspacesOrigin = (pspace_t**)malloc(sizeof(pspace_t*)*mars->nWarriors))) {
        u32_t i;
        memset(mars->pspacesOrigin, 0, sizeof(pspace_t*)*mars->nWarriors);
        for (i=0; i<mars->nWarriors; ++i) {
            if (!(mars->pspacesOrigin[i] = pspace_alloc(mars->pspaceSize))) return 0;
        }
        sim_clear_pspaces(mars);
    }
    else return 0;

    
    
    return (mars->warriors
            && mars->positions
            && mars->startPositions
            && mars->deaths
            && mars->results
            && mars->pspaces
            && mars->pspacesOrigin
            && mars->coreMem
            && mars->queueMem
            && mars->warTab);
}


/* NAME
 *     sim_load_warrior -- load warrior code into core.
 * 
 * SYNOPSIS
 *     sim_load_warrior(unsigned int pos, insn_t *code, unsigned int len);
 * 
 * INPUTS
 *     pos -- The core address to load the warrior into.
 *     code -- array of instructions.
 *     len -- 
 * 
 * DESCRIPTION
 *     This function is the preferred method to load warriors into core.
 *     It strips the instructions of any flags they may possess, and
 *     copies the instructions into core.
 * 
 *     The code will happily overwrite any previous contents of core,
 *     so beware not to load warriors so that their code overlaps.
 * 
 * NOTE
 *     The core must have been allocated previously with sim(), or
 *     preferably sim_alloc_bufs() and not freed since.
 * 
 * RETURN VALUE
 *     0 -- warrior loaded OK.
 *    -1 -- core memory not allocated.
 *    -2 -- warrior length > core size.
 */
int
sim_load_warrior(mars_t* mars, unsigned int pos, const insn_t * const code, unsigned int len)
{
    unsigned int i;
    field_t k;
    u16_t in;
    u32_t coresize = mars->coresize;
    insn_t* coreMem = mars->coreMem;
    
    for (i=0; i<len; i++) {
        k = (pos+i) % coresize;

#if SIM_STRIP_FLAGS
        in = code[i].in & iMASK;
#else
        in = code[i].in;
#endif

        coreMem[k].in = in;
        coreMem[k].a = code[i].a;
        coreMem[k].b = code[i].b;
    }
    return 0;
}



/*---------------------------------------------------------------
 * P-Space management.
 */
/*
  static void
  free_pspaces(mars_t* mars)
  {
  unsigned int i;
  if (mars->nWarriors > 0) {
  if (mars->pspaces) {
  for (i=0; i<mars->nWarriors; i++) {
  pspace_free(mars->pspaces[i]);
  }
  free(mars->pspaces);
  }
  }
  mars->pspaceSize = 0;
  mars->pspaces = NULL;
  }
*/
/*

static void
alloc_pspaces(mars_t* mars)
{
unsigned int i;
int success = 0;

mars->pspaces = NULL;
mars->pspaceSize = 0;
if (mars->nWarriors == 0) return;

if (( mars->pspaces = (pspace_t**)malloc(sizeof(pspace_t*)*nwars))) {
success = 1;
for (i=0; i<nwars; i++) { PSpaces[i] = NULL; }
for (i=0; success && i<nwars; i++) {
PSpaces[i] = pspace_alloc(pspacesize);
success = success && PSpaces[i] != NULL;
}
}

if ( !success ) {
free_pspaces(nwars);
} else {
PSpace_size = pspacesize;
}
}
*/

 /* NAME
  *     sim_get_pspaces -- get array of p-spaces of warriors.
  *     sim_get_pspace -- get a single p-space.
  * 
  * SYNOPSIS
  *     pspace_t **sim_get_pspaces(void);
  *     pspace_t *sim_get_pspace(unsigned int war_id)
  *
  * DESCRIPTION
  *     Return an array of pointers to p-space structures of the warriors.
  *     The pspace at index i is used as the pspace of the ith warrior
  *     when fighting.
  */
 /*
   pspace_t **
   sim_get_pspaces()
   {
   return PSpaces;
   }
 */

 /*
   pspace_t *
   sim_get_pspace(unsigned int war_id)
   {
   return PSpaces[war_id];
   }
 */

 /* NAME
  *     sim_clear_pspaces -- clear and/or reinitialise p-spaces
  *     sim_reset_pspaces 
  *
  * SYNOPSIS
  *     void sim_clear_pspaces(void);
  *     void sim_reset_pspaces(void);
  *
  * DESCRIPTION
  * All p-spaces are cleared and the P-space locations 0 are set
  * to CORESIZE-1.  For sim_reset_pspaces(): All p-spaces are made
  * private.
  * */

void
sim_clear_pspaces(mars_t* mars)
{
    u32_t i;
    u32_t nwars = mars->nWarriors;
    pspace_t** pspacesOrigin = mars->pspacesOrigin;
    u32_t coresize1 = mars->coresize-1;
    for (i=0; i<nwars; ++i) {
        pspace_clear(pspacesOrigin[i]);
        pspace_set(pspacesOrigin[i], 0, (field_t)coresize1);
    }
}

void
sim_reset_pspaces(mars_t* mars)
{
    u32_t i;
    u32_t nwars = mars->nWarriors;
    pspace_t** pspacesOrigin = mars->pspacesOrigin;
    for (i=0; i<nwars; i++) {
        pspace_privatise(pspacesOrigin[i]);
    }
    sim_clear_pspaces(mars);
}



/*---------------------------------------------------------------
 * Simulator interface
 */

/* NAME
 *     sim, sim_mw -- public functions to simulate a round of Core War
 * 
 * SYNOPSIS
 *     int sim_mw( unsigned int nwar, const field_t *war_pos_tab, 
 *                 unsigned int *death_tab );
 *     int sim( int nwar, field_t w1_start, field_t w2_start,
 *      unsigned int cycles, void **ptr_result );
 * 
 * INPUTS
 *     nwar        -- number of warriors
 *     w1_start, w2_start -- core addresses of first processes
 *                    warrior 1 and warrior 2. Warrior 1 executes first.
 *     cycles      -- the number of cycles to play before tie.
 *     ptr_result  -- NULL, except when requesting the address of core.
 *     war_pos_tab -- core addresses where warriors are loaded in
 *            the order they are to be executed.
 *     death_tab   -- the table where dead warrior indices are stored
 * 
 * DESCRIPTION
 *     The real simulator is inside sim_proper() to which sim() and
 *     sim_mw() are proxies.  sim_mw() reads the warrior position
 *     of the ith warrior from war_tab_pos[i-1].
 *
 * RESULTS
 *     The warriors fight their fight in core which gets messed up in
 *     the process.  If a warrior died during the fight then its p-space
 *     location 0 is cleared.  Otherwise the number of warriors alive
 *     at the end of the battle is stored into its p-space location 0.
 *
 *     sim_mw() stores indices of warriors that die into the death_tab
 *     array in the order of death.  Warrior indices start from 0.
 *
 *     For sim(): If nwar == -1 then buffers of default size for
 *     max. two warriors are allocated and the address of the core
 *     memory is returned via the ptr_result pointer.
 * 
 * RETURN VALUE
 *     sim_mw(): the number of warriors still alive at the end of the
 *       battle.
 *       -1: simulator panic attack -- something's gone wrong
 *
 *     sim(): 
 *       single warrior: 0: warrior suicided, 1: warrior didn't die.
 *       one-on-one two warriors:
 *        0: warrior 1 won, 1: warrior 2 won, 2: tie
 *       -1: simulator panic attack -- something's gone wrong
 * 
 * GLOBALS
 *     All file scoped globals */

int
sim_mw(mars_t* mars, const field_t *const war_pos_tab, u32_t* death_tab)
{
    int alive_count;
    const char *fixed_error = sim_fixed_settings_error(mars);
    if (fixed_error) {
        snprintf(mars->errmsg, sizeof(mars->errmsg), "%s", fixed_error);
        return -1;
    }
    /* if ( !Core_Mem || !Queue_Mem || !War_Tab || !PSpaces ) return -1; */

    alive_count = sim_proper(mars, war_pos_tab, death_tab);

    /* Update p-space locations 0. */
    if (alive_count >= 0) {
        unsigned int nalive = alive_count;
        unsigned int i;
        
        u32_t nwars = mars->nWarriors;
        pspace_t** pspacesOrigin = mars->pspacesOrigin;

        
        for (i=0; i<nwars; i++) {
            pspace_set(pspacesOrigin[i], 0, (field_t)nalive);
        }
        for (i=0; i < nwars - nalive; i++) {
            pspace_set(pspacesOrigin[death_tab[i]], 0, 0);
        }
    }
    return alive_count;
}

/*-------------------------------------------------------------------------
 * private functions
 */

/* NAME
 *     sim_proper -- the real simulator code
 * 
 * SYNOPSIS
 *     int sim_proper( unsigned int nwar,
 *                     const field_t *war_pos_tab,
 *                     unsigned int *death_tab );
 * 
 * INPUTS
 *     nwar        -- number of warriors
 *     war_pos_tab -- core addresses where warriors are loaded in
 *            the order they are to be executed.
 *     death_tab   -- the table where dead warrior indices are stored
 * 
 * RESULTS
 *     The warriors fight their fight in core which gets messed up in
 *     the process.  The indices of warriors that die are stored into
 *     the death_tab[] array in the order of death.  Warrior indices
 *     start from 0.
 * 
 * RETURN VALUE
 *     The number of warriors still alive at the end of the
 *     battle or -1 on an anomalous condition.
 * 
 * GLOBALS
 *     All file scoped globals
 */

/* Various macros:
 *
 *  queue(x): Inserts a core address 'x' to the head of the current
 *            warrior's process queue.  Assumes the warrior's
 *        tail pointer is inside the queue buffer.
 *
 * x, y must be in 0..coresize-1 for the following macros:
 *
 * INCMOD(x): x = x+1 mod coresize
 * DECMOD(x): x = x-1 mod coresize
 * ADDMOD(z,x,y): z = x+y mod coresize
 * SUBMOD(z,x,y): z = x-y mod coresize
 */

/*
  #define queue(x)  do { *w->tail++ = (x); if ( w->tail == queue_end )\
  w->tail = queue_start; } while (0)
*/

/*
  #define queue(x) \
  do { *(w->tail) = (x); if (++(w->tail) == queue_end) w->tail = queue_start; } while(0)
*/

#define queue(x)                                                        \
    do {                                                                \
        insn_t** w_tail = SIM_QTAIL;                                      \
        *(w_tail) = (x); if (++(w_tail) == queue_end) w_tail = queue_start; \
        SIM_QTAIL = w_tail;                                               \
    } while(0)

#define INCMOD(x) do { if ( ++(x) == coresize ) (x) = 0; } while (0)
#define IPINCMOD(x) do { if ( ++(x) == CoreEnd ) (x) = core; } while (0)
#define DECMOD(x) do { if ((x) == 0) (x)=coresize1; else --(x); } while (0)
#define IPDECMOD(x) do { if ((x)==0) (x)=CoreEnd1; else --(x); } while (0)
/* Reduce in unsigned int, not in z: z may be a 16-bit field, where x+y or
 * the wrapped x-y would already be truncated for cores above 32768. */
#define ADDMOD(z,x,y) do { unsigned int t_ = (unsigned int)(x)+(y); if (t_>=coresize) t_ -= coresize; (z) = t_; } while (0)
/* t_ is unsigned! x-y wraps and adding coresize wraps back. */
#define SUBMOD(z,x,y) do { unsigned int t_ = (unsigned int)(x)-(y); if (t_>=coresize) t_ += coresize; (z) = t_; } while (0)

/* private macros to access p-space. */
#define UNSAFE_PSPACE_SET(warid, paddr, val) do {           \
        if (paddr) {                                        \
            pspacesOrigin[(warid)]->mem[(paddr)] = (val);   \
        } else {                                            \
            pspacesOrigin[(warid)]->lastresult = (val);     \
        }                                                   \
    } while(0)

#define UNSAFE_PSPACE_GET(warid, paddr)                 \
    ( (paddr) ? pspacesOrigin[(warid)]->mem[(paddr)]    \
      : pspacesOrigin[(warid)]->lastresult )



/* No instruction creates a new encoding except MOV.I, which copies one.
 * Therefore absence of STP anywhere in core proves no future P-space write.
 * A dormant STP conservatively disables the process-count early exit. */
#if !defined(EXMARS_REFERENCE) && DEBUG == 0

/* MOV.I is the only opcode writer. Once absent, an opcode cannot reappear.
 * DAT is counted cheaply; the other process killers/P-space writer are checked
 * only when DAT reaches zero. Retain a blocker address to avoid repeated scans. */
static unsigned sim_is_other_stop(unsigned in) {
    return ((1u << EX_DIV | 1u << EX_MODM | 1u << EX_STP) >> ((in >> opPOS) & opMASK)) & 1u;
}
static unsigned sim_is_dat(unsigned in) { return !(in & (opMASK << opPOS)); }
static const insn_t *sim_find_other_stop(const insn_t *core, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
        if (sim_is_other_stop(core[i].in)) return core + i;
    }
    return NULL;
}
#define SIM_CHECK_DAT_TIE() do { \
    if (!dat_count) { \
        if (!other_stop_present || \
            (!(blocker && sim_is_other_stop(blocker->in)) && \
             !(blocker = sim_find_other_stop(core, coresize)))) goto out; \
    } \
} while (0)

static int sim_has_stp(const insn_t *core, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
        if (((core[i].in >> opPOS) & opMASK) == EX_STP) return 1;
    return 0;
}
#define SIM_FUNCTION sim_general
#define SIM_EARLY 1
#else
#define SIM_FUNCTION sim_proper
#define SIM_EARLY 0
#endif
#define SIM_TWO 0
#define SIM_QHEAD w->head
#define SIM_QTAIL w->tail
#include "sim_loop.inc"
#undef SIM_FUNCTION
#undef SIM_EARLY
#undef SIM_TWO
#undef SIM_QHEAD
#undef SIM_QTAIL

#if !defined(EXMARS_REFERENCE) && DEBUG == 0
/* Separate instantiations keep the opt-in flag out of the exact hot loop. */
#define SIM_FUNCTION sim_two
#define SIM_EARLY 0
#define SIM_TWO 1
#define SIM_QHEAD qhead
#define SIM_QTAIL qtail
#include "sim_loop.inc"
#undef SIM_FUNCTION
#undef SIM_EARLY
#define SIM_FUNCTION sim_two_early
#define SIM_EARLY 1
#include "sim_loop.inc"
#undef SIM_FUNCTION
#undef SIM_EARLY
#undef SIM_TWO
#undef SIM_QHEAD
#undef SIM_QTAIL

static int sim_proper(mars_t* mars, const field_t *positions, u32_t *deaths) {
    if (mars->nWarriors != 2) return sim_general(mars, positions, deaths);
    /* The early interpreter initializes its proof from actual core, including
     * direct C callers. Exact execution keeps the original unchecked loop. */
    return mars->earlyExit
        ? sim_two_early(mars, positions, deaths)
        : sim_two(mars, positions, deaths);
}
#endif
