/*
 * =================================================================
 * python_mars.c
 *
 * (Corrected version: Fixes all known typos and build warnings)
 * =================================================================
 */

#include <Python.h>
#include "structmember.h"
#include <time.h>

#include "exhaust.h"
#include "sim.h"
#include "insn_help.h"

// --- External Function Declarations ---
extern int assemble_warrior(mars_t* mars, char* fName, warrior_struct* w);
extern int assemble_warrior_from_string(mars_t* mars, const char* redcode_string, warrior_struct* w);
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

// Python class cache
static PyObject *pWarriorClass = NULL;
static PyObject *pInstructionClass = NULL;


/*
 * =================================================================
 * Mars Python Type Definition
 * =================================================================
 */
typedef struct {
    PyObject_HEAD
    mars_t *mars;
    int pspaces_saved;  /* mars->pspaces holds the warriors' P-spaces in input order */
} MarsObject;

/* pmars.c's tag for a warrior whose source declared PIN. */
#define PIN_APPEARED -2
/* Largest -F value pMARS accepts (MAXCORESIZE). */
#define MAX_FIXED_POSITION ((INT_MAX >> 1) + 1)


/*
 * Destructor
 */
static void
Mars_dealloc(MarsObject *self)
{
    if (self->mars) {
        sim_free_bufs(self->mars);
        self->mars = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject *) self);
}

/*
 * __new__
 */
static PyObject *
Mars_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    MarsObject *self;
    self = (MarsObject *) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->mars = NULL;
    }
    return (PyObject *) self;
}

/*
 * __init__
 */
static int
Mars_init(MarsObject *self, PyObject *args, PyObject *kwds)
{
    int nWarriors, coreSize, cycles, maxProcesses, maxWarriorLen, minWarriorDistance;

    if (!PyArg_ParseTuple(args, "iiiiii",
                          &nWarriors,
                          &coreSize,
                          &cycles,
                          &maxProcesses,
                          &maxWarriorLen,
                          &minWarriorDistance)) {
        return -1;
    }
    
    self->mars = (mars_t*)malloc(sizeof(mars_t));
    if (self->mars == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate mars_t");
        return -1;
    }
    memset(self->mars, 0, sizeof(mars_t));

        
    self->mars->seed = rng((s32_t)time(0)*0x1d872b41);
    self->mars->rounds = 1;
    self->mars->cycles = cycles;
    self->mars->coresize = coreSize;
    self->mars->processes = maxProcesses;
    self->mars->maxWarriorLength = maxWarriorLen;
    self->mars->minsep = minWarriorDistance;
    self->mars->nWarriors = nWarriors;
    
    self->mars->errorcode = SUCCESS;
    self->mars->errorlevel = WARNING;
    self->mars->saveOper = 0;
    
    if (!sim_alloc_bufs(self->mars)) {
        free(self->mars);
        self->mars = NULL;
        PyErr_SetString(PyExc_MemoryError, "sim_alloc_bufs failed");
        return -1;
    }
    
    self->mars->errmsg[0] = '\0';

    return 0;
}

/*
 * =================================================================
 * Helper function to get Warrior/Instruction classes
 * =================================================================
 */
static int
import_warrior_classes(void)
{
    PyObject *pWarriorModule;

    if (pWarriorClass && pInstructionClass) {
        return 0; // Already imported
    }

    pWarriorModule = PyImport_ImportModule("Warrior");
    if (pWarriorModule == NULL) {
        PyErr_SetString(PyExc_ImportError, "Failed to import 'Warrior.py'. Make sure it's in your PYTHONPATH.");
        return -1;
    }

    pWarriorClass = PyObject_GetAttrString(pWarriorModule, "Warrior");
    if (pWarriorClass == NULL) {
        PyErr_SetString(PyExc_ImportError, "Failed to find 'Warrior' class in Warrior.py");
        Py_DECREF(pWarriorModule);
        return -1;
    }

    pInstructionClass = PyObject_GetAttrString(pWarriorModule, "Instruction");
    if (pInstructionClass == NULL) {
        PyErr_SetString(PyExc_ImportError, "Failed to find 'Instruction' class in Warrior.py");
        Py_DECREF(pWarriorModule);
        Py_DECREF(pWarriorClass);
        return -1;
    }

    Py_DECREF(pWarriorModule);
    return 0;
}


/*
 * =================================================================
 * Mars Methods
 * =================================================================
 */

// Helper: Converts a C warrior_struct into a Python Warrior object.
static PyObject*
create_python_warrior_from_c_struct(mars_t* mars, warrior_struct* w, const char* fileName)
{
    PyObject *pWarrior, *pInstructions, *pInstruction, *pArgs;
    PyObject *pAddrA, *pAddrB; // Temporary objects for strings
    int i;

    pWarrior = PyObject_CallObject(pWarriorClass, NULL);
    if (pWarrior == NULL) return NULL;

    PyObject_SetAttrString(pWarrior, "name", PyUnicode_FromString(w->name));
    PyObject_SetAttrString(pWarrior, "authorName", PyUnicode_FromString(w->authorName));
    PyObject_SetAttrString(pWarrior, "startPos", PyLong_FromLong(w->offset));
    PyObject_SetAttrString(pWarrior, "fileName", PyUnicode_FromString(fileName));
    if (w->pSpaceIndex == PIN_APPEARED) {
        PyObject *pPin = PyLong_FromLong(w->pSpaceIDNumber);
        PyObject_SetAttrString(pWarrior, "pin", pPin);
        Py_XDECREF(pPin);
    }

    pInstructions = PyList_New(0);
    if (pInstructions == NULL) {
        Py_DECREF(pWarrior);
        return NULL;
    }

    for (i=0; i < w->instLen; ++i) {
        mem_struct* cell = w->instBank + i;
        FIELD_T opcode = ((FIELD_T) (cell->opcode & 0xf8)) >> 3;
        FIELD_T modifier = (cell->opcode & 0x07);
        char replaceOpname[5];

        if (opcode == CMP) {
            sprintf(replaceOpname, "%s", opname[SEQ]);
        }
        else {
            sprintf(replaceOpname, "%s", opname[opcode]);
        }
        
        pAddrA = PyUnicode_FromFormat("%c", PM_INDIR_A(cell->A_mode) ? addr_sym[INDIR_A_TO_SYM(cell->A_mode)] : addr_sym[cell->A_mode]);
        pAddrB = PyUnicode_FromFormat("%c", PM_INDIR_A(cell->B_mode) ? addr_sym[INDIR_A_TO_SYM(cell->B_mode)] : addr_sym[cell->B_mode]);

        // Format: (string, string, PyObject, int, PyObject, int)
        pArgs = Py_BuildValue("(ssOiOi)",
                replaceOpname,
                modname[modifier],
                pAddrA,
                cell->A_value,
                pAddrB,
                cell->B_value);
        
        Py_DECREF(pAddrA);
        Py_DECREF(pAddrB);

        if (pArgs == NULL) { // Py_BuildValue failed
            Py_DECREF(pWarrior);
            Py_DECREF(pInstructions);
            return NULL;
        }
        
        pInstruction = PyObject_CallObject(pInstructionClass, pArgs);
        Py_DECREF(pArgs);
        
        if (pInstruction == NULL) {
            Py_DECREF(pWarrior);
            Py_DECREF(pInstructions);
            return NULL;
        }

        PyList_Append(pInstructions, pInstruction);
        Py_DECREF(pInstruction);
    }
    
    PyObject_SetAttrString(pWarrior, "instructions", pInstructions);
    Py_DECREF(pInstructions);

    return pWarrior;
}


/*
 * Mars.parse_file(filename)
 */
static PyObject *
Mars_parse_file(MarsObject *self, PyObject *args)
{
    warrior_struct* w;
    mars_t* mars = self->mars;
    PyObject *pWarrior;
    char* fileName;

    if (!PyArg_ParseTuple(args, "s", &fileName)) {
        return NULL;
    }

    if (import_warrior_classes() != 0) {
        return NULL;
    }

    w = (warrior_struct*)MALLOC(sizeof(warrior_struct));
    if (w == NULL) return PyErr_NoMemory();
    memset(w, 0, sizeof(warrior_struct));
    
    if (assemble_warrior(mars, fileName, w)) {
        PyErr_SetString(PyExc_ValueError, mars->errmsg);
        FREE(w);
        return NULL;
    }
    
    pWarrior = create_python_warrior_from_c_struct(mars, w, fileName);

    FREE(w->name);
    FREE(w->version);
    FREE(w->date);
    FREE(w->authorName);
    FREE(w->instBank);
    FREE(w);
    
    return pWarrior;
}


/*
 * Mars.parse_string(redcode_string)
 */
static PyObject *
Mars_parse_string(MarsObject *self, PyObject *args)
{
    warrior_struct* w;
    mars_t* mars = self->mars;
    const char* redcodeString;
    PyObject *pWarrior;

    if (!PyArg_ParseTuple(args, "s", &redcodeString)) {
        return NULL; 
    }

    if (import_warrior_classes() != 0) {
        return NULL;
    }
    
    w = (warrior_struct*)MALLOC(sizeof(warrior_struct));
    if (w == NULL) return PyErr_NoMemory();
    memset(w, 0, sizeof(warrior_struct));
    
    if (assemble_warrior_from_string(mars, redcodeString, w)) {
        PyErr_SetString(PyExc_ValueError, mars->errmsg);
        FREE(w);
        return NULL;
    }
    
    pWarrior = create_python_warrior_from_c_struct(mars, w, "from_string");

    FREE(w->name);
    FREE(w->version);
    FREE(w->date);
    FREE(w->authorName);
    FREE(w->instBank);
    FREE(w);
    
    return pWarrior;
}


/*
 * Mars.fight(*warriors)
 */
static PyObject *
Mars_fight(MarsObject *self, PyObject *args)
{
    int i, j;
    int seed;
    mars_t* mars = self->mars;
    int nWarriors = PyTuple_Size(args);

    if ((u32_t)nWarriors != mars->nWarriors) {
        PyErr_Format(PyExc_ValueError, "You have to supply %d warriors instead of %d.", mars->nWarriors, nWarriors);
        return NULL;
    }   

    for (i = 0; i < nWarriors; ++i) {        
        warrior_t* warrior = &(mars->warriors[i]);
        insn_t* in = NULL;
        PyObject *pWarrior, *pInstructionsArray, *pStartPos, *pInstruction, *pDataArray;

        pWarrior = PyTuple_GetItem(args, i);

        pInstructionsArray = PyObject_GetAttrString(pWarrior, "instructions");
        pStartPos = PyObject_GetAttrString(pWarrior, "startPos");

        if (!pInstructionsArray || !PyList_Check(pInstructionsArray)) {
            PyErr_SetString(PyExc_TypeError, "warrior.instructions must be a list.");
            return NULL;
        }
        if (!pStartPos || !PyLong_Check(pStartPos)) {
            Py_DECREF(pInstructionsArray);
            PyErr_SetString(PyExc_TypeError, "warrior.startPos must be an integer.");
            return NULL;
        }
        
        warrior->start = (u32_t)PyLong_AsLong(pStartPos);
        warrior->len = (u32_t)PyList_Size(pInstructionsArray);
        in = warrior->code;

        Py_DECREF(pStartPos);

        /* warrior.pin: None (or absent) for no PIN; equal PINs share P-space. */
        warrior->have_pin = 0;
        if (PyObject_HasAttrString(pWarrior, "pin")) {
            PyObject *pPin = PyObject_GetAttrString(pWarrior, "pin");
            if (pPin && pPin != Py_None) {
                long pin = PyLong_AsLong(pPin);
                if (pin == -1 && PyErr_Occurred()) {
                    Py_DECREF(pPin);
                    Py_DECREF(pInstructionsArray);
                    PyErr_SetString(PyExc_TypeError, "warrior.pin must be an integer or None.");
                    return NULL;
                }
                warrior->have_pin = 1;
                warrior->pin = (u32_t)pin;
            }
            Py_XDECREF(pPin);
        }

        for (j = 0; (u32_t)j < warrior->len; ++j) {
            int op, m, ma, mb;
            int flags = 0;
            long tmp;
            
            pInstruction = PyList_GetItem(pInstructionsArray, j);
            pDataArray = PyObject_GetAttrString(pInstruction, "data");
            
            if (!pDataArray || !PyList_Check(pDataArray) || PyList_Size(pDataArray) != 6) {
                PyErr_SetString(PyExc_TypeError, "instruction.data must be a list of 6 integers.");
                Py_DECREF(pInstructionsArray);
                Py_XDECREF(pDataArray);
                return NULL;
            }
            
            if ((u32_t)j == warrior->start) flags |= fl_START;

            op  = (int)PyLong_AsLong(PyList_GetItem(pDataArray, 0));
            m   = (int)PyLong_AsLong(PyList_GetItem(pDataArray, 1));
            ma  = (int)PyLong_AsLong(PyList_GetItem(pDataArray, 2));
            tmp = PyLong_AsLong(PyList_GetItem(pDataArray, 3));
            in->a = MODS(tmp, mars->coresize);
            
            mb  = (int)PyLong_AsLong(PyList_GetItem(pDataArray, 4));
            tmp = PyLong_AsLong(PyList_GetItem(pDataArray, 5));
            in->b = MODS(tmp, mars->coresize);

            in->in = (flags << flPOS) | OP( op, m, ma, mb );
            ++in;
            
            Py_DECREF(pDataArray);
        }
        Py_DECREF(pInstructionsArray);
    }
    
    check_sanity(mars);
    clear_results(mars);
    
    if (mars->fixedPosition) {
        seed = mars->fixedPosition - mars->minsep;
    } else {
        seed = rng(mars->seed);
    }

    /* Each fight is a fresh match, as on a KotH hill: P-space persists between
     * its rounds only. Undo the previous fight's per-round rotation (which
     * would swap ownership after an even number of rounds), drop PIN sharing,
     * then clear: zeroes, and CORESIZE-1 as the last result. */
    if (self->pspaces_saved)
        memcpy(mars->pspacesOrigin, mars->pspaces, sizeof(pspace_t *) * mars->nWarriors);
    sim_reset_pspaces(mars);
    save_pspaces(mars);
    self->pspaces_saved = 1;
    amalgamate_pspaces(mars);

    for (i=0; (u32_t)i < mars->rounds; ++i) {
        int nalive;
        sim_clear_core(mars);

        seed = compute_positions(seed, mars);
        load_warriors(mars);
        set_starting_order(i, mars);

        nalive = sim_mw(mars, mars->startPositions, mars->deaths);
        if (nalive < 0) {
            PyErr_SetString(PyExc_ValueError, mars->errmsg);
            return NULL;
        }

        accumulate_results(mars);
    }
    mars->seed = seed;

    Py_INCREF(self);
    return (PyObject *)self;
}

/*
 * Mars.results_str()
 */
static PyObject *
mars_results_str(MarsObject *self)
{
    char str[32];
    mars_t* mars = self->mars;
    PyObject *rStr, *pTempStr;

    if (mars->nWarriors == 2 && !mars->isMultiWarriorOutput) {
        sprintf(str, "%ld %ld\n", mars->results[0*(mars->nWarriors+1) + 1], mars->results[0*(mars->nWarriors+1) + 2]);
        rStr = PyUnicode_FromString(str);
        
        sprintf(str, "%ld %ld\n", mars->results[1*(mars->nWarriors+1) + 1], mars->results[1*(mars->nWarriors+1) + 2]);
        pTempStr = PyUnicode_FromString(str);
        PyUnicode_AppendAndDel(&rStr, pTempStr);
    } else {
        int i;
        rStr = PyUnicode_FromString("");
        for (i=0; (u32_t)i<mars->nWarriors; i++) {
            int j;
            for (j=1; (u32_t)j<=mars->nWarriors; j++) {                
                sprintf(str, "%ld ", mars->results[i*(mars->nWarriors+1) + j]);
                pTempStr = PyUnicode_FromString(str);
                PyUnicode_AppendAndDel(&rStr, pTempStr);
            }
            sprintf(str, "%ld\n", mars->results[i*(mars->nWarriors+1) + 0]);
            
            // *** THIS IS THE FIX ***
            // Was: pTempStr = PyUnicode_FromString(.str);
            // Now: pTempStr = PyUnicode_FromString(str);
            pTempStr = PyUnicode_FromString(str);
            PyUnicode_AppendAndDel(&rStr, pTempStr);
        }
    }
    return rStr;
}

/*
 * Mars.wins(warrior_nr, survived_count)
 */
static PyObject *
mars_results_wins(MarsObject *self, PyObject *args) {
    int warriorNr, survivedCount;
    mars_t* mars = self->mars;
    
    if (!PyArg_ParseTuple(args, "ii", &warriorNr, &survivedCount)) {
        return NULL;
    }

    if (warriorNr < 0 || (u32_t)warriorNr >= mars->nWarriors) {
        PyErr_Format(PyExc_IndexError, "warrior nr has to be within range [0, %d], but it is %d.", mars->nWarriors-1, warriorNr);
        return NULL;
    }
    if (survivedCount < 1 || (u32_t)survivedCount > mars->nWarriors) {
        PyErr_Format(PyExc_ValueError, "survived counter has to be within range [1, %d], but it is %d.", mars->nWarriors, survivedCount);
        return NULL;
    }

    return PyLong_FromLong(mars->results[warriorNr*(mars->nWarriors+1) + survivedCount]);
}

/*
 * Mars.losses(warrior_nr)
 */
static PyObject *
mars_results_losses(MarsObject *self, PyObject *args) {
    int warriorNr;
    mars_t* mars = self->mars;

    if (!PyArg_ParseTuple(args, "i", &warriorNr)) {
        return NULL;
    }
    
    if (warriorNr < 0 || (u32_t)warriorNr >= mars->nWarriors) {
        PyErr_Format(PyExc_IndexError, "warrior nr has to be within range [0, %d], but it is %d.", mars->nWarriors-1, warriorNr);
        return NULL;
    }
    
    return PyLong_FromLong(mars->results[warriorNr*(mars->nWarriors+1) + 0]);
}

/*
 * Mars.scores()
 */
static PyObject *
mars_scores(MarsObject *self, PyObject *Py_UNUSED(args))
{
    PyObject *rScores;
    mars_t* mars = self->mars;
    int i, survivors;
    unsigned long score;

    rScores = PyList_New(mars->nWarriors);
    if (rScores == NULL) return NULL;

    for (i = 0; (u32_t)i < mars->nWarriors; ++i) {
        score = 0;
        for (survivors = 1; (u32_t)survivors <= mars->nWarriors; ++survivors) {
            score += (long)(mars->results[i*(mars->nWarriors+1) + survivors]) * (long)((mars->nWarriors*mars->nWarriors-1)/survivors);
        }
        PyList_SetItem(rScores, i, PyLong_FromUnsignedLong(score));
    }
    return rScores;
}


/*
 * =================================================================
 * Mars Properties (Getters/Setters)
 * =================================================================
 */

static PyObject *
Mars_get_rounds(MarsObject *self, void *closure)
{
    return PyLong_FromLong(self->mars->rounds);
}

static int
Mars_set_rounds(MarsObject *self, PyObject *value, void *closure)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the rounds attribute");
        return -1;
    }
    long rounds = PyLong_AsLong(value);
    if (PyErr_Occurred()) {
        PyErr_SetString(PyExc_TypeError, "The rounds attribute must be an integer");
        return -1;
    }
    self->mars->rounds = (u32_t)rounds;
    return 0;
}

static PyObject * Mars_get_coresize(MarsObject *self, void *closure) { return PyLong_FromLong(self->mars->coresize); }
static PyObject * Mars_get_fixed_position(MarsObject *self, void *closure) {
    return PyLong_FromLong(self->mars->fixedPosition);
}
static int Mars_set_fixed_position(MarsObject *self, PyObject *value, void *closure) {
    long position;
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the fixed_position attribute");
        return -1;
    }
    position = PyLong_AsLong(value);
    if (position == -1 && PyErr_Occurred()) return -1;
    if (position != 0 && (position < (long)self->mars->minsep || position > MAX_FIXED_POSITION)) {
        PyErr_Format(PyExc_ValueError, "fixed_position must be 0 or %lu..%d, like pMARS -F",
                     (unsigned long)self->mars->minsep, MAX_FIXED_POSITION);
        return -1;
    }
    self->mars->fixedPosition = (int)position;
    return 0;
}
static PyObject * Mars_get_cycles(MarsObject *self, void *closure) { return PyLong_FromLong(self->mars->cycles); }
static PyObject * Mars_get_processes(MarsObject *self, void *closure) { return PyLong_FromLong(self->mars->processes); }
static PyObject * Mars_get_length(MarsObject *self, void *closure) { return PyLong_FromLong(self->mars->maxWarriorLength); }
static PyObject * Mars_get_distance(MarsObject *self, void *closure) { return PyLong_FromLong(self->mars->minsep); }
static PyObject * Mars_get_warriors(MarsObject *self, void *closure) { return PyLong_FromLong(self->mars->nWarriors); }

// Define the properties
static PyGetSetDef Mars_getseters[] = {
    {"rounds", (getter)Mars_get_rounds, (setter)Mars_set_rounds, "Number of rounds to fight", NULL},
    {"fixed_position", (getter)Mars_get_fixed_position, (setter)Mars_set_fixed_position,
     "Placement seed like the CLI's -F (0 = random, default); every fight() then repeats pMARS's placements", NULL},
    {"coresize", (getter)Mars_get_coresize, NULL, "Size of core memory", NULL},
    {"cycles", (getter)Mars_get_cycles, NULL, "Max cycles per round", NULL},
    {"max_processes", (getter)Mars_get_processes, NULL, "Max processes per warrior", NULL},
    {"max_length", (getter)Mars_get_length, NULL, "Max warrior length", NULL},
    {"min_distance", (getter)Mars_get_distance, NULL, "Min distance between warriors", NULL},
    {"warriors", (getter)Mars_get_warriors, NULL, "Number of warriors", NULL},
    {NULL}  /* Sentinel */
};


/*
 * =================================================================
 * Mars Method Definitions
 * =================================================================
 */
static PyMethodDef Mars_methods[] = {
    {"parse_file", (PyCFunction) Mars_parse_file, METH_VARARGS,
     "Parses a Redcode warrior file and returns a Warrior object."},
    {"parse_string", (PyCFunction) Mars_parse_string, METH_VARARGS,
     "Parses a Redcode warrior from a string and returns a Warrior object."},
    {"fight", (PyCFunction) Mars_fight, METH_VARARGS,
     "fight(*warriors): one match of self.rounds rounds, starting from fresh P-space."},
    {"results_str", (PyCFunction) mars_results_str, METH_NOARGS,
     "Returns the fight results as a string."},
    {"wins", (PyCFunction) mars_results_wins, METH_VARARGS,
     "wins(warrior_nr, survived_count) - Get win count."},
    {"losses", (PyCFunction) mars_results_losses, METH_VARARGS,
     "losses(warrior_nr) - Get loss count."},
    {"scores", (PyCFunction) mars_scores, METH_NOARGS,
     "Calculates and returns a list of scores for all warriors."},
    {NULL}  /* Sentinel */
};


/*
 * =================================================================
 * Mars Type Object
 * =================================================================
 */
static PyTypeObject MarsType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "Mars.Mars",
    .tp_doc = "pMARS Corewar Simulator",
    .tp_basicsize = sizeof(MarsObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Mars_new,
    .tp_init = (initproc) Mars_init,
    .tp_dealloc = (destructor) Mars_dealloc,
    .tp_methods = Mars_methods,
    .tp_getset = Mars_getseters,
};


/*
 * =================================================================
 * Module Definition
 * =================================================================
 */

// Module-level methods (for standard() and tiny())
static PyObject *
Mars_standard(PyObject *self, PyObject *args)
{
    PyObject *nWarriors;
    if (!PyArg_ParseTuple(args, "O!", &PyLong_Type, &nWarriors)) {
        PyErr_SetString(PyExc_TypeError, "Argument must be an integer");
        return NULL;
    }

    PyObject *init_args = Py_BuildValue("(Oiiiii)",
        nWarriors, 8000, 80000, 8000, 100, 100);
    
    PyObject *mars_instance = PyObject_CallObject((PyObject *)&MarsType, init_args);
    Py_DECREF(init_args);
    return mars_instance;
}

static PyObject *
Mars_tiny(PyObject *self, PyObject *args)
{
    PyObject *nWarriors;
    if (!PyArg_ParseTuple(args, "O!", &PyLong_Type, &nWarriors)) {
        PyErr_SetString(PyExc_TypeError, "Argument must be an integer");
        return NULL;
    }
    
    PyObject *init_args = Py_BuildValue("(Oiiiii)",
        nWarriors, 800, 8000, 800, 20, 20);
    
    PyObject *mars_instance = PyObject_CallObject((PyObject *)&MarsType, init_args);
    Py_DECREF(init_args);
    return mars_instance;
}


static PyMethodDef MarsModule_methods[] = {
    {"standard", Mars_standard, METH_VARARGS,
     "Creates a Mars instance with standard 8000-core settings."},
    {"tiny", Mars_tiny, METH_VARARGS,
     "Creates a Mars instance with tiny 800-core settings."},
    {NULL, NULL, 0, NULL}
};


static struct PyModuleDef marsmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "Mars",
    .m_doc = "Python interface for the pMARS Corewar simulator.",
    .m_size = -1,
    .m_methods = MarsModule_methods,
};


/*
 * =================================================================
 * Module Initialization Function
 * =================================================================
 */
PyMODINIT_FUNC
PyInit_Mars(void)
{
    PyObject *m;
    if (PyType_Ready(&MarsType) < 0)
        return NULL;

    m = PyModule_Create(&marsmodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&MarsType);
    if (PyModule_AddObject(m, "Mars", (PyObject *) &MarsType) < 0) {
        Py_DECREF(&MarsType);
        Py_DECREF(m);
        return NULL;
    }

    if (import_warrior_classes() != 0) {
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
