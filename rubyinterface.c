#ifdef RUBYINTERFACE

#include "ruby.h"
#include "exhaust.h"
#include "sim.h"
#include "insn_help.h"
#include <time.h>
#include <limits.h>

extern int assemble_warrior(mars_t* mars, char* fName, warrior_struct* w);
extern void check_sanity(mars_t* mars);
extern void clear_results(mars_t* mars);
extern void save_pspaces(mars_t* mars);
extern void amalgamate_pspaces(mars_t* mars);
extern void load_warriors(mars_t* mars);
extern void set_starting_order(unsigned int round, mars_t* mars);
extern void accumulate_results(mars_t* mars);
extern s32_t compute_positions(s32_t seed, mars_t* mars);
extern s32_t rng(s32_t seed);
extern void panic(char *msg);




VALUE cMars;
#define PIN_APPEARED -2
static const rb_data_type_t mars_type = {
    "exmars/Mars", {0, sim_free_bufs, 0}, 0, 0, RUBY_TYPED_FREE_IMMEDIATELY
};


static VALUE mars_parse(VALUE self, VALUE fileName) {
    warrior_struct* w;
    mars_t* mars;
    VALUE rWarrior;
    VALUE rInstructions;
    int i;

    /* bail out if filename is no string */
    Check_Type(fileName, T_STRING);

    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    
    /* init pmars warrior data structure */
    w = (warrior_struct*)MALLOC(sizeof(warrior_struct));
    memset(w, 0, sizeof(warrior_struct));
    w->fileName = StringValueCStr(fileName);
    if (assemble_warrior(mars, w->fileName, w)) {
        FREE(w->name);
        FREE(w->version);
        FREE(w->date);
        FREE(w->authorName);
        FREE(w->instBank);
        FREE(w);
        rb_raise(rb_eArgError, "Could not assemble warrior");
    }
    
    /* generate ruby data structure */
    rWarrior = rb_class_new_instance(0, NULL, rb_path2class("Warrior"));
    rb_iv_set(rWarrior, "@name", rb_str_new2(w->name));
    rb_iv_set(rWarrior, "@authorName", rb_str_new2(w->authorName));
    rb_iv_set(rWarrior, "@startPos", INT2FIX(w->offset));   
    rb_iv_set(rWarrior, "@fileName", fileName);
    rb_iv_set(rWarrior, "@pin", w->pSpaceIndex == PIN_APPEARED ? LONG2NUM(w->pSpaceIDNumber) : Qnil);

    rInstructions = rb_ary_new();

    for (i=0; i < w->instLen; ++i) {
        mem_struct* cell = w->instBank + i;
        FIELD_T opcode = ((FIELD_T) (cell->opcode & 0xf8)) >> 3;
        FIELD_T modifier = (cell->opcode & 0x07);
        char instrStr[256];
        VALUE rInstruction;
        char replaceOpname[5];


        if (opcode == CMP) {
            strcpy(replaceOpname, opname[SEQ]);
        }
        else strcpy(replaceOpname, opname[opcode]);
        
        /* build new instruction */
        sprintf(instrStr, "Instruction.new(\"%s\", \"%s\", \"%c\", %d, \"%c\", %d)",
                replaceOpname,
                modname[modifier],
                PM_INDIR_A(cell->A_mode) ? addr_sym[INDIR_A_TO_SYM(cell->A_mode)] : addr_sym[cell->A_mode],
                cell->A_value,
                PM_INDIR_A(cell->B_mode) ? addr_sym[INDIR_A_TO_SYM(cell->B_mode)] : addr_sym[cell->B_mode],
                cell->B_value);
        rb_ary_push(rInstructions, rb_eval_string(instrStr));
    }   
    rb_iv_set(rWarrior, "@instructions", rInstructions);
    FREE(w->name);
    FREE(w->version);
    FREE(w->date);
    FREE(w->authorName);
    FREE(w->instBank);
    FREE(w);
    return rWarrior;
}

VALUE mars_new(VALUE rClass,
               VALUE rWarriors, 
               VALUE rCoreSize, 
               VALUE rCycles, 
               VALUE rMaxProcesses, 
               VALUE rMaxWarriorLen, 
               VALUE rMinWarriorDistance) 
{
    mars_t* mars;
    VALUE rMarsData;
    
    Check_Type(rCycles, T_FIXNUM);
    Check_Type(rWarriors, T_FIXNUM);
    Check_Type(rCoreSize, T_FIXNUM);
    Check_Type(rCycles, T_FIXNUM);
    Check_Type(rMaxProcesses, T_FIXNUM);
    Check_Type(rMaxWarriorLen, T_FIXNUM);
    Check_Type(rMinWarriorDistance, T_FIXNUM);
    
    if (NUM2LONG(rWarriors) < 1 || NUM2LONG(rCoreSize) < 1 ||
        NUM2LONG(rCycles) < 1 || NUM2LONG(rMaxProcesses) < 1 ||
        NUM2LONG(rMaxWarriorLen) < 1 || NUM2LONG(rMinWarriorDistance) < 0)
        rb_raise(rb_eArgError, "Invalid simulator settings");
    mars = (mars_t*)malloc(sizeof(mars_t));
    if (!mars) rb_memerror();
    memset(mars, 0, sizeof(mars_t));
        
    mars->seed = rng((s32_t)((u32_t)time(0)*0x1d872b41UL));
    mars->rounds = 1;
    mars->cycles = FIX2INT(rCycles);
    mars->coresize = FIX2INT(rCoreSize);
    mars->processes = FIX2INT(rMaxProcesses);
    mars->maxWarriorLength = FIX2INT(rMaxWarriorLen);
    mars->minsep = FIX2INT(rMinWarriorDistance);
    mars->nWarriors = FIX2INT(rWarriors);
    /* pmars */
    mars->errorcode = SUCCESS;
    mars->errorlevel = WARNING;
    mars->saveOper = 0;
    
    if (mars->nWarriors < 1 || mars->coresize < 1 || mars->coresize > 65535 ||
        mars->cycles < 1 || mars->processes < 1 || mars->maxWarriorLength < 1 ||
        mars->maxWarriorLength > mars->coresize || mars->minsep > mars->coresize ||
        mars->nWarriors > mars->coresize / (mars->minsep ? mars->minsep : mars->maxWarriorLength)) {
        free(mars);
        rb_raise(rb_eArgError, "Invalid simulator settings");
    }
    if (!sim_alloc_bufs(mars)) rb_memerror();
    /* Keep input-order ownership stable, even after a rotated final round. */
    save_pspaces(mars);
    clear_results(mars);
    
    mars->errmsg[0] = '\0';                /* reserve for future */

    rMarsData = TypedData_Wrap_Struct(rClass, &mars_type, mars);
    rb_obj_call_init(rMarsData, 0, 0);
    return rMarsData;
}

VALUE mars_new_standard(VALUE rClass, VALUE rWarriors) {
    return mars_new(rClass, 
                    rWarriors, 
                    INT2FIX(8000),
                    INT2FIX(80000), 
                    INT2FIX(8000), 
                    INT2FIX(100), 
                    INT2FIX(100));
}

VALUE mars_new_tiny(VALUE rClass, VALUE rWarriors) {
    return mars_new(rClass, 
                    rWarriors, 
                    INT2FIX(800), 
                    INT2FIX(8000), 
                    INT2FIX(800), 
                    INT2FIX(20), 
                    INT2FIX(20));
}
VALUE mars_initialize(VALUE self) {
    mars_t* mars;
    int rounds = 1;
    rb_iv_set(self, "@rounds", INT2FIX(rounds));
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    mars->rounds = rounds;
    return self;
}

VALUE mars_get_rounds(VALUE self) {
    return rb_iv_get(self, "@rounds");
}

VALUE mars_get_coresize(VALUE self) {
    mars_t* mars;
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    return INT2FIX(mars->coresize);
}

VALUE mars_get_cycles(VALUE self) {
    mars_t* mars;
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    return INT2FIX(mars->cycles);
}

VALUE mars_get_processes(VALUE self) {
    mars_t* mars;
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    return INT2FIX(mars->processes);
}

VALUE mars_get_length(VALUE self) {
    mars_t* mars;
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    return INT2FIX(mars->maxWarriorLength);
}


VALUE mars_get_distance(VALUE self) {
    mars_t* mars;
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    return INT2FIX(mars->minsep);
}


VALUE mars_get_warriors(VALUE self) {
    mars_t* mars;
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    return INT2FIX(mars->nWarriors);
}

VALUE mars_get_fixed_position(VALUE self) {
    mars_t *mars;
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    return INT2NUM(mars->fixedPosition);
}

VALUE mars_set_fixed_position(VALUE self, VALUE value) {
    mars_t *mars;
    long position = NUM2LONG(value);
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    if (position != 0 && (position < (long)mars->minsep || position > ((INT_MAX >> 1) + 1)))
        rb_raise(rb_eArgError, "fixed_position must be 0 or minimum separation..1073741824");
    mars->fixedPosition = (int)position;
    return value;
}

VALUE mars_set_rounds(VALUE self, VALUE newVal) {
    mars_t* mars;
    Check_Type(newVal, T_FIXNUM);
    if (NUM2INT(newVal) < 1) rb_raise(rb_eArgError, "rounds must be positive");
    rb_iv_set(self, "@rounds", newVal);
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    mars->rounds = NUM2INT(newVal);
    return self;
}

VALUE mars_fight(int argc, VALUE* argv, VALUE self) {
    int i, j, k;
    int seed;
    mars_t* mars;

    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    if (argc != mars->nWarriors) {
        rb_raise(rb_eArgError, "You have to supply %d warriors instead of %d.", mars->nWarriors, argc);
    }   

    /* convert */
    for (i=0; i<argc; ++i) {        
        warrior_t* warrior = &(mars->warriors[i]);
        insn_t* in = NULL;
        VALUE rInstructionsArray = rb_iv_get(argv[i], "@instructions");
        VALUE rStartPos = rb_iv_get(argv[i], "@startPos");

        Check_Type(rInstructionsArray, T_ARRAY);
        Check_Type(rStartPos, T_FIXNUM);
        warrior->start = NUM2INT(rStartPos);
        warrior->len = RARRAY_LEN(rInstructionsArray);
        if (warrior->len < 1 || warrior->len > mars->maxWarriorLength ||
            warrior->start >= warrior->len || (mars->minsep && warrior->len > mars->minsep))
            rb_raise(rb_eArgError, "Invalid warrior length or start position");
        VALUE pin = rb_iv_get(argv[i], "@pin");
        warrior->have_pin = !NIL_P(pin);
        warrior->pin = NIL_P(pin) ? 0 : (u32_t)NUM2LONG(pin);
        in = warrior->code;
        
        for (j=0; j<warrior->len; ++j) {
            int op, m, ma, mb;
            int flags = 0;
            VALUE rTmp;
            int tmp;
            
            VALUE rInsArray = rb_iv_get(rb_ary_entry(rInstructionsArray, j), "@data");
            Check_Type(rInsArray, T_ARRAY);
            
            if (RARRAY_LEN(rInsArray) != 6) rb_raise(rb_eArgError, "Instruction needs six fields");
            if (j == warrior->start) flags |= fl_START;
            rTmp = rb_ary_entry(rInsArray, 0);
            Check_Type(rTmp, T_FIXNUM);         
            op = NUM2INT(rTmp);

            rTmp = rb_ary_entry(rInsArray, 1);
            Check_Type(rTmp, T_FIXNUM);         
            m = NUM2INT(rTmp);

            rTmp = rb_ary_entry(rInsArray, 2);
            Check_Type(rTmp, T_FIXNUM);         
            ma = NUM2INT(rTmp);
            
            rTmp = rb_ary_entry(rInsArray, 4);
            Check_Type(rTmp, T_FIXNUM);         
            mb = NUM2INT(rTmp);
            
            rTmp = rb_ary_entry(rInsArray, 3);
            Check_Type(rTmp, T_FIXNUM);
            tmp = NUM2INT(rTmp);
            in->a = MODS(tmp, mars->coresize);
            
            rTmp = rb_ary_entry(rInsArray, 5);
            Check_Type(rTmp, T_FIXNUM);
            tmp = NUM2INT(rTmp);
            in->b = MODS(tmp, mars->coresize);

            if (op < 0 || op > STP || m < 0 || m > 6 || ma < 0 || ma > 7 || mb < 0 || mb > 7)
                rb_raise(rb_eArgError, "Invalid instruction encoding");
            in->in = (flags << flPOS) | OP( op, m, ma, mb );
            ++in;
        }
    }
    
    check_sanity(mars);
    clear_results(mars);
    /* seed = mars->fixedPosition ? mars->fixedPosition - mars->minsep : rng((s32_t)((u32_t)time(0)*0x1d872b41UL)); */
    if (mars->fixedPosition) {
        seed = mars->fixedPosition - mars->minsep;
    } else {
        seed = rng(mars->seed);
    }

    memcpy(mars->pspacesOrigin, mars->pspaces, sizeof(pspace_t *) * mars->nWarriors);
    sim_reset_pspaces(mars);
    amalgamate_pspaces(mars);   /* Share P-spaces with equal PINs */

    /* Fight rounds rounds. */
    for (i=0; i < mars->rounds; ++i) {
        int nalive;
        sim_clear_core(mars);

        seed = compute_positions(seed, mars);
        load_warriors(mars);
        set_starting_order(i, mars);

        nalive = sim_mw(mars, mars->startPositions, mars->deaths);
        if (nalive<0)
            panic("simulator panic!\n");    

        accumulate_results(mars);
    }
    mars->seed = seed;

    /*
      output_results(mars); 
    */
    
    /* TODO: result */
    return self;
}

VALUE mars_results_wins(VALUE self, VALUE rWarriorNr, VALUE rSurvivedCount) {
    int warriorNr;
    int survivedCount;
    mars_t* mars;
    VALUE rRetval;
    
    Check_Type(rWarriorNr, T_FIXNUM);
    Check_Type(rSurvivedCount, T_FIXNUM);   
    warriorNr = NUM2INT(rWarriorNr);
    survivedCount = NUM2INT(rSurvivedCount);
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    
    if (warriorNr < 0 || warriorNr >= mars->nWarriors) {
        rb_raise(rb_eRangeError, "warrior nr has to be within range [0, %d], but it is %d.", mars->nWarriors-1, warriorNr);
    }
    if (survivedCount < 1 || survivedCount > mars->nWarriors) {
        rb_raise(rb_eRangeError, "survived counter has to be within range [1, %d], but it is %d.", mars->nWarriors, warriorNr);
    }

    rRetval = INT2FIX(mars->results[warriorNr*(mars->nWarriors+1) + survivedCount]);
    return rRetval;
}

VALUE mars_results_losses(VALUE self, VALUE rWarriorNr) {
    int warriorNr;
    VALUE rRetval;
    mars_t* mars;

    Check_Type(rWarriorNr, T_FIXNUM);
    warriorNr = NUM2INT(rWarriorNr);
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);

    if (warriorNr < 0 || warriorNr >= mars->nWarriors) {
        rb_raise(rb_eRangeError, "warrior nr has to be within range [0, %d], but it is %d.", mars->nWarriors-1, warriorNr);
    }
    
    rRetval = INT2FIX(mars->results[warriorNr*(mars->nWarriors+1) + 0]);
    return rRetval;
}

VALUE mars_results_str(VALUE self) {
    char str[32];
    mars_t* mars;
    VALUE rStr;

    TypedData_Get_Struct(self, mars_t, &mars_type, mars);

    if (mars->nWarriors == 2 && !mars->isMultiWarriorOutput) {
        sprintf(str, "%ld %ld\n", mars->results[0*(mars->nWarriors+1) + 1], mars->results[0*(mars->nWarriors+1) + 2]);
        rStr = rb_str_new(str, strlen(str));
        sprintf(str, "%ld %ld\n", mars->results[1*(mars->nWarriors+1) + 1], mars->results[1*(mars->nWarriors+1) + 2]);
        rStr = rb_str_concat(rStr, rb_str_new(str, strlen(str)));
    } else {
        int i;
        rStr = rb_str_new("", 0);
        for (i=0; i<mars->nWarriors; i++) {
            int j;
            for (j=1; j<=mars->nWarriors; j++) {                
                sprintf(str, "%ld ", mars->results[i*(mars->nWarriors+1) + j]);
                rStr = rb_str_concat(rStr, rb_str_new(str, strlen(str)));
            }
            sprintf(str, "%ld\n", mars->results[i*(mars->nWarriors+1) + 0]);
            rStr = rb_str_concat(rStr, rb_str_new(str, strlen(str)));
        }
    }
    return rStr;
}

VALUE mars_score(VALUE self, VALUE rWarriorNr) {
    int warriorNr;
    int survivors;
    VALUE rScore;
    mars_t* mars;
    unsigned long score;

    Check_Type(rWarriorNr, T_FIXNUM);
    warriorNr = NUM2INT(rWarriorNr);
    TypedData_Get_Struct(self, mars_t, &mars_type, mars);

    if (warriorNr < 0 || warriorNr >= mars->nWarriors) {
        rb_raise(rb_eRangeError, "warrior nr has to be within range [0, %d], but it is %d.", mars->nWarriors-1, warriorNr);
    }
    
    score = 0;
    if (rb_block_given_p()) {
        VALUE rParams = rb_ary_new2(2);
        rb_ary_store(rParams, 0, INT2NUM(mars->nWarriors));
        for (survivors=1; survivors <= mars->nWarriors; ++survivors) {      
            VALUE rYieldResult;
            rb_ary_store(rParams, 1, INT2NUM(survivors));
            rYieldResult = rb_yield(rParams);
            Check_Type(rYieldResult, T_FIXNUM);
            
            score += mars->results[warriorNr*(mars->nWarriors+1) + survivors] * FIX2INT(rYieldResult);
        }
    }
    else {
        for (survivors=1; survivors <= mars->nWarriors; ++survivors) {
            score += (long)(mars->results[warriorNr*(mars->nWarriors+1) + survivors]) * (long)((mars->nWarriors*mars->nWarriors-1)/survivors);
        }
    }
    return INT2NUM(score);
}

VALUE mars_scores(VALUE self) {
    VALUE rScores;
    mars_t* mars;
    int i;

    TypedData_Get_Struct(self, mars_t, &mars_type, mars);
    rScores = rb_ary_new2(mars->nWarriors);
    for (i=0; i<mars->nWarriors; ++i) {
        rb_ary_store(rScores, i, mars_score(self, INT2FIX(i)));
    }
    return rScores;
}

RUBY_FUNC_EXPORTED void Init_Mars(void) {
    rb_require("Warrior");
    cMars = rb_define_class("Mars", rb_cObject);
    rb_undef_alloc_func(cMars);
    rb_define_singleton_method(cMars, "new", mars_new, 6);
    rb_define_singleton_method(cMars, "standard", mars_new_standard, 1);
    rb_define_singleton_method(cMars, "tiny", mars_new_tiny, 1);

    rb_define_method(cMars, "rounds", mars_get_rounds, 0);
    rb_define_method(cMars, "coresize", mars_get_coresize, 0);
    rb_define_method(cMars, "cycles", mars_get_cycles, 0);
    rb_define_method(cMars, "max_processes", mars_get_processes, 0);
    rb_define_method(cMars, "max_length", mars_get_length, 0);
    rb_define_method(cMars, "min_distance", mars_get_distance, 0);
    rb_define_method(cMars, "warriors", mars_get_warriors, 0);
    rb_define_method(cMars, "cycles", mars_get_cycles, 0);

    rb_define_method(cMars, "fixed_position", mars_get_fixed_position, 0);
    rb_define_method(cMars, "fixed_position=", mars_set_fixed_position, 1);
    rb_define_method(cMars, "rounds=", mars_set_rounds, 1);

    rb_define_method(cMars, "initialize", mars_initialize, 0);

    rb_define_method(cMars, "parse", mars_parse, 1);
    rb_define_method(cMars, "fight", mars_fight, -1);
    rb_define_method(cMars, "results_str", mars_results_str, 0);
    /* rb_define_method(cMars, "results", mars_results, 0); */

    rb_define_method(cMars, "wins", mars_results_wins, 2);
    rb_define_method(cMars, "losses", mars_results_losses, 1);
    
    rb_define_method(cMars, "score", mars_score, 1);
    rb_define_method(cMars, "scores", mars_scores, 0);
}

#endif
