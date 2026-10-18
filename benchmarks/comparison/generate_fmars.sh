#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TASK_GUILE_ROOT="$TASK_ROOT/dist/guile/root/usr"
export LD_LIBRARY_PATH="$TASK_GUILE_ROOT/lib/x86_64-linux-gnu"
export GUILE_LOAD_PATH="$TASK_GUILE_ROOT/share/guile/3.0"
export GUILE_LOAD_COMPILED_PATH="$TASK_GUILE_ROOT/lib/x86_64-linux-gnu/guile/3.0/ccache"
export GUILE_AUTO_COMPILE=0
cd "$TASK_ROOT/dist/comparison-sources/${1:-fmars-0.0.207}"
"$TASK_GUILE_ROOT/bin/guile-3.0" -s header.scm > fm_types.h
"$TASK_GUILE_ROOT/bin/guile-3.0" -s code.scm < corpus.set > fm_sim_tmp.c
"$TASK_GUILE_ROOT/bin/guile-3.0" -l skel.scm -c '(display includes/defines)' > fm_includes.c
