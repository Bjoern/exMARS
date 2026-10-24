# ===================================================================
# Makefile for exMARS (Standalone Executable)
#
# This builds 'exmars' with the minimal, effective flags.
# It does NOT build the Ruby interface.
# ===================================================================

# Use the system's default C compiler (clang on your Mac)
CC = gcc

# These are the "classic" flags, which you found to be fast and effective.
CFLAGS = -std=gnu17 -O3 -fomit-frame-pointer

SOURCES = pmars.c pspace.c sim.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = exmars

# Default target
all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET)
	@echo "Built $(TARGET)."

# Rule to compile .c files to .o files
$(OBJECTS): exhaust.h sim.h hill_config.h
sim.o: sim_loop.inc

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Cleans up files from BOTH Python and C builds
clean:
	@echo "Cleaning up build artifacts..."
	# Remove C build files
	rm -f $(TARGET) *.o *~ *.da
	
	# Remove Python build artifacts
	rm -rf build dist Mars.egg-info __pycache__
	
	@echo "Clean complete."

# pMARS compatibility regressions (needs Python 3)
test: $(TARGET)
	python tests/pmars_compat/run.py ./$(TARGET)

.PHONY: all clean test

# Specialized builds are separate from generic objects and always recompiled.
# Zero keeps a kernel parameter dynamic. Core+processes was the best tested mix.
PYTHON ?= python
CORESIZE ?= 8000
MAXPROCESSES ?= 8000
CYCLES ?= 0
MAXLENGTH ?= 100
MINSEP ?= 100
SPECIALIZED_DIR ?= bin/hill-c$(CORESIZE)-p$(MAXPROCESSES)-t$(CYCLES)-l$(MAXLENGTH)-d$(MINSEP)
SPECIALIZED_PYTHON ?= 0
ifeq ($(SPECIALIZED_PYTHON),1)
SPECIALIZED_EXTENSION = --python-extension
endif

specialized:
	$(PYTHON) build_fast.py --cc "$(CC)" --flags "$(CPPFLAGS) $(CFLAGS)" --fixed-coresize $(CORESIZE) --fixed-processes $(MAXPROCESSES) --fixed-cycles $(CYCLES) --max-length $(MAXLENGTH) --min-distance $(MINSEP) --output-dir "$(SPECIALIZED_DIR)" $(SPECIALIZED_EXTENSION)

.PHONY: specialized

# Browser/Worker/Node modules. Optional pMARS source enables comparison builds.
EMCC ?= emcc
PMARS_SOURCE ?=
wasm:
	python3 wasm/build.py --emcc "$(EMCC)" $(if $(PMARS_SOURCE),--pmars-source "$(PMARS_SOURCE)")

wasm-test:
	node wasm/test.mjs dist/wasm

.PHONY: wasm wasm-test
