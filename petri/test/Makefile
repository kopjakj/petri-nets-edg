CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra

COMMON := ../../samples/common
CORE_SOURCES := ../edg_pn.c $(COMMON)/edg.c
CORE_HEADERS := ../edg_pn.h $(COMMON)/edg.h
CORE_INCLUDES := -I.. -I$(COMMON)
HOST_INCLUDES := $(CORE_INCLUDES) -I$(COMMON)/test/inc

FREERTOS_KERNEL_PATH ?= ../../pico/FreeRTOS-Kernel
KERNEL := $(FREERTOS_KERNEL_PATH)
PORT := $(KERNEL)/portable/ThirdParty/GCC/Posix
POSIX_SOURCES := $(KERNEL)/tasks.c $(KERNEL)/queue.c $(KERNEL)/list.c \
	$(KERNEL)/portable/MemMang/heap_3.c $(PORT)/port.c \
	$(PORT)/utils/wait_for_event.c
POSIX_INCLUDES := -Iposix $(CORE_INCLUDES) -I$(KERNEL)/include \
	-I$(PORT) -I$(PORT)/utils -I$(COMMON)/test/inc

.PHONY: all check clean
all: check

check: pn_unit pn_explore
	./pn_unit
	./pn_explore fork-join credit
	./pn_explore kitting 'credit*'
	./pn_explore mutex-idle credit
	./pn_explore --max=100000 fork-join naive-preset

pn_unit: pn_unit.c $(CORE_SOURCES) $(CORE_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_INCLUDES) $(filter %.c,$^) $(LDFLAGS) $(LDLIBS) -o $@

pn_explore: pn_explore.c nets.h $(CORE_SOURCES) $(CORE_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_INCLUDES) $(filter %.c,$^) $(LDFLAGS) $(LDLIBS) -o $@

pn_freertos: Makefile pn_freertos.c nets.h $(CORE_SOURCES) $(CORE_HEADERS) posix/FreeRTOSConfig.h posix/shim.h $(POSIX_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(POSIX_INCLUDES) -pthread -include posix/shim.h -c ../edg_pn.c -o pn_posix_petri.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(POSIX_INCLUDES) -pthread -include posix/shim.h -c $(COMMON)/edg.c -o pn_posix_gate.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(POSIX_INCLUDES) -D_DEFAULT_SOURCE -pthread \
		pn_freertos.c pn_posix_petri.o pn_posix_gate.o $(POSIX_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f pn_unit pn_explore pn_freertos pn_posix_petri.o pn_posix_gate.o
