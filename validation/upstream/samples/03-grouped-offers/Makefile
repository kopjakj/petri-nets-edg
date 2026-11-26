CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
PN = ../../petri
EDG = ../common
STUBS = $(EDG)/test/inc
SOURCES = $(PN)/edg_pn_choice.c $(PN)/edg_pn.c $(EDG)/edg.c

.PHONY: run concurrent check clean
run: grouped-offers
	./grouped-offers
concurrent: threaded-offers
	./threaded-offers
check: grouped-offers threaded-offers
	./threaded-offers
	./grouped-offers --check
grouped-offers: main.c $(SOURCES) $(PN)/edg_pn_choice.h $(PN)/edg_pn.h $(EDG)/edg.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections -I$(PN) -I$(EDG) -I$(STUBS) main.c $(SOURCES) $(LDFLAGS) -Wl,--gc-sections -o $@
threaded-offers: threaded.c $(SOURCES) $(PN)/edg_pn_choice.h $(PN)/edg_pn.h $(EDG)/edg.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -ffunction-sections -fdata-sections -I$(PN) -I$(EDG) -I$(STUBS) threaded.c $(SOURCES) $(LDFLAGS) -Wl,--gc-sections -o $@
clean:
	$(RM) grouped-offers threaded-offers
