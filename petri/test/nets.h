/*==============================================================================
 *  nets.h  --  the validation nets, shared by the explorer (pn_explore.c) and
 *              the FreeRTOS run (pn_freertos.c).
 *
 *  A net is given by its places (with initial marking) and, per transition,
 *  the list of input places and the list of output places. Arc weight is 1.
 *============================================================================*/
#ifndef NETS_H
#define NETS_H

#define MAXP 16
#define MAXT 10
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

/* V2  producer-consumer with a two-slot buffer: conflict-free, 2-bounded. */
{ .name = "buffer-2", .klass = "conflict-free, 2-bounded",
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
{ .name = "mutex", .klass = "asymmetric choice (not free choice), safe",
  .np = 7, .nt = 6,
  .pname = { "idle1", "want1", "crit1", "idle2", "want2", "crit2", "r" },
  .m0    = {  1,       0,       0,       1,       0,       0,       1  },
  .tname = { "req1", "enter1", "leave1", "req2", "enter2", "leave2" },
  .npre  = { 1, 2, 1, 1, 2, 1 },
  .pre   = { {0}, {1, 6}, {2}, {3}, {4, 6}, {5} },
  .npost = { 1, 1, 2, 1, 1, 2 },
  .post  = { {1}, {2}, {0, 6}, {4}, {5}, {3, 6} } },

/* V6  the same mutex, but process 1 never asks for the resource. */
{ .name = "mutex-idle", .klass = "asymmetric choice (not free choice), safe",
  .np = 7, .nt = 6,
  .pname = { "idle1", "want1", "crit1", "idle2", "want2", "crit2", "r" },
  .m0    = {  0,       0,       0,       1,       0,       0,       1  },
  .tname = { "req1", "enter1", "leave1", "req2", "enter2", "leave2" },
  .npre  = { 1, 2, 1, 1, 2, 1 },
  .pre   = { {0}, {1, 6}, {2}, {3}, {4, 6}, {5} },
  .npost = { 1, 1, 2, 1, 1, 2 },
  .post  = { {1}, {2}, {0, 6}, {4}, {5}, {3, 6} } },

/* V7  two dining philosophers, each taking both forks atomically: NOT free
 *     choice, every 'take' transition has two shared input places. */
{ .name = "philosophers-2", .klass = "not free choice, safe",
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
{ .name = "philosophers-3", .klass = "not free choice, safe",
  .np = 9, .nt = 6,
  .pname = { "th0", "th1", "th2", "ea0", "ea1", "ea2", "f0", "f1", "f2" },
  .m0    = {  1,     1,     1,     0,     0,     0,     1,    1,    1   },
  .tname = { "take0", "take1", "take2", "put0", "put1", "put2" },
  .npre  = { 3, 3, 3, 1, 1, 1 },
  .pre   = { {0, 6, 7}, {1, 7, 8}, {2, 8, 6}, {3}, {4}, {5} },
  .npost = { 1, 1, 1, 3, 3, 3 },
  .post  = { {3}, {4}, {5}, {0, 6, 7}, {1, 7, 8}, {2, 8, 6} } },

/*---------------------------------------------------------------------------
 *  Application examples (Section 6 of the paper), as CLOSED nets: the
 *  environment (sensors, the radio link, vehicles) is modelled by places and
 *  transitions of its own, so that the nets can run without external input.
 *-------------------------------------------------------------------------*/

/* A1  IoT data-collection gateway. Three sensor nodes each deliver a reading;
 *     one reading from every node is merged into a frame; frames are sent
 *     under a window of two unacknowledged frames. Conflict-free.
 *     xk/net/ack model the radio link returning an acknowledgement. The place
 *     `space` (a frame buffer of two) is what makes the CLOSED net bounded:
 *     without it the nodes could produce frames without limit. */
{ .name = "iot-gateway", .klass = "conflict-free, 2-bounded",
  .np = 12, .nt = 7,
  .pname = { "n1", "n2", "n3", "r1", "r2", "r3", "frame", "win", "flight", "xk", "ack", "space" },
  .m0    = {  1,    1,    1,    0,    0,    0,    0,       2,     0,        0,    0,     2      },
  .tname = { "sample1", "sample2", "sample3", "merge", "send", "net", "confirm" },
  .npre  = { 1, 1, 1, 4, 2, 1, 2 },
  .pre   = { {0}, {1}, {2}, {3, 4, 5, 11}, {6, 7}, {9}, {8, 10} },
  .npost = { 1, 1, 1, 4, 3, 1, 1 },
  .post  = { {3}, {4}, {5}, {6, 0, 1, 2}, {8, 9, 11}, {10}, {7} } },

/* A2  Batch mixer (sequential control). Two ingredients are filled in
 *     parallel, mixed for a fixed time, and drained. A marked graph.
 *     xA/xB/xT/xE with sensA/sensB/timer/sensE model the level sensors and
 *     the timer that end each step. */
{ .name = "batch-mixer", .klass = "marked graph, safe",
  .np = 16, .nt = 10,
  .pname = { "idle", "req", "fillA", "fillB", "fullA", "fullB", "mixing", "drain",
             "evA", "evB", "evT", "evE", "xA", "xB", "xT", "xE" },
  .m0    = {  1, 1, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0 },
  .tname = { "start", "endA", "endB", "join", "mixdone", "endD",
             "sensA", "sensB", "timer", "sensE" },
  .npre  = { 2, 2, 2, 2, 2, 2, 1, 1, 1, 1 },
  .pre   = { {0, 1}, {2, 8}, {3, 9}, {4, 5}, {6, 10}, {7, 11}, {12}, {13}, {14}, {15} },
  .npost = { 4, 1, 1, 2, 2, 2, 1, 1, 1, 1 },
  .post  = { {2, 3, 12, 13}, {4}, {5}, {6, 14}, {7, 15}, {0, 1}, {8}, {9}, {10}, {11} } },

/* A3  Electric-vehicle charging site. Arriving vehicles are assigned to the
 *     three phases in turn (a free choice); each phase has one charging slot
 *     (its budget); a vehicle waits until the slot of its phase is free.
 *     Free choice. Four vehicles circulate. */
{ .name = "ev-site", .klass = "free choice, 4-bounded",
  .np = 11, .nt = 10,
  .pname = { "away", "arrived", "w1", "w2", "w3", "b1", "b2", "b3", "c1", "c2", "c3" },
  .m0    = {  4,      0,         0,    0,    0,    1,    1,    1,    0,    0,    0   },
  .tname = { "arrive", "toL1", "toL2", "toL3", "con1", "con2", "con3", "fin1", "fin2", "fin3" },
  .npre  = { 1, 1, 1, 1, 2, 2, 2, 1, 1, 1 },
  .pre   = { {0}, {1}, {1}, {1}, {2, 5}, {3, 6}, {4, 7}, {8}, {9}, {10} },
  .npost = { 1, 1, 1, 1, 1, 1, 1, 2, 2, 2 },
  .post  = { {1}, {2}, {3}, {4}, {8}, {9}, {10}, {5, 0}, {6, 0}, {7, 0} } },
};

#define N_NETS ((int)(sizeof NETS / sizeof NETS[0]))

#endif /* NETS_H */
