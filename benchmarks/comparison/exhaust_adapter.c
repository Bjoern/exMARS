#include "common.h"
#include "exhaust.h"
#include "sim.h"
#include "insn.h"
static insn_t code[64][100];
int main(int argc,char **argv) {
    read_input(argc,argv);
    for(unsigned i=0;i<bn;i++)for(unsigned j=0;j<input[i].len;j++) {
        bench_cell *c=&input[i].cells[j];
        code[i][j].a=c->a;code[i][j].b=c->b;code[i][j].in=OP(c->op,c->mod,c->am,c->bm);
    }
#ifdef EXMARS
    mars_t *m=calloc(1,sizeof(*m));
    m->nWarriors=2;m->coresize=bc;m->processes=bp;m->cycles=bt;m->maxWarriorLength=100;m->minsep=bd;
#ifdef EARLY
    m->earlyExit=EARLY;
#endif
    if(!sim_alloc_bufs(m))return 8;
#else
    if(!sim_alloc_bufs(2,bc,bp,bt))return 8;
#endif
    unsigned pair=0;double start=now();
    for(unsigned i=0;i<bn;i++)for(unsigned j=i+1;j<bn;j++,pair++)for(unsigned r=0;r<br;r++) {
        unsigned pos=bench_position(r),order=r%2,death[2];
        field_t starts[2];
        starts[order]=input[i].start;starts[1-order]=(pos+input[j].start)%bc;
#ifdef EXMARS
        u32_t deaths[2];
        sim_clear_core(m);sim_load_warrior(m,0,code[i],input[i].len);sim_load_warrior(m,pos,code[j],input[j].len);
        int alive=sim_mw(m,starts,deaths);death[0]=(unsigned)deaths[0];
#else
        sim_clear_core();sim_load_warrior(0,code[i],input[i].len);sim_load_warrior(pos,code[j],input[j].len);
        int alive=sim_mw(2,starts,death);
#endif
        if(alive==2)scores[pair][1]++;
        else if(alive==1)scores[pair][death[0]==order?2:0]++;
        else return 9;
    }
    output(now()-start,pair);
#ifdef EXMARS
    sim_free_bufs(m);
#else
    sim_free_bufs();
#endif
    return 0;
}
