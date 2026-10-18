CC = gcc
CFLAGS = -std=gnu17 -O3 -fomit-frame-pointer
SOURCES = pmars.c pspace.c sim.c

exmars: $(SOURCES) exhaust.h sim.h insn.h pspace.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@

test: exmars
	python tests/pmars_compat/run.py ./exmars

clean:
	rm -f exmars exmars.exe *.o

.PHONY: test clean
