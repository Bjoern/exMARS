#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "exhaust.h"
#include "sim.h"
#include "insn.h"
static unsigned run(unsigned nw, unsigned cycles, int early, int stp, int *alive) {
    mars_t *m=calloc(1,sizeof(*m));
    m->nWarriors=nw; m->coresize=80; m->cycles=cycles;
    m->processes=80; m->maxWarriorLength=3; m->earlyExit=early;
    assert(sim_alloc_bufs(m)); sim_clear_core(m);
    for(unsigned i=0;i<nw;i++) {
        unsigned p=i*20; m->positions[i]=p;
        m->coreMem[p]=(insn_t){1,0,OP(EX_SPL,EX_mB,EX_DIRECT,EX_DIRECT)};
        if(stp==1) m->coreMem[p+1]=(insn_t){7,1,OP(EX_STP,EX_mAB,EX_IMMEDIATE,EX_IMMEDIATE)};
    }
    if(stp==2) m->coreMem[79]=(insn_t){9,1,OP(EX_STP,EX_mAB,EX_IMMEDIATE,EX_IMMEDIATE)};
    *alive=sim_mw(m,m->positions,m->deaths);
    unsigned procs=0;
    for(unsigned i=0;i<nw;i++) {
        procs+=m->warTab[i].nprocs;
        assert(pspace_get(m->pspacesOrigin[i],1)==(stp==1?7:0));
        if(cycles==2) assert(pspace_get(m->pspacesOrigin[i],0)==nw);
    }
    sim_free_bufs(m); return procs;
}
int main(void) {
    for(unsigned nw=1;nw<=4;nw++) {
        int a,b;
        unsigned exact=run(nw,2,0,0,&a),early=run(nw,2,1,0,&b);
        assert(a==b && a==(int)nw && early>exact);
        for(int stp=1;stp<=2;stp++) {
            exact=run(nw,2,0,stp,&a); early=run(nw,2,1,stp,&b);
            assert(a==b && early==exact);
        }
    }
    /* After SPL: two processes, exactly two turns, both execute DAT and die. */
    int a,b;
    assert(run(1,3,0,0,&a)==0);
    assert(run(1,3,1,0,&b)==0 && a==0 && b==0);
    puts("early termination: no-STP, pending/dormant STP, strict boundary pass (1-4 warriors)");
}
