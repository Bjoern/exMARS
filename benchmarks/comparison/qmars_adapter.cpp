#include "common.h"
#include <vector>
#include "warrior.hpp"
static unsigned benchmark_position(unsigned r) { return bench_position(r); }
#include "qmars.hpp"
template<unsigned CORE,unsigned CYCLES,unsigned PROCESSES,unsigned DIST> int run() {
    const EOpCode ops[]={eDat,eSpl,eMov,eDjn,eAdd,eJmz,eSub,eCmp,eSne,eSlt,eJmn,eJmp,eNop,eMul,eMod,eDiv};
    const EModifier mods[]={eF,eA,eB,eAB,eBA,eX,eI};
    const EAddrMode modes[]={eDir,eIm,eInB,ePreDecInB,ePostIncInB,eInA,ePreDecInA,ePostIncInA};
    Warrior cache[64];
    for(unsigned i=0;i<bn;i++) {
        cache[i].start=input[i].start;
        for(unsigned j=0;j<input[i].len;j++) {
            bench_cell *c=&input[i].cells[j];cache[i].code.push_back(Code(ops[c->op],mods[c->mod],modes[c->am],c->a,modes[c->bm],c->b));
        }
    }
    Mars<2,CORE,CYCLES,PROCESSES,DIST,eSilent> *m=new Mars<2,CORE,CYCLES,PROCESSES,DIST,eSilent>();
    unsigned pair=0;double start=now();
    for(unsigned i=0;i<bn;i++)for(unsigned j=i+1;j<bn;j++,pair++) {
        cache[i].win=cache[i].tie=cache[i].lose=0;cache[j].win=cache[j].tie=cache[j].lose=0;
        std::vector<Warrior*> w;w.push_back(&cache[i]);w.push_back(&cache[j]);m->simulate(w,br);
        scores[pair][0]=cache[i].win;scores[pair][1]=cache[i].tie;scores[pair][2]=cache[j].win;
    }
    output(now()-start,pair);delete m;return 0;
}
int main(int argc,char **argv) { read_input(argc,argv); return bc==80?run<80,800,80,5>():run<8000,80000,8000,100>(); }
