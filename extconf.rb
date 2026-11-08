require 'mkmf'
$CFLAGS += " -std=gnu17 -DRUBYINTERFACE"
# Explicit sources exclude the Python extension and experimental/test programs.
$srcs = %w[rubyinterface.c pmars.c pspace.c sim.c match.c]
create_makefile("Mars")
