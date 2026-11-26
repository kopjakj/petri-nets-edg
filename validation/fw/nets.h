/*==============================================================================
 *  nets.h  --  every closed net of the study, shared by the explorer
 *              (sims/explorer/pn_explore.c) and the FreeRTOS host run
 *              (rtos/pn_freertos.c): eight validation nets, the extended
 *              free-choice stall net, two sensor-steered choices and the
 *              embedded examples (job shop, level crossing, parking).
 *
 *  A net is given by its places (with initial marking) and, per transition,
 *  the list of input places and the list of output places. Arc weight is 1.
 *============================================================================*/
#ifndef NETS_H
#define NETS_H

#define MAXP 16
#define MAXT 12
#define MAXA 4          /* arcs per side of a transition in these nets */

typedef struct {
    const char *name;
    const char *klass;                     /* structural class, for the report */
    int         np, nt;
    const char *pname[MAXP];
    int         m0[MAXP];
    const char *tname[MAXT];
    int         npre[MAXT];  int pre[MAXT][MAXA];
    int         npost[MAXT]; int post[MAXT][MAXA];
} net_t;

static const net_t NETS[] = {

/* V1  fork-join: a safe marked graph (concurrency + synchronisation). */
{ .name = "fork-join", .klass = "marked graph, safe",
  .np = 5, .nt = 4,
  .pname = { "p0", "p1", "p2", "p3", "p4" },
  .m0    = {  1,    0,    0,    0,    0   },
  .tname = { "fork", "a", "b", "join" },
  .npre  = { 1, 1, 1, 2 }, .pre  = { {0}, {1}, {2}, {3, 4} },
  .npost = { 2, 1, 1, 1 }, .post = { {1, 2}, {3}, {4}, {0} } },

/* V2  producer-consumer with a two-slot buffer: a marked graph, 2-bounded. */
{ .name = "buffer-2", .klass = "marked graph, 2-bounded",
  .np = 6, .nt = 4,
  .pname = { "p_rdy", "p_has", "free", "full", "c_rdy", "c_has" },
  .m0    = {  1,       0,       2,      0,      1,       0      },
  .tname = { "produce", "put", "get", "consume" },
  .npre  = { 1, 2, 2, 1 }, .pre  = { {0}, {1, 2}, {4, 3}, {5} },
  .npost = { 1, 2, 2, 1 }, .post = { {1}, {0, 3}, {5, 2}, {4} } },

/* V3  choice-merge with two tokens: a state machine (free choice). */
{ .name = "choice-merge", .klass = "state machine (free choice), 2-bounded",
  .np = 4, .nt = 5,
  .pname = { "p0", "p1", "p2", "p3" },
  .m0    = {  2,    0,    0,    0   },
  .tname = { "a", "b", "c", "d", "e" },
  .npre  = { 1, 1, 1, 1, 1 }, .pre  = { {0}, {0}, {1}, {2}, {3} },
  .npost = { 1, 1, 1, 1, 1 }, .post = { {1}, {2}, {3}, {3}, {0} } },

/* V4  kitting cell: parts are dispatched to two lanes by a free choice and
 *     joined pairwise. Free choice with both choice and synchronisation; the
 *     net has dead markings (both parts sent to the same lane). */
{ .name = "kitting", .klass = "free choice, 2-bounded",
  .np = 4, .nt = 4,
  .pname = { "in", "qa", "qb", "done" },
  .m0    = {  2,    0,    0,    0     },
  .tname = { "toA", "toB", "join", "ship" },
  .npre  = { 1, 1, 2, 1 }, .pre  = { {0}, {0}, {1, 2}, {3} },
  .npost = { 1, 1, 2, 1 }, .post = { {1}, {2}, {3, 0}, {0} } },

/* V5  mutual exclusion of two cyclic processes: NOT free choice (the
 *     resource place r is shared by two transitions that have other inputs). */
{ .name = "mutex", .klass = "asymmetric choice, one shared input per transition, safe",
  .np = 7, .nt = 6,
  .pname = { "idle1", "want1", "crit1", "idle2", "want2", "crit2", "r" },
  .m0    = {  1,       0,       0,       1,       0,       0,       1  },
  .tname = { "req1", "enter1", "leave1", "req2", "enter2", "leave2" },
  .npre  = { 1, 2, 1, 1, 2, 1 },
  .pre   = { {0}, {1, 6}, {2}, {3}, {4, 6}, {5} },
  .npost = { 1, 1, 2, 1, 1, 2 },
  .post  = { {1}, {2}, {0, 6}, {4}, {5}, {3, 6} } },

/* V6  the same mutex, but process 1 never asks for the resource. */
{ .name = "mutex-idle", .klass = "asymmetric choice, one shared input per transition, safe",
  .np = 7, .nt = 6,
  .pname = { "idle1", "want1", "crit1", "idle2", "want2", "crit2", "r" },
  .m0    = {  0,       0,       0,       1,       0,       0,       1  },
  .tname = { "req1", "enter1", "leave1", "req2", "enter2", "leave2" },
  .npre  = { 1, 2, 1, 1, 2, 1 },
  .pre   = { {0}, {1, 6}, {2}, {3}, {4, 6}, {5} },
  .npost = { 1, 1, 2, 1, 1, 2 },
  .post  = { {1}, {2}, {0, 6}, {4}, {5}, {3, 6} } },

/* V7  two dining philosophers, each taking both forks atomically: asymmetric
 *     choice (but not restricted free choice). */
{ .name = "philosophers-2", .klass = "asymmetric choice, two shared inputs per transition, safe",
  .np = 6, .nt = 4,
  .pname = { "th0", "th1", "ea0", "ea1", "f0", "f1" },
  .m0    = {  1,     1,     0,     0,     1,    1   },
  .tname = { "take0", "take1", "put0", "put1" },
  .npre  = { 3, 3, 1, 1 },
  .pre   = { {0, 4, 5}, {1, 5, 4}, {2}, {3} },
  .npost = { 1, 1, 3, 3 },
  .post  = { {2}, {3}, {0, 4, 5}, {1, 5, 4} } },

/* V8  three dining philosophers, each taking both forks atomically: NOT free
 *     choice, every 'eat' transition has two shared input places. */
{ .name = "philosophers-3", .klass = "general, two shared inputs per transition, safe",
  .np = 9, .nt = 6,
  .pname = { "th0", "th1", "th2", "ea0", "ea1", "ea2", "f0", "f1", "f2" },
  .m0    = {  1,     1,     1,     0,     0,     0,     1,    1,    1   },
  .tname = { "take0", "take1", "take2", "put0", "put1", "put2" },
  .npre  = { 3, 3, 3, 1, 1, 1 },
  .pre   = { {0, 6, 7}, {1, 7, 8}, {2, 8, 6}, {3}, {4}, {5} },
  .npost = { 1, 1, 1, 3, 3, 3 },
  .post  = { {3}, {4}, {5}, {0, 6, 7}, {1, 7, 8}, {2, 8, 6} } },

/* E1  extended free choice: two transitions with the identical preset {p, q}.
 *     Both places are shared by both transitions. */
{ .name = "efc-pair", .klass = "extended free choice, two shared inputs per transition, safe",
  .np = 2, .nt = 2,
  .pname = { "p", "q" }, .m0 = { 1, 1 },
  .tname = { "a", "b" },
  .npre  = { 2, 2 }, .pre  = { {0, 1}, {0, 1} },
  .npost = { 2, 2 }, .post = { {0, 1}, {0, 1} } },

/* S1  a choice steered by condition places: a part goes to lane A only when
 *     sensor A reports okA, to lane B only when sensor B reports okB. Sensor A
 *     never reports (idleA is empty). The part place offers to toA first. */
{ .name = "sensor-choice", .klass = "asymmetric choice, one shared input per transition, safe",
  .np = 7, .nt = 6,
  .pname = { "part", "idleA", "idleB", "okA", "okB", "doneA", "doneB" },
  .m0    = {  1,      0,       1,       0,     0,     0,       0      },
  .tname = { "senseA", "senseB", "toA", "toB", "retA", "retB" },
  .npre  = { 1, 1, 2, 2, 1, 1 },
  .pre   = { {1}, {2}, {0, 3}, {0, 4}, {5}, {6} },
  .npost = { 1, 1, 1, 1, 2, 2 },
  .post  = { {3}, {4}, {5}, {6}, {0, 1}, {0, 2} } },

/* S2  the same choice with both sensors active. */
{ .name = "sensor-choice-2", .klass = "asymmetric choice, one shared input per transition, safe",
  .np = 7, .nt = 6,
  .pname = { "part", "idleA", "idleB", "okA", "okB", "doneA", "doneB" },
  .m0    = {  1,      1,       1,       0,     0,     0,       0      },
  .tname = { "senseA", "senseB", "toA", "toB", "retA", "retB" },
  .npre  = { 1, 1, 2, 2, 1, 1 },
  .pre   = { {1}, {2}, {0, 3}, {0, 4}, {5}, {6} },
  .npost = { 1, 1, 1, 1, 2, 2 },
  .post  = { {3}, {4}, {5}, {6}, {0, 1}, {0, 2} } },

/*---------------------------------------------------------------------------
 *  Embedded examples (also built as RP2040 firmware in pico/models/main.c).
 *  job-shop-rev is the same net with the consumer order of m2 reversed.
 *  The parking scenario is inspired by the IOPT-Tools model
 *  ATPN08_park3levels (use-case inspiration only, not a reduction of it).
 *-------------------------------------------------------------------------*/
{ .name = "job-shop", .klass = "asymmetric choice, one shared input per transition, safe", .np = 8, .nt = 6,
  .pname = { "j1", "j1m1", "j1m2", "j2", "j2m1", "j2m2", "m1", "m2" },
  .m0    = {  1,     0,     0,     1,     0,     0,     1,    1   },
  .tname = { "j1_m1", "j1_m1_m2", "j1_end", "j2_m2", "j2_m2_m1", "j2_end" },
  .npre  = { 2, 2, 1, 2, 2, 1 },
  .pre   = { {0, 6}, {1, 7}, {2}, {3, 7}, {5, 6}, {4} },
  .npost = { 1, 2, 2, 1, 2, 2 },
  .post  = { {1}, {2, 6}, {0, 7}, {5}, {4, 7}, {3, 6} } },

{ .name = "job-shop-rev", .klass = "asymmetric choice, one shared input per transition, safe", .np = 8, .nt = 6,
  .pname = { "j1", "j1m1", "j1m2", "j2", "j2m1", "j2m2", "m1", "m2" },
  .m0    = {  1,     0,     0,     1,     0,     0,     1,    1   },
  .tname = { "j1_m1", "j2_m2", "j1_m1_m2", "j1_end", "j2_m2_m1", "j2_end" },
  .npre  = { 2, 2, 2, 1, 2, 1 },
  .pre   = { {0, 6}, {3, 7}, {1, 7}, {2}, {5, 6}, {4} },
  .npost = { 1, 1, 2, 2, 2, 2 },
  .post  = { {1}, {5}, {2, 6}, {0, 7}, {4, 7}, {3, 6} } },

{ .name = "level-crossing", .klass = "marked graph, safe", .np = 12, .nt = 7,
  .pname = { "far", "near", "zone", "evA", "evB", "okA", "okB", "gUp", "gDown", "mvDown", "mvUp", "go" },
  .m0    = {  1,     0,      0,      0,     0,     1,     1,     1,     0,       0,        0,      0  },
  .tname = { "sense", "lower", "lowered", "cross", "passed", "raise", "raised" },
  .npre  = { 2, 2, 1, 2, 2, 2, 1 },
  .pre   = { {0, 5}, {3, 7}, {9}, {1, 11}, {2, 6}, {4, 8}, {10} },
  .npost = { 2, 2, 2, 1, 2, 2, 1 },
  .post  = { {1, 3}, {9, 5}, {8, 11}, {2}, {0, 4}, {10, 6}, {7} } },

{ .name = "car-park", .klass = "general, paid1 has two shared inputs", .np = 14, .nt = 12,
  .pname = { "Out", "FreeIn1", "WaitingIn1", "EnteringIn1", "FreePlaces", "InArea1", "InArea2",
             "InSlop12", "OnSlop12", "InSlop21", "OnSlop21", "FreeOut1", "WaitingOut1", "EnteringOut1" },
  .m0    = {  3, 1, 0, 0, 2, 0, 0, 1, 0, 1, 0, 1, 0, 0 },
  .tname = { "ArriveIn1", "LeaveIn1", "got_ticket1", "EnteredIn1", "Enter12", "Exit12",
             "Enter21", "Exit21", "ArriveOut1", "LeaveOut1", "paid1", "EnteredOut1" },
  .npre  = { 2, 1, 2, 1, 2, 1, 2, 1, 1, 1, 2, 1 },
  .pre   = { {0, 1}, {2}, {2, 4}, {3}, {5, 7}, {8}, {6, 9}, {10}, {11}, {12}, {12, 5}, {13} },
  .npost = { 1, 2, 2, 1, 1, 2, 1, 2, 1, 1, 2, 2 },
  .post  = { {2}, {1, 0}, {3, 5}, {1}, {8}, {6, 7}, {10}, {5, 9}, {12}, {11}, {13, 4}, {11, 0} } },
};

#define N_NETS ((int)(sizeof NETS / sizeof NETS[0]))

#endif /* NETS_H */
