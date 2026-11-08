/* match.h: one match (rounds of a fight) on a mars_t, shared by the CLI and
 * all bindings. No parser here, so nothing on this path calls exit(). */
#ifndef MATCH_H
#define MATCH_H

#include "exhaust.h"

/* mars_match() results. On failure mars->errmsg says why. */
#define MATCH_OK 0
#define MATCH_E_ARG (-1)      /* warriors or settings can't be fought */
#define MATCH_E_SIM (-2)      /* simulator error */
#define MATCH_E_MEMORY (-3)

s32_t rng(s32_t seed);

/* One match of mars->rounds rounds of the loaded warriors, from fresh P-space
 * (P-space persists between the rounds of the match only), results in
 * mars->results. Placements: mars->fixedPosition as pMARS -F (0 = random);
 * with mars->permutate (pMARS -P, two warriors only), each round draws a
 * not yet used (position, starting warrior) pair, so mars_permutations()
 * rounds play every pair once, in pMARS's order for the same -F. */
int mars_match(mars_t *mars);

/* Rounds that play every placement in both starting orders once: pMARS -P's
 * default, 2 * (coresize - 2 * minsep + 1); 0 unless there are two warriors. */
u32_t mars_permutations(const mars_t *mars);

/* NULL when the loaded warriors can be fought, else why not (also left in
 * mars->errmsg). */
const char *sanity_error(mars_t *mars);

/* The steps of a round, for callers that need them one by one. */
void clear_results(mars_t *mars);
void save_pspaces(mars_t *mars);
void amalgamate_pspaces(mars_t *mars);
void load_warriors(mars_t *mars);
void set_starting_order(unsigned int round, mars_t *mars);
void accumulate_results(mars_t *mars);
s32_t compute_positions(s32_t seed, mars_t *mars);
int posit(s32_t *seed, mars_t *mars);
void npos(s32_t *seed, mars_t *mars);

#endif
