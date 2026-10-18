#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "exhaust.h"
#include "sim.h"
#include "insn.h"
int main(void) {
    unsigned sizes[]={8000,46341,50000,65535};
    for(unsigned k=0;k<4;++k) {
        unsigned size=sizes[k];
        mars_t *m=calloc(1,sizeof(*m));
        m->nWarriors=1;m->coresize=size;m->cycles=1;m->processes=1;m->maxWarriorLength=3;
        if(!sim_alloc_bufs(m))return 2;
        sim_clear_core(m);m->positions[0]=0;
        m->coreMem[0]=(insn_t){1,2,OP(EX_MUL,EX_mF,EX_DIRECT,EX_DIRECT)};
        m->coreMem[1]=(insn_t){size-1,size-2,OP(EX_DAT,EX_mF,EX_DIRECT,EX_DIRECT)};
        m->coreMem[2]=m->coreMem[1];
        sim_mw(m,m->positions,m->deaths);
        unsigned a=((uint64_t)(size-1)*(size-1))%size;
        unsigned b=((uint64_t)(size-2)*(size-2))%size;
        if(m->coreMem[2].a!=a || m->coreMem[2].b!=b)return 3;
        sim_free_bufs(m);
    }
    puts("MUL independent uint64 oracle: core sizes 8000, 46341, 50000, 65535 passed");
}
