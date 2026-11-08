/* match.c: the rounds of a match, shared by the CLI (pmars.c) and the Python,
 * Ruby and Wasm bindings. The placement helpers are pMARS compatible
 * (pmars/pos.c, pmars/sim.c); no parser, no exit(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust.h"
#include "sim.h"
#include "pspace.h"
#include "match.h"

#define min(x,y) ((x)<(y) ? (x) : (y))

/* pmars/rng.c random number generator
 */
s32_t rng(s32_t seed)
{
    /* return FIX2INT(rb_eval_string("rand(2147483647)")); */
    register s32_t temp = seed;
    /* As pMARS pos.c: 0 is a fixed point of this generator, so -F equal to
     * the minimum separation would otherwise repeat one placement forever. */
    temp = (s32_t)((u32_t)temp & 0x7fffffffu);
    if (!temp) temp = 1;
    temp = 16807 * (temp % 127773) - 2836 * (temp / 127773);
    if (temp < 0)
        temp += 2147483647;
    return temp;
}

void clear_results(mars_t* mars)
{
    memset(mars->results, 0, sizeof(u32_t)*mars->nWarriors*(mars->nWarriors+1));
    /*
      unsigned int i, j;
      for (i=0; i<NWarriors; i++) {
      for (j=0; j<=NWarriors; j++) {
      Results[i*(NWarriors+1) + j] = 0;
      }
      }
    */
}

void save_pspaces(mars_t* mars)
{
    memcpy(mars->pspaces, mars->pspacesOrigin, sizeof(pspace_t *)*mars->nWarriors );
}

void amalgamate_pspaces(mars_t* mars)
{
    unsigned int i, j;

    /* Share p-space according to PINs. */
    for (i=0; i<mars->nWarriors; i++) {
        if ( mars->warriors[i].have_pin ) {
            for (j=0; j<i; j++) {
                if ( mars->warriors[j].have_pin &&
                     mars->warriors[j].pin == mars->warriors[i].pin )
                {
                    pspace_share(mars->pspaces[i], mars->pspaces[j]);
                }
            }
        }
    }
}


void load_warriors(mars_t* mars)
{
    unsigned int i;
    for (i=0; i<mars->nWarriors; i++) {
        sim_load_warrior(mars, mars->positions[i], mars->warriors[i].code, mars->warriors[i].len);
    }
}

void set_starting_order(unsigned int round, mars_t* mars)
{
    unsigned int i;

    /* Copy load positions into starting positions array
       with a cyclic shift of rounds places. */
    for (i=0; i<mars->nWarriors; i++) {
        unsigned int j = (i+round) % (mars->nWarriors);
        mars->startPositions[i] =(field_t)((mars->positions[j] + mars->warriors[j].start ) % (mars->coresize));
    }

    /* Copy p-spaces into simulator p-space array with a
       cyclic shift of rounds places. */
    /*
      ps = sim_get_pspaces();
      for (i=0; i<NWarriors; i++) {
      ps[i] = PSpaces[(i + round) % NWarriors];
      }
    */
    for (i=0; i<mars->nWarriors; i++) {
        mars->pspacesOrigin[i] = mars->pspaces[(i + round) % (mars->nWarriors)];
    }
}

void accumulate_results(mars_t* mars)
{
    unsigned int i;

    /* Fetch the results of the last round from p-space location 0
       that has been updated by the simulator. */
    for (i=0; i<mars->nWarriors; i++) {
        unsigned int result;
        result = pspace_get(mars->pspaces[i], 0);
        /* printf("%d ", result); */
        mars->results[i*(mars->nWarriors+1) + result]++;
    }
}

/*---------------------------------------------------------------
 * Warrior positioning algorithms
 *
 * These are pMARS compatible.  Warrior 0 is always positioned at 0.
 * posit() and npos() are transcribed from pmars/pos.c.  */

#define RETRIES1 20                /* how many times to try generating one
                                    * position */
#define RETRIES2 4                /* how many times to start backtracking */
int posit(s32_t *seed, mars_t* mars)
{
    unsigned int pos = 1, i;
    unsigned int retries1 = RETRIES1, retries2 = RETRIES2;
    int     diff;

    do {
        /* generate */
        *seed = rng(*seed);
        /*mars->positions[pos] = (field_t)(rand()%(mars->coresize - 2*mars->minsep+1) + mars->minsep);*/
        mars->positions[pos] = (field_t)((*seed % (mars->coresize - 2*mars->minsep+1))+mars->minsep);

        /* test for overlap */
        for (i = 1; i < pos; ++i) {
            /* calculate positive difference */
            diff = (int) mars->positions[pos] - mars->positions[i];
            if (diff < 0)
                diff = -diff;
            if ((unsigned int)diff < mars->minsep)
                break;      /* overlap! */
        }

        if (i == pos)       /* no overlap, generate next number */
            ++pos;
        else {          /* overlap */
            if (!retries2)
                return 1;   /* exceeded attempts, fail */
            if (!retries1) {    /* backtrack: generate new sequence starting */
                pos = i;    /* at arbitrary position (last conflict) */
                --retries2;
                retries1 = RETRIES1;
            } else      /* generate new current number (pos not
                         * incremented) */
                --retries1;
        }
    } while (pos < mars->nWarriors);
    return 0;
}

void npos(s32_t *seed, mars_t* mars)
{
    unsigned int i, j;
    unsigned int temp;
    unsigned int room = mars->coresize - mars->minsep*mars->nWarriors+1;

    /* Choose NWarriors-1 positions from the available room. */
    for (i = 1; i < mars->nWarriors; i++) {
        *seed = rng(*seed);
        temp = *seed % room;
        for (j = i - 1; j > 0; j--) {
            if (temp > mars->positions[j])
                break;
            mars->positions[j+1] = mars->positions[j];
        }
        mars->positions[j+1] = temp;
    }

    /* Separate the positions by minsep cells. */
    temp = mars->minsep;
    for (i = 1; i < mars->nWarriors; i++) {
        mars->positions[i] += temp;
        temp += mars->minsep;
    }

    /* Random permutation of positions. */
    for (i = 1; i < mars->nWarriors; i++) {
        *seed = rng(*seed);
        j = *seed % (mars->nWarriors - i) + i;
        temp = mars->positions[j];
        mars->positions[j] = mars->positions[i];
        mars->positions[i] = temp;
    }
}


s32_t compute_positions(s32_t seed, mars_t* mars)
{
    u32_t avail = mars->coresize+1 - mars->nWarriors * mars->minsep;

    mars->positions[0] = 0;

    /* Case single warrior. */
    if (mars->nWarriors == 1) return seed;

    /* Case one on one. */
    if (mars->nWarriors == 2) {
        mars->positions[1] = (field_t)(mars->minsep + seed % avail);
        /*printf("s: %ld\n", mars->positions[pos]),
          ++seedtest[mars->positions[pos]];*/
        seed = rng(seed);
        return seed;
    }

    if (mars->nWarriors>2) {
        if (posit(&seed, mars)) {
            npos(&seed, mars);
        }
    }
    return seed;
}

/* NULL when the loaded warriors can be fought, else why not (also left in
 * mars->errmsg). For library callers, which must not exit(). */
const char* sanity_error(mars_t* mars)
{
    u32_t space_used;
    unsigned int i;

    /* Make sure each warrior has some code. */
    for (i=0; i<mars->nWarriors; i++) {
        if (mars->warriors[i].len == 0) {
            sprintf(mars->errmsg,"warrior %d has no code\n", i);
            return mars->errmsg;
        }
    }

    /* Make sure there is some minimum sepation. */
    if ( mars->minsep == 0 ) {
        mars->minsep = min(mars->coresize/mars->nWarriors, mars->maxWarriorLength);
    }

    /* Make sure minsep dominates the lengths of all warriors. */
    for (i=0; i<mars->nWarriors; i++) {
        if ( mars->minsep < mars->warriors[i].len ) {
            strcpy(mars->errmsg, "minimum separation must be >= longest warrior\n");
            return mars->errmsg;
        }
    }

    /* Make sure there is space for all warriors to be loaded. */
    space_used = mars->nWarriors*mars->minsep;
    if ( space_used > mars->coresize ) {
        strcpy(mars->errmsg, "warriors too large to fit into core\n");
        return mars->errmsg;
    }
    return NULL;
}

u32_t mars_permutations(const mars_t *mars)
{
    return mars->nWarriors == 2 ? 2 * (mars->coresize + 1 - 2 * mars->minsep) : 0;
}

int mars_match(mars_t *mars)
{
    u32_t i, positions, permidx = 0, permtmp, *permbuf = NULL;
    s32_t seed;

    if (sanity_error(mars)) return MATCH_E_ARG;
    if (mars->permutate && mars->nWarriors != 2) {
        strcpy(mars->errmsg, "permutation needs exactly two warriors\n");
        return MATCH_E_ARG;
    }
    clear_results(mars);
    seed = mars->fixedPosition ? mars->fixedPosition - (s32_t)mars->minsep : rng(mars->seed);
    if (mars->permutate) {
        positions = mars->coresize + 1 - 2 * mars->minsep;
        if (mars->fixedPosition)
            seed *= 2;          /* as pMARS: the table index of that position */
        permbuf = (u32_t *)malloc(2 * positions * sizeof *permbuf);
        if (!permbuf) {
            strcpy(mars->errmsg, "out of memory\n");
            return MATCH_E_MEMORY;
        }
    }

    /* Each match is fresh, as on a KotH hill: undo the previous match's
     * per-round rotation (which would swap ownership after an even number of
     * rounds), drop PIN sharing, clear (zeroes, CORESIZE-1 as the last
     * result), then share by PIN. */
    if (mars->pspacesRotated)
        memcpy(mars->pspacesOrigin, mars->pspaces, sizeof(pspace_t *) * mars->nWarriors);
    sim_reset_pspaces(mars);
    save_pspaces(mars);
    mars->pspacesRotated = 1;
    amalgamate_pspaces(mars);

    for (i = 0; i < mars->rounds; ++i) {
        unsigned int starter = i;
        int nalive;
        sim_clear_core(mars);
        if (permbuf) {
            /* pMARS sim.c: draw a (position, starter) pair without replacement.
             * TODO (cleanup, no behaviour change): move this draw into a
             * permutate_positions(&seed, mars) next to compute_positions(),
             * returning the starter, with permbuf/permidx kept in mars_t, so
             * the loop makes a single placement call. Kept inline for now, as
             * in pMARS's own round loop. */
            if (permidx == 0)
                for (; permidx < 2 * positions; permidx++) permbuf[permidx] = permidx;
            permtmp = (u32_t)seed % permidx;
            mars->positions[0] = 0;
            mars->positions[1] = (field_t)(mars->minsep + permbuf[permtmp] / 2);
            starter = permbuf[permtmp] % 2;
            permbuf[permtmp] = permbuf[--permidx];
            seed = rng(seed);
        } else {
            seed = compute_positions(seed, mars);
        }
        load_warriors(mars);
        set_starting_order(starter, mars);

        nalive = sim_mw(mars, mars->startPositions, mars->deaths);
        if (nalive < 0) {
            free(permbuf);
            mars->seed = seed;
            if (!mars->errmsg[0]) strcpy(mars->errmsg, "simulator error\n");
            return MATCH_E_SIM;
        }
        accumulate_results(mars);
    }
    mars->seed = seed;
    free(permbuf);
    return MATCH_OK;
}
