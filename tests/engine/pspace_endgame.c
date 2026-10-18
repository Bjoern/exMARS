#include <stdlib.h>
#include <stdio.h>
#include "exhaust.h"
#include "sim.h"
#include "insn.h"
int main(void) {
    mars_t *m = calloc(1, sizeof(*m));
    m->nWarriors=2; m->coresize=80; m->cycles=2; m->processes=80; m->maxWarriorLength=2;
    if (!sim_alloc_bufs(m)) return 2;
    sim_clear_core(m);
    for (int i=0; i<2; ++i) {
        m->positions[i]=i*40;
        m->coreMem[i*40]=(insn_t){1,0,OP(EX_SPL,EX_mB,EX_DIRECT,EX_DIRECT)};
        m->coreMem[i*40+1]=(insn_t){7,1,OP(EX_STP,EX_mAB,EX_IMMEDIATE,EX_IMMEDIATE)};
    }
    sim_mw(m,m->positions,m->deaths);
    for(int i=0;i<2;++i) if(pspace_get(m->pspacesOrigin[i],1)!=7) return 3;
    sim_free_bufs(m);
    puts("SPL endgame preserves later P-space stores");
    return 0;
}
