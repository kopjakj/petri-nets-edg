# Host-side verification of the edg gate logic (no hardware, no Pico SDK).
# Compiles the REAL ../edg.c against tiny stub headers and checks behaviour.
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Iinc -I..
SRC      = ../edg.c

all: test fb alu interlock
	@echo "--- truth tables ---";        ./test
	@echo "--- feedback (latches) ---";  ./fb
	@echo "--- 1-bit ALU (32 combos) ---"; ./alu
	@echo "--- safety interlock ---";    ./interlock

test:      test.c $(SRC);          $(CC) $(CFLAGS) $^ -o $@
fb:        fb.c $(SRC);            $(CC) $(CFLAGS) $^ -o $@
alu:       alu_sim.c $(SRC);       $(CC) $(CFLAGS) $^ -o $@
interlock: interlock_sim.c $(SRC); $(CC) $(CFLAGS) $^ -o $@

clean:; rm -f test fb alu interlock
.PHONY: all clean
