/* Shared interpreter body, instantiated for general and two-warrior execution.
 * Keep opcode semantics in one place. SIM_TWO only changes queue scheduling.
 */
int
SIM_FUNCTION(mars_t* mars, const field_t * const war_pos_tab, u32_t* death_tab )
{
    /*
     * Core and Process queue memories.
     *
     * The warriors share a common cyclic buffer for use as a process
     * queue which the contains core addresses where active processes
     * are.  The buffer has size N*P+1, where N = number of warriors,
     * P = maximum number of processes / warrior.
     *
     * Each warrior has a fixed slice of the buffer for its own process
     * queue which are initially allocated to the warriors in reverse
     * order. i.e. if the are N warriors w1, w2, ..., wN, the slice for
     * wN is 0..P-1, w{N-1} has P..2P-1, until w1 has (N-1)P..NP-1.
     *
     * The core address of the instruction is fetched from the head of
     * the process queue and processes are pushed to the tail, so the
     * individual slices slide along at one location per executed
     * instruction.  The extra '+1' in the buffer size is to have free
     * space to slide the slices along.
     * 
     * For two warriors w1, w2:
     *
     * |\......../|\......../| |
     * | w2 queue | w1 queue | |
     * 0          P         2P 2P+1
     */

    /*
     * Cache Registers.
     *
     * The '94 draft specifies that the redcode processor model be
     * 'in-register'.  That is, the current instruction and the
     * instructions at the effective addresses (ea's) be cached in
     * registers during instruction execution, rather than have
     * core memory accessed directly when the operands are needed.  This
     * causes differences from the 'in-memory' model.  e.g. MOV 0,>0
     * doesn't change the instruction's b-field since the instruction at
     * the a-field's effective address (i.e. the instruction itself) was
     * cached before the post-increment happened.
     *
     * There are conceptually three registers: IN, A, and B.  IN is the
     * current instruction, and A, B are the ones at the a- and
     * b-fields' effective addresses respectively.
     *
     * We don't actually cache the complete instructions, but rather
     * only the *values* of their a- and b-field.  This is because
     * currently there is no way effective address computations can
     * modify the opcode, modifier, or addressing modes of an
     * instruction.
     */


    /*
     * misc.
     */
    u32_t processes = EXMARS_PROCESSES(mars);
    insn_t* const core = mars->coreMem;
    insn_t** const queue_start = mars->queueMem;
    #if SIM_TWO
    const u32_t nwar = 2;
#else
    u32_t nwar = mars->nWarriors;
#endif
    insn_t** const queue_end = mars->queueMem + nwar * EXMARS_PROCESSES(mars) + 1;
    w_t* w;         /* current warrior */
    const unsigned int coresize = EXMARS_CORE(mars);
    const unsigned int coresize1 = coresize-1; /* size of core, size of core - 1 */
    insn_t* const CoreEnd = core + coresize; /* point after last instruction */
    u32_t cycles = nwar * EXMARS_CYCLES(mars); /* set instruction executions until tie counter */
    int alive_cnt = nwar;
#if SIM_EARLY && !SIM_TWO
    int stp_present = -1;
#endif
    u32_t max_alive_proc = nwar * mars->processes; 
    insn_t **pofs = queue_end-1;
    u32_t pspaceSize = mars->pspaceSize;
    pspace_t** pspacesOrigin = mars->pspacesOrigin;


#if DEBUG >= 1
    insn_t insn;            /* used for disassembly */
    char debug_line[256];       /* ditto */
#endif
    w_t* warTab = mars->warTab;

    warTab[0].succ = &(warTab[nwar-1]);
    warTab[nwar-1].pred = &(warTab[0]);
    {
        u32_t ftmp = 0;     /* temps */

        do {
            int t = nwar-1-ftmp;
            if ( t > 0 ) warTab[t].succ = &(warTab[t-1]);
            if ( t < (int)nwar-1 ) warTab[t].pred = &(warTab[t+1]);
            pofs -= processes;
            *pofs = &(core[war_pos_tab[ftmp]]);
            warTab[t].head = pofs;
            warTab[t].tail = pofs+1;
            warTab[t].nprocs = 1;
            warTab[t].id = ftmp;
            ftmp++;
        } while ( ftmp < nwar );
    }

    w = &(warTab[nwar-1]);
#if SIM_TWO
    insn_t **qhead = w->head, **qtail = w->tail;
    insn_t **other_head = warTab[0].head, **other_tail = warTab[0].tail;
#endif


#if SIM_TWO && SIM_EARLY
    /* Start from actual core. Flags describe initially available opcodes;
     * subsequent MOV.I copies cannot introduce a previously absent opcode. */
    unsigned dat_count = 0, other_stop_present = 0;
    int allow_spl_exit = 1;
    const insn_t *blocker = NULL;
    for (unsigned cell = 0; cell < coresize; ++cell) {
        unsigned in = core[cell].in;
        dat_count += sim_is_dat(in);
        other_stop_present |= sim_is_other_stop(in);
        if (((in >> opPOS) & opMASK) == EX_STP) allow_spl_exit = 0;
    }
    SIM_CHECK_DAT_TIE();
#endif
    /*******************************************************************
     * Main loop - optimize here
     */
    do {
        /* 'in' field of current insn for decoding */
        u32_t in;

        /* A register values */
        field_t ra_a, ra_b;

        /* B register values */
        field_t rb_a, rb_b;

        insn_t *pta;
        insn_t *ptb;
        unsigned int mode;

        insn_t* ip = *(SIM_QHEAD);
        if ( ++(SIM_QHEAD) == queue_end ) SIM_QHEAD = queue_start;
        in = ip->in;        /* note: flags must be unset! */
#if !SIM_STRIP_FLAGS
        in = in & iMASK;        /* strip flags. */
#endif
#if DEBUG == 0 && !defined(EXMARS_REFERENCE)
        /* Common imp/copy instruction. Immediate A and direct B have no
         * addressing side effects, so copying the instruction here is
         * equivalent to caching A, resolving B, then storing all fields.
         * Keep other modes on the general path: their pre/post updates can
         * alias the source and require the existing register semantics.
         * EXMARS_REFERENCE disables this shortcut for differential tests.
         */
        if (in == OP(EX_MOV, EX_mI, EX_IMMEDIATE, EX_DIRECT)) {
            ptb = ip + ip->b;
            if (ptb >= CoreEnd) ptb -= coresize;
#if SIM_TWO && SIM_EARLY
            dat_count -= sim_is_dat(ptb->in);
#endif
            *ptb = *ip;
            IPINCMOD(ip);
            queue(ip);
#if SIM_TWO && SIM_EARLY
            SIM_CHECK_DAT_TIE();
#endif
            goto noqueue;
        }
#endif
        rb_a = ra_a = ip->a;
        rb_b = ip->b;

#if DEBUG >= 1
        insn = *ip;
        dis1( debug_line, insn, coresize);
#endif

        mode = in & mMASK;

        /* a-mode calculation */
        if (mode == EX_IMMEDIATE) {
            /*printf("IMMEDIATE\n");*/
            ra_b = rb_b;
            pta = ip;
        } else if (mode == EX_DIRECT) {
            /*printf("DIRECT\n");*/
            pta = ip + ra_a; if (pta >= CoreEnd) pta -= coresize;
            ra_a = pta->a;
            ra_b = pta->b;
        } else if (mode == EX_BINDIRECT) {
            /*printf("BINDIRECT\n");*/
            pta = ip + ra_a; if (pta >= CoreEnd) pta -= coresize;
            pta = pta + pta->b; if (pta >= CoreEnd) pta -= coresize;
            ra_a = pta->a;      /* read in registers */
            ra_b = pta->b;
        } else if (mode == EX_APOSTINC) {
            /*printf("APOSTINC\n");*/
            pta = ip + ra_a; if (pta >= CoreEnd) pta -= coresize;
            {field_t* f = &(pta->a);
                pta = pta + pta->a; if (pta >= CoreEnd) pta -= coresize;
                ra_a = pta->a;      /* read in registers */
                ra_b = pta->b;
                INCMOD(*f);}
        } else if (mode == EX_BPOSTINC) {
            /*printf("BPOSTINC\n");*/
            pta = ip + ra_a; if (pta >= CoreEnd) pta -= coresize;
            {field_t* f = &(pta->b);
                pta = pta + pta->b; if (pta >= CoreEnd) pta -= coresize;
                ra_a = pta->a;      /* read in registers */
                ra_b = pta->b;
                INCMOD(*f);}
        } else if (mode == EX_APREDEC) {
            /*printf("APREDEC\n");*/
            pta = ip + ra_a; if (pta >= CoreEnd) pta -= coresize;
            DECMOD(pta->a);
            pta = pta + pta->a; if (pta >= CoreEnd) pta -= coresize;
            ra_a = pta->a;      /* read in registers */
            ra_b = pta->b;
        } else if (mode == EX_BPREDEC) {
            /*printf("BPREDEC\n");*/
            pta = ip + ra_a; if (pta >= CoreEnd) pta -= coresize;
            DECMOD(pta->b);
            pta = pta + pta->b; if (pta >= CoreEnd) pta -= coresize;
            ra_a = pta->a;      /* read in registers */
            ra_b = pta->b;
        } else { /* AINDIRECT */
            /*printf("AINDIRECT\n");*/
            pta = ip + ra_a; if (pta >= CoreEnd) pta -= coresize;
            pta = pta + pta->a; if (pta >= CoreEnd) pta -= coresize;
            ra_a = pta->a;      /* read in registers */
            ra_b = pta->b;
        }

        mode = in & (mMASK<<mBITS);

        /* special mov.i code to improve performance */
        if ((in & 16320) == (_OP(EX_MOV, EX_mI) << (mBITS*2))) {
            if (mode == EX_DIRECT<<mBITS) {             
                /*++modes[1];*/
                /* 150886214*/  ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
            } else if (mode == EX_BPOSTINC<<mBITS) {
                /*++modes[5];*/
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                {field_t* f = &(ptb->b);
                    ptb = ptb + *f; if (ptb >= CoreEnd) ptb -= coresize;
                    /*  92075270*/  INCMOD(*f);}
            } else if (mode == EX_AINDIRECT<<mBITS) {
                /*++modes[7];*/
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                /*  39436060*/  ptb = ptb + ptb->a; if (ptb >= CoreEnd) ptb -= coresize;
            } else if (mode == EX_APOSTINC<<mBITS) {
                /*++modes[2];*/
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                {field_t* f = &(ptb->a);
                    ptb = ptb + *f; if (ptb >= CoreEnd) ptb -= coresize;
                    /*  32635122*/  INCMOD(*f);}
            } else if (mode == EX_APREDEC<<mBITS) {
                /*++modes[0];*/
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                DECMOD(ptb->a);
                /*  19211424*/  ptb = ptb + ptb->a; if (ptb >= CoreEnd) ptb -= coresize;
            } else if (mode == EX_BPREDEC<<mBITS) {
                /*++modes[3];*/
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                DECMOD(ptb->b);
                /*  11269800*/  ptb = ptb + ptb->b; if (ptb >= CoreEnd) ptb -= coresize;
            } else if (mode == EX_BINDIRECT<<mBITS) {
                /*++modes[6];*/
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                /*  8582998*/   ptb = ptb + ptb->b; if (ptb >= CoreEnd) ptb -= coresize;
            } else { /* EX_IMMEDIATE */
                /*++modes[4];*/
                /*      1446*/  ptb = ip;
            }
#if SIM_TWO && SIM_EARLY
            dat_count += sim_is_dat(pta->in) - sim_is_dat(ptb->in);
#endif
            ptb->a = ra_a;
            ptb->b = ra_b;
            ptb->in = pta->in;
            IPINCMOD(ip);
            queue(ip);
#if SIM_TWO && SIM_EARLY
            SIM_CHECK_DAT_TIE();
#endif
            goto noqueue;
        }

        
        /*15360:
         *              0  0  1  1  1  1  0  0  0  0  0  0  0  0  0  0
         * bit         15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
         * field   | flags | |- op-code  -| |-.mod-| |b-mode| |a-mode|
         */
        if (!(in & 15360)) {
            /* DAT or SPL */
            if (mode == EX_IMMEDIATE<<mBITS) {
            } else if (mode == EX_DIRECT<<mBITS) {
            } else if (mode == EX_BPOSTINC<<mBITS) {
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                INCMOD(ptb->b);
            } else if (mode == EX_BPREDEC<<mBITS) {
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                DECMOD(ptb->b);
            } else if (mode == EX_APREDEC<<mBITS) {
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                DECMOD(ptb->a);
            } else if (mode == EX_APOSTINC<<mBITS) {
                ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
                INCMOD(ptb->a);
            } /* BINDIRECT, AINDIRECT */

            if (in & 512) {
                /* SPL */
                IPINCMOD(ip);
                queue(ip);
                if ( w->nprocs < processes ) {
                    ++w->nprocs;
                    queue(pta);
                }
#if SIM_EARLY && SIM_TWO
                /* The initial scan proved that no STP exists in core. Preserve
                 * the cheap inherited SPL-only survival test for two players.
                 * Strictly more than cycles/2 is conservative for both turns. */
                if (allow_spl_exit && cycles < max_alive_proc &&
                    w->nprocs * 2 > cycles &&
                    w->succ->nprocs * 2 > cycles) goto out;
#elif SIM_EARLY
                if (mars->earlyExit && cycles < max_alive_proc) {
                    unsigned remaining = cycles - 1;
                    unsigned base = remaining / alive_cnt, extra = remaining % alive_cnt;
                    unsigned slot = 0;
                    w_t *candidate = w->succ;
                    do {
                        if (candidate->nprocs <= base + (slot < extra)) break;
                        candidate = candidate->succ;
                        ++slot;
                    } while (candidate != w->succ);
                    if (slot == (unsigned)alive_cnt) {
                        if (stp_present < 0) stp_present = sim_has_stp(core, coresize);
                        if (!stp_present) goto out;
                    }
                }
#endif


            }
            else {
                /* DAT */
            die:
                if (--w->nprocs) goto noqueue;
                w->pred->succ = w->succ;
                w->succ->pred = w->pred;
                *death_tab++ = w->id;
                cycles = cycles - cycles/alive_cnt; /* nC+k -> (n-1)C+k */
                max_alive_proc = alive_cnt * processes;
                if ( --alive_cnt <= 1 ) 
                    goto out;
            }
            goto noqueue;
        }


        /* b-mode calculation */
        if (mode == EX_APREDEC<<mBITS) {
            /*printf("APREDEC\n");*/
            ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
            DECMOD(ptb->a);
            ptb = ptb + ptb->a; if (ptb >= CoreEnd) ptb -= coresize;
            rb_a = ptb->a;      /* read in registers */
            rb_b = ptb->b;
        } else if (mode == EX_DIRECT<<mBITS) {
            /*printf("DIRECT\n");*/
            ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
            rb_a = ptb->a;
            rb_b = ptb->b;
        } else if (mode == EX_APOSTINC<<mBITS) {
            /*printf("APOSTINC\n");*/
            ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
            {field_t* f = &(ptb->a);
                ptb = ptb + ptb->a; if (ptb >= CoreEnd) ptb -= coresize;
                rb_a = ptb->a;      /* read in registers */
                rb_b = ptb->b;
                INCMOD(*f);}
        } else if (mode == EX_BPREDEC<<mBITS) {
            /*printf("BPREDEC\n");*/
            ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
            DECMOD(ptb->b);
            ptb = ptb + ptb->b; if (ptb >= CoreEnd) ptb -= coresize;
            rb_a = ptb->a;      /* read in registers */
            rb_b = ptb->b;
        } else if (mode == EX_IMMEDIATE<<mBITS) {
            /*printf("IMMEDIATE\n");*/
            ptb = ip;
            /* As pMARS: read the B register after A evaluation, which may
             * have pre-decremented or post-incremented this instruction. */
            rb_a = ip->a;
            rb_b = ip->b;
        } else if (mode == EX_BPOSTINC<<mBITS) {
            /*printf("BPOSTINC\n");*/
            ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
            {field_t* f = &(ptb->b);
                ptb = ptb + ptb->b; if (ptb >= CoreEnd) ptb -= coresize;
                rb_a = ptb->a;      /* read in registers */
                rb_b = ptb->b;
                INCMOD(*f);}
        } else if (mode == EX_BINDIRECT<<mBITS) {
            /*printf("BINDIRECT\n");*/
            ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
            ptb = ptb + ptb->b; if (ptb >= CoreEnd) ptb -= coresize;
            rb_a = ptb->a;      /* read in registers */
            rb_b = ptb->b;
        } else { /* AINDIRECT */
            /*printf("AINDIRECT\n");*/
            ptb = ip + rb_b; if (ptb >= CoreEnd) ptb -= coresize;
            ptb = ptb + ptb->a; if (ptb >= CoreEnd) ptb -= coresize;
            rb_a = ptb->a;      /* read in registers */
            rb_b = ptb->b;
        }

#if DEBUG == 2
        /* Debug output */
        printf("%6d %4ld  %s  |%4ld, d %4ld,%4ld a %4ld,%4ld b %4ld,%4ld\n",
               cycles, ip-core, debug_line,
               w->nprocs, pta-core, ptb-core, 
               ra_a, ra_b, rb_a, rb_b );
#endif

        /*
         * Execute the instruction on opcode.modifier
         */


        switch ( in>>(mBITS*2) ) {

        case _OP(EX_MOV, EX_mA):
            ptb->a = ra_a;
            break;
        case _OP(EX_MOV, EX_mF):
            ptb->a = ra_a;
        case _OP(EX_MOV, EX_mB):
            ptb->b = ra_b;
            break;
        case _OP(EX_MOV, EX_mAB):
            ptb->b = ra_a;
            break;
        case _OP(EX_MOV, EX_mX):
            ptb->b = ra_a;
        case _OP(EX_MOV, EX_mBA):
            ptb->a = ra_b;
            break;

        case _OP(EX_MOV, EX_mI):
            printf("unreachable code reached. You have a problem!\n");
            break;

        case _OP(EX_DJN,EX_mBA):
        case _OP(EX_DJN,EX_mA):
            DECMOD(ptb->a);
            if ( rb_a == 1 ) break;
            queue(pta);
            goto noqueue;

        case _OP(EX_DJN,EX_mAB):
        case _OP(EX_DJN,EX_mB):
            DECMOD(ptb->b);
            if ( rb_b == 1 ) break;
            queue(pta);
            goto noqueue;

        case _OP(EX_DJN,EX_mX):
        case _OP(EX_DJN,EX_mI):
        case _OP(EX_DJN,EX_mF):
            DECMOD(ptb->a);
            DECMOD(ptb->b);
            /* if ( rb_a == 1 && rb_b == 1 ) break; */
            if ( rb_a == 1 && rb_b == 1 ) break;
            queue(pta);
            goto noqueue;


        case _OP(EX_ADD, EX_mI):
        case _OP(EX_ADD, EX_mF):
            ADDMOD(ptb->b, ra_b, rb_b );
        case _OP(EX_ADD, EX_mA):
            ADDMOD(ptb->a, ra_a, rb_a );
            break;
        case _OP(EX_ADD, EX_mB):
            ADDMOD(ptb->b, ra_b, rb_b );
            break;
        case _OP(EX_ADD, EX_mX):
            ADDMOD(ptb->a, ra_b, rb_a );
        case _OP(EX_ADD, EX_mAB):
            ADDMOD(ptb->b, ra_a, rb_b );
            break;
        case _OP(EX_ADD, EX_mBA):
            ADDMOD(ptb->a, ra_b, rb_a );
            break;


        case _OP(EX_JMZ, EX_mBA):
        case _OP(EX_JMZ, EX_mA):
            if ( rb_a )
                break;
            queue(pta);
            goto noqueue;

        case _OP(EX_JMZ, EX_mAB):
        case _OP(EX_JMZ, EX_mB):
            if ( rb_b )
                break;
            queue(pta);
            goto noqueue;

        case _OP(EX_JMZ, EX_mX):
        case _OP(EX_JMZ, EX_mF):
        case _OP(EX_JMZ, EX_mI):
            if ( rb_a || rb_b )
                break;
            queue(pta);
            goto noqueue;


        case _OP(EX_SUB, EX_mI):
        case _OP(EX_SUB, EX_mF):
            SUBMOD(ptb->b, rb_b, ra_b );
        case _OP(EX_SUB, EX_mA):
            SUBMOD(ptb->a, rb_a, ra_a);
            break;
        case _OP(EX_SUB, EX_mB):
            SUBMOD(ptb->b, rb_b, ra_b );
            break;
        case _OP(EX_SUB, EX_mX):
            SUBMOD(ptb->a, rb_a, ra_b );
        case _OP(EX_SUB, EX_mAB):
            SUBMOD(ptb->b, rb_b, ra_a );
            break;
        case _OP(EX_SUB, EX_mBA):
            SUBMOD(ptb->a, rb_a, ra_b );
            break;


        case _OP(EX_SEQ, EX_mA):
            if ( ra_a == rb_a )
                IPINCMOD(ip);
            break;
        case _OP(EX_SEQ, EX_mB):
            if ( ra_b == rb_b )
                IPINCMOD(ip);
            break;
        case _OP(EX_SEQ, EX_mAB):
            if ( ra_a == rb_b )
                IPINCMOD(ip);
            break;
        case _OP(EX_SEQ, EX_mBA):
            if ( ra_b == rb_a )
                IPINCMOD(ip);
            break;

        case _OP(EX_SEQ, EX_mI):
            if ( pta->in != ptb->in )
                break;
        case _OP(EX_SEQ, EX_mF):
            if ( ra_a == rb_a && ra_b == rb_b )
                IPINCMOD(ip);
            break;
        case _OP(EX_SEQ, EX_mX):
            if ( ra_a == rb_b && ra_b == rb_a )
                IPINCMOD(ip);
            break;


        case _OP(EX_SNE, EX_mA):
            if ( ra_a != rb_a )
                IPINCMOD(ip);
            break;
        case _OP(EX_SNE, EX_mB):
            if ( ra_b != rb_b )
                IPINCMOD(ip);
            break;
        case _OP(EX_SNE, EX_mAB):
            if ( ra_a != rb_b )
                IPINCMOD(ip);
            break;
        case _OP(EX_SNE, EX_mBA):
            if ( ra_b != rb_a )
                IPINCMOD(ip);
            break;

        case _OP(EX_SNE, EX_mI):
            if ( pta->in != ptb->in ) {
                IPINCMOD(ip);
                break;
            }
            /* fall through */
        case _OP(EX_SNE, EX_mF):
            if ( ra_a != rb_a || ra_b != rb_b )
                IPINCMOD(ip);
            break;
        case _OP(EX_SNE, EX_mX):
            if ( ra_a != rb_b || ra_b != rb_a )
                IPINCMOD(ip);
            break;


        case _OP(EX_JMN, EX_mBA):
        case _OP(EX_JMN, EX_mA):
            if (! rb_a )
                break;
            queue(pta);
            goto noqueue;

        case _OP(EX_JMN, EX_mAB):
        case _OP(EX_JMN, EX_mB):
            if (! rb_b )
                break;
            queue(pta);
            goto noqueue;

        case _OP(EX_JMN, EX_mX):
        case _OP(EX_JMN, EX_mF):
        case _OP(EX_JMN, EX_mI):
            if (rb_a || rb_b) {
                queue(pta);
                goto noqueue;
            }
            break;


        case _OP(EX_JMP, EX_mA):
        case _OP(EX_JMP, EX_mB):
        case _OP(EX_JMP, EX_mAB):
        case _OP(EX_JMP, EX_mBA):
        case _OP(EX_JMP, EX_mX):
        case _OP(EX_JMP, EX_mF):
        case _OP(EX_JMP, EX_mI):
            queue(pta);
            goto noqueue;



        case _OP(EX_SLT, EX_mA):
            if (ra_a < rb_a)
                IPINCMOD(ip);
            break;
        case _OP(EX_SLT, EX_mAB):
            if (ra_a < rb_b)
                IPINCMOD(ip);
            break;
        case _OP(EX_SLT, EX_mB):
            if (ra_b < rb_b)
                IPINCMOD(ip);
            break;
        case _OP(EX_SLT, EX_mBA):
            if (ra_b < rb_a)
                IPINCMOD(ip);
            break;
        case _OP(EX_SLT, EX_mI):
        case _OP(EX_SLT, EX_mF):
            if (ra_a < rb_a && ra_b < rb_b)
                IPINCMOD(ip);
            break;
        case _OP(EX_SLT, EX_mX):
            if (ra_a < rb_b && ra_b < rb_a)
                IPINCMOD(ip);
            break;


        case _OP(EX_MODM, EX_mI):
        case _OP(EX_MODM, EX_mF):
            if ( ra_a ) ptb->a = rb_a % ra_a;
            if ( ra_b ) ptb->b = rb_b % ra_b;
            if (!ra_a || !ra_b) goto die;
            break;
        case _OP(EX_MODM, EX_mX):
            if ( ra_b ) ptb->a = rb_a % ra_b;
            if ( ra_a ) ptb->b = rb_b % ra_a;
            if (!ra_b || !ra_a) goto die;
            break;
        case _OP(EX_MODM, EX_mA):
            if ( !ra_a ) goto die;
            ptb->a = rb_a % ra_a;
            break;
        case _OP(EX_MODM, EX_mB):
            if ( !ra_b ) goto die;
            ptb->b = rb_b % ra_b;
            break;
        case _OP(EX_MODM, EX_mAB):
            if ( !ra_a ) goto die;
            ptb->b = rb_b % ra_a;
            break;
        case _OP(EX_MODM, EX_mBA):
            if ( !ra_b ) goto die;
            ptb->a = rb_a % ra_b;
            break;


        /* Promote before multiplying: 16-bit fields otherwise promote to
         * signed int, whose product can overflow for large cores. */
        case _OP(EX_MUL, EX_mI):
        case _OP(EX_MUL, EX_mF):
            ptb->b = ((u32_t)rb_b * ra_b) % coresize;
        case _OP(EX_MUL, EX_mA):
            ptb->a = ((u32_t)rb_a * ra_a) % coresize;
            break;
        case _OP(EX_MUL, EX_mB):
            ptb->b = ((u32_t)rb_b * ra_b) % coresize;
            break;
        case _OP(EX_MUL, EX_mX):
            ptb->a = ((u32_t)rb_a * ra_b) % coresize;
        case _OP(EX_MUL, EX_mAB):
            ptb->b = ((u32_t)rb_b * ra_a) % coresize;
            break;
        case _OP(EX_MUL, EX_mBA):
            ptb->a = ((u32_t)rb_a * ra_b) % coresize;
            break;


        case _OP(EX_DIV, EX_mI):
        case _OP(EX_DIV, EX_mF):
            if ( ra_a ) ptb->a = rb_a / ra_a;
            if ( ra_b ) ptb->b = rb_b / ra_b;
            if (!ra_a || !ra_b) goto die;
            break;
        case _OP(EX_DIV, EX_mX):
            if ( ra_b ) ptb->a = rb_a / ra_b;
            if ( ra_a ) ptb->b = rb_b / ra_a;
            if (!ra_b || !ra_a) goto die;
            break;
        case _OP(EX_DIV, EX_mA):
            if ( !ra_a ) goto die;
            ptb->a = rb_a / ra_a;
            break;
        case _OP(EX_DIV, EX_mB):
            if ( !ra_b ) goto die;
            ptb->b = rb_b / ra_b;
            break;
        case _OP(EX_DIV, EX_mAB):
            if ( !ra_a ) goto die;
            ptb->b = rb_b / ra_a;
            break;
        case _OP(EX_DIV, EX_mBA):
            if ( !ra_b ) goto die;
            ptb->a = rb_a / ra_b;
            break;


        case _OP(EX_NOP,EX_mI):
        case _OP(EX_NOP,EX_mX):
        case _OP(EX_NOP,EX_mF):
        case _OP(EX_NOP,EX_mA):
        case _OP(EX_NOP,EX_mAB):
        case _OP(EX_NOP,EX_mB):
        case _OP(EX_NOP,EX_mBA):
            break;

        case _OP(EX_LDP,EX_mA):
            ptb->a = UNSAFE_PSPACE_GET(w->id, ra_a % pspaceSize);
            break;
        case _OP(EX_LDP,EX_mAB):
            ptb->b = UNSAFE_PSPACE_GET(w->id, ra_a % pspaceSize);
            break;
        case _OP(EX_LDP,EX_mBA):
            ptb->a = UNSAFE_PSPACE_GET(w->id, ra_b % pspaceSize);
            break;
        case _OP(EX_LDP,EX_mF):
        case _OP(EX_LDP,EX_mX):
        case _OP(EX_LDP,EX_mI):
        case _OP(EX_LDP,EX_mB):
            ptb->b = UNSAFE_PSPACE_GET(w->id, ra_b % pspaceSize);
            break;

        case _OP(EX_STP,EX_mA):
            UNSAFE_PSPACE_SET(w->id, rb_a % pspaceSize, ra_a);
            break;
        case _OP(EX_STP,EX_mAB):
            UNSAFE_PSPACE_SET(w->id, rb_b % pspaceSize, ra_a);
            break;
        case _OP(EX_STP,EX_mBA):
            UNSAFE_PSPACE_SET(w->id, rb_a % pspaceSize, ra_b);
            break;
        case _OP(EX_STP,EX_mF):
        case _OP(EX_STP,EX_mX):
        case _OP(EX_STP,EX_mI):
        case _OP(EX_STP,EX_mB):
            UNSAFE_PSPACE_SET(w->id, rb_b % pspaceSize, ra_b);
            break;

#if DEBUG > 0
        default:
            alive_cnt = -1;
            goto out;
#endif
        }

        IPINCMOD(ip);
        queue(ip);
    noqueue:

        #if SIM_TWO
        {
        insn_t **tmp = qhead; qhead = other_head; other_head = tmp;
        tmp = qtail; qtail = other_tail; other_tail = tmp;
        w = w == warTab ? warTab + 1 : warTab;
        }
#else
        w = w->succ;
#endif
    } while(--cycles);

out:
#if SIM_TWO
    {
    /* Restore observable queue state on every exit, including DAT and early tie. */
    w->head = qhead; w->tail = qtail;
    w_t *other = w == warTab ? warTab + 1 : warTab;
    other->head = other_head; other->tail = other_tail;
    }
#endif
#if DEBUG == 1
    printf("cycles: %d\n", cycles);
#endif
    return alive_cnt;
}

/*
  void print_counts(void) {
  printf("%10ld;", modes[0]);
  printf("%10ld;", modes[1]);
  printf("%10ld;", modes[2]);
  printf("%10ld;", modes[3]);
  printf("%10ld;", modes[4]);
  printf("%10ld;", modes[5]);
  printf("%10ld;", modes[6]);
  printf("%10ld",  modes[7]);
  printf("\n");
  }
*/
