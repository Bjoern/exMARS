#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
typedef struct { unsigned op, mod, am, a, bm, b; } bench_cell;
typedef struct { unsigned len, start; bench_cell cells[100]; } bench_war;
static bench_war input[64];
static unsigned bc, bt, bp, bd, bn, br;
static unsigned long scores[2016][3];
static unsigned bench_position(unsigned round) {
    unsigned k = round/2;
    return bc==80 ? bd+k%71 : bd+((k%16)*487+3900)%(bc-2*bd+1);
}
static void read_input(int argc, char **argv) {
    if(argc!=3) exit(2);
    FILE *f=fopen(argv[1],"r"); if(!f)exit(3);
    if(fscanf(f,"%u%u%u%u%u",&bc,&bt,&bp,&bd,&bn)!=5 || bn>64)exit(4);
    br=(bc==80?142:32)*atoi(argv[2]); if(!br)exit(5);
    for(unsigned i=0;i<bn;i++) {
        bench_war *w=&input[i];
        if(fscanf(f,"%u%u",&w->len,&w->start)!=2 || w->len>100 || w->start>=w->len)exit(6);
        for(unsigned j=0;j<w->len;j++) {
            bench_cell *c=&w->cells[j];
            if(fscanf(f,"%u%u%u%u%u%u",&c->op,&c->mod,&c->am,&c->a,&c->bm,&c->b)!=6)exit(7);
        }
    }
    fclose(f);
}
static double now(void) {
#ifdef _WIN32
    LARGE_INTEGER t,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t);return (double)t.QuadPart/f.QuadPart;
#else
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); exit(10); }
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
#endif
}
static void output(double elapsed,unsigned pairs) {
    printf("{\"seconds\":%.9f,\"rounds\":%u,\"scores\":[",elapsed,pairs*br);
    for(unsigned i=0;i<pairs;i++)printf("%s[%lu,%lu,%lu]",i?",":"",scores[i][0],scores[i][1],scores[i][2]);
    puts("]}");
}
#endif
