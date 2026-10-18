#include <stdio.h>
#include "fmars.h"
#include "insn.h"
int main(void) {
    for(int ismod=0;ismod<2;ismod++) {
        fmars_mars_t *m=fmars_alloc(2,80,80,1,5);
        insn_t code[4]={{0}},idle={0};
        code[0].i=OP(ismod?MODM:DIV,ismod?mF:mI,DIRECT,APOSTINC);code[0].a=1;code[0].b=3;
        code[3].a=79;code[1].i=code[2].i=code[3].i=OP(DAT,mF,DIRECT,DIRECT);
        code[1].a=6;code[1].b=3;code[2].a=ismod?20:18;code[2].b=ismod?17:15;
        idle.i=OP(JMP,mB,DIRECT,DIRECT);
        fmars_warrior_t *a=fmars_bind_warrior(m,code,4,0),*b=fmars_bind_warrior(m,&idle,1,0);
        if(!a||!b)return 2;
        fmars_clear_core(m);fmars_load_warrior(m,a,0,0,0);fmars_load_warrior(m,b,1,40,1);fmars_sim_multiwarrior(m,0);
        printf("%s got %u,%u expected %s\n",ismod?"MOD":"DIV",m->core_mem[2].a,m->core_mem[2].b,ismod?"2,2":"3,5");
        fmars_free_warrior(a);fmars_free_warrior(b);fmars_free(m);
    }
}
