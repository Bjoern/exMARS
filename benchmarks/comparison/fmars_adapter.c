#include "common.h"
#include "fmars.h"
#include "insn.h"
int main(int argc,char **argv) {
    read_input(argc,argv);
    fmars_mars_t *m=fmars_alloc(2,bc,bp,bt,bc/16);
    if(!m)return 8;
    fmars_warrior_t *cache[64];
    for(unsigned i=0;i<bn;i++) {
        insn_t code[100];
        for(unsigned j=0;j<input[i].len;j++) {
            bench_cell *c=&input[i].cells[j];
            code[j].a=c->a;code[j].b=c->b;code[j].i=OP(c->op,c->mod,c->am,c->bm);
        }
        cache[i]=fmars_bind_warrior(m,code,input[i].len,input[i].start);if(!cache[i])return 9;
    }
    unsigned pair=0;double start=now();
    for(unsigned i=0;i<bn;i++)for(unsigned j=i+1;j<bn;j++,pair++)for(unsigned r=0;r<br;r++) {
        fmars_clear_core(m);fmars_load_warrior(m,cache[i],0,0,r%2);fmars_load_warrior(m,cache[j],1,bench_position(r),1-r%2);
        int *deaths;int alive=fmars_sim_multiwarrior(m,&deaths);
        if(alive==2)scores[pair][1]++;
        else if(alive==1)scores[pair][deaths[0]==0?2:0]++;
        else return 10;
    }
    output(now()-start,pair);for(unsigned i=0;i<bn;i++)fmars_free_warrior(cache[i]);fmars_free(m);return 0;
}
