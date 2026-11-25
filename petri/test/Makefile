# Host builds for the EDG-PN layer.
#
#   make                 unit tests + a few quick interleaving searches
#   make pn_freertos FREERTOS_KERNEL_PATH=/abs/path/FreeRTOS-Kernel
#   make clean
CC     ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra

PN     := ..
COMMON := ../../samples/common
CORE_SOURCES := $(PN)/edg_pn.c $(COMMON)/edg.c
CORE_HEADERS := $(PN)/edg_pn.h $(COMMON)/edg.h
CORE_INCLUDES := -I$(PN) -I$(COMMON)
HOST_INCLUDES := $(CORE_INCLUDES) -I$(COMMON)/test/inc

FREERTOS_KERNEL_PATH ?= ../../pico/FreeRTOS-Kernel
KERNEL := $(FREERTOS_KERNEL_PATH)
PORT   := $(KERNEL)/portable/ThirdParty/GCC/Posix
POSIX_SOURCES := $(KERNEL)/tasks.c $(KERNEL)/queue.c $(KERNEL)/list.c \
	$(KERNEL)/portable/MemMang/heap_3.c $(PORT)/port.c \
	$(PORT)/utils/wait_for_event.c
POSIX_INCLUDES := -Iposix $(CORE_INCLUDES) -I$(KERNEL)/include \
	-I$(PORT) -I$(PORT)/utils -I$(COMMON)/test/inc

.PHONY: all check clean
all: check

check: pn_unit pn_explore
	./pn_unit
	./pn_explore fork-join release
	./pn_explore sensor-choice 'release*'
	./pn_explore mutex-idle offer
	./pn_explore --max=200000 kitting 'release*'

pn_unit: pn_unit.c $(CORE_SOURCES) $(CORE_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_INCLUDES) pn_unit.c $(CORE_SOURCES) -o $@

pn_explore: pn_explore.c nets.h $(CORE_SOURCES) $(CORE_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_INCLUDES) pn_explore.c $(CORE_SOURCES) -o $@

pn_freertos: Makefile pn_freertos.c nets.h $(CORE_SOURCES) $(CORE_HEADERS) posix/FreeRTOSConfig.h posix/shim.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(POSIX_INCLUDES) -pthread -include posix/shim.h -c $(PN)/edg_pn.c -o pn_posix_petri.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(POSIX_INCLUDES) -pthread -include posix/shim.h -c $(COMMON)/edg.c -o pn_posix_gate.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(POSIX_INCLUDES) -D_DEFAULT_SOURCE -pthread \
		pn_freertos.c pn_posix_petri.o pn_posix_gate.o $(POSIX_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	$(RM) pn_unit pn_explore pn_freertos pn_posix_petri.o pn_posix_gate.o
