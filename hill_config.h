#ifndef EXMARS_HILL_CONFIG_H
#define EXMARS_HILL_CONFIG_H

#include "exhaust.h"

/* Zero keeps a setting dynamic. Nonzero values are part of this build's API. */
#ifndef EXMARS_FIXED_CORESIZE
#define EXMARS_FIXED_CORESIZE 0
#endif
#ifndef EXMARS_FIXED_PROCESSES
#define EXMARS_FIXED_PROCESSES 0
#endif
#ifndef EXMARS_FIXED_CYCLES
#define EXMARS_FIXED_CYCLES 0
#endif
#ifndef EXMARS_DEFAULT_LENGTH
#define EXMARS_DEFAULT_LENGTH 100
#endif
#ifndef EXMARS_DEFAULT_MINSEP
#define EXMARS_DEFAULT_MINSEP 100
#endif

#if EXMARS_FIXED_CORESIZE < 0 || EXMARS_FIXED_CORESIZE > 65536
#error "EXMARS_FIXED_CORESIZE must be 0 (dynamic) or 1..65536"
#endif
#if EXMARS_FIXED_PROCESSES < 0 || EXMARS_FIXED_PROCESSES > 2147483647
#error "EXMARS_FIXED_PROCESSES must be 0 (dynamic) or 1..2147483647"
#endif
#if EXMARS_FIXED_CYCLES < 0 || EXMARS_FIXED_CYCLES > 2147483647
#error "EXMARS_FIXED_CYCLES must be 0 (dynamic) or 1..2147483647"
#endif
#if EXMARS_DEFAULT_LENGTH <= 0 || EXMARS_DEFAULT_MINSEP <= 0
#error "Default warrior length and separation must be positive"
#endif

#if EXMARS_FIXED_CORESIZE
#define EXMARS_CORE(m) ((unsigned int)EXMARS_FIXED_CORESIZE)
#define EXMARS_INITIAL_CORE EXMARS_FIXED_CORESIZE
#else
#define EXMARS_CORE(m) ((m)->coresize)
#define EXMARS_INITIAL_CORE 8000
#endif
#if EXMARS_FIXED_PROCESSES
#define EXMARS_PROCESSES(m) ((u32_t)EXMARS_FIXED_PROCESSES)
#define EXMARS_INITIAL_PROCESSES EXMARS_FIXED_PROCESSES
#else
#define EXMARS_PROCESSES(m) ((m)->processes)
#define EXMARS_INITIAL_PROCESSES 8000
#endif
#if EXMARS_FIXED_CYCLES
#define EXMARS_CYCLES(m) ((u32_t)EXMARS_FIXED_CYCLES)
#define EXMARS_INITIAL_CYCLES EXMARS_FIXED_CYCLES
#else
#define EXMARS_CYCLES(m) ((m)->cycles)
#define EXMARS_INITIAL_CYCLES 80000
#endif

#define EXMARS_STRING_VALUE_(x) #x
#define EXMARS_STRING_VALUE(x) EXMARS_STRING_VALUE_(x)

/* Also exposed to C callers. Generic builds fold this check away entirely. */
static inline const char *sim_fixed_settings_error(const mars_t *mars) {
#if EXMARS_FIXED_CORESIZE
    if (mars->coresize != EXMARS_FIXED_CORESIZE)
        return "specialized build requires coresize=" EXMARS_STRING_VALUE(EXMARS_FIXED_CORESIZE);
#endif
#if EXMARS_FIXED_PROCESSES
    if (mars->processes != EXMARS_FIXED_PROCESSES)
        return "specialized build requires max processes=" EXMARS_STRING_VALUE(EXMARS_FIXED_PROCESSES);
#endif
#if EXMARS_FIXED_CYCLES
    if (mars->cycles != EXMARS_FIXED_CYCLES)
        return "specialized build requires cycles=" EXMARS_STRING_VALUE(EXMARS_FIXED_CYCLES);
#endif
    (void)mars;
    return 0;
}
#endif
