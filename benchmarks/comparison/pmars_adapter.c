#include "common.h"
#include "global.h"
#include "sim.h"
static mem_struct code[64][100];
unsigned benchmark_position(unsigned r) { return bench_position(r); }
int main(int argc,char **argv) {
    read_input(argc,argv);
    const unsigned ops[]={DAT,SPL,MOV,DJN,ADD,JMZ,SUB,CMP,SNE,SLT,JMN,JMP,NOP,MUL,MOD,DIV};
    const unsigned mods[]={mF,mA,mB,mAB,mBA,mX,mI};
    const unsigned modes[]={DIRECT,IMMEDIATE,INDIRECT,PREDECR,POSTINC,SYM_TO_INDIR_A(5),SYM_TO_INDIR_A(6),SYM_TO_INDIR_A(7)};
    for(unsigned i=0;i<bn;i++)for(unsigned j=0;j<input[i].len;j++) {
        bench_cell *c=&input[i].cells[j];mem_struct *p=&code[i][j];
        p->opcode=ops[c->op]*8+mods[c->mod];p->A_mode=modes[c->am];p->B_mode=modes[c->bm];p->A_value=c->a;p->B_value=c->b;p->debuginfo=0;
    }
    warriors=2;coreSize=bc;cycles=bt;taskNum=bp;instrLim=100;separation=bd;rounds=br;
#ifdef RWLIMIT
    readLimit=writeLimit=bc;
#endif
    INITIALINST.opcode=DAT*8+mF;INITIALINST.A_mode=INITIALINST.B_mode=DIRECT;
    SWITCH_Fnum=bd+1;SWITCH_b=SWITCH_k=1;debugState=0;
    unsigned pair=0;double start=now();
    for(unsigned i=0;i<bn;i++)for(unsigned j=i+1;j<bn;j++,pair++) {
        memset(warrior,0,2*sizeof(*warrior));
        warrior[0].instBank=code[i];warrior[0].instLen=input[i].len;warrior[0].offset=input[i].start;
        warrior[1].instBank=code[j];warrior[1].instLen=input[j].len;warrior[1].offset=input[j].start;
        simulator1();
        scores[pair][0]=warrior[0].score[0];scores[pair][1]=warrior[0].score[1];scores[pair][2]=warrior[1].score[0];
    }
    output(now()-start,pair);return 0;
}
