/* Additional scenarios for the emulator study, in the format of nets.h. */
#ifndef NETS_EXTRA_H
#define NETS_EXTRA_H

#include "nets.h"

static const net_t EXTRA[] = {

/* Five philosophers taking both forks at once: every take has two shared inputs. */
{ .name = "philosophers-5", .klass = "two shared inputs per transition, safe",
  .np = 15, .nt = 10,
  .pname = { "th0", "th1", "th2", "th3", "th4", "ea0", "ea1", "ea2", "ea3", "ea4",
             "f0", "f1", "f2", "f3", "f4" },
  .m0    = { 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 },
  .tname = { "take0", "take1", "take2", "take3", "take4", "put0", "put1", "put2", "put3", "put4" },
  .npre  = { 3, 3, 3, 3, 3, 1, 1, 1, 1, 1 },
  .pre   = { {0, 10, 11}, {1, 11, 12}, {2, 12, 13}, {3, 13, 14}, {4, 14, 10},
             {5}, {6}, {7}, {8}, {9} },
  .npost = { 1, 1, 1, 1, 1, 3, 3, 3, 3, 3 },
  .post  = { {5}, {6}, {7}, {8}, {9},
             {0, 10, 11}, {1, 11, 12}, {2, 12, 13}, {3, 13, 14}, {4, 14, 10} } },

/* Three philosophers taking the left fork, then the right fork: the net itself
 * has a dead marking (every philosopher holds its left fork). */
{ .name = "philosophers-3-split", .klass = "one shared input per transition, has a deadlock",
  .np = 12, .nt = 9,
  .pname = { "th0", "th1", "th2", "hl0", "hl1", "hl2", "ea0", "ea1", "ea2", "f0", "f1", "f2" },
  .m0    = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1 },
  .tname = { "left0", "left1", "left2", "right0", "right1", "right2", "put0", "put1", "put2" },
  .npre  = { 2, 2, 2, 2, 2, 2, 1, 1, 1 },
  .pre   = { {0, 9}, {1, 10}, {2, 11}, {3, 10}, {4, 11}, {5, 9}, {6}, {7}, {8} },
  .npost = { 1, 1, 1, 1, 1, 1, 3, 3, 3 },
  .post  = { {3}, {4}, {5}, {6}, {7}, {8}, {0, 9, 10}, {1, 10, 11}, {2, 11, 9} } },

/* Three clients, one resource: fairness and starvation. */
{ .name = "mutex-3", .klass = "one shared input per transition, safe",
  .np = 10, .nt = 9,
  .pname = { "idle1", "want1", "crit1", "idle2", "want2", "crit2", "idle3", "want3", "crit3", "r" },
  .m0    = { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 },
  .tname = { "req1", "enter1", "leave1", "req2", "enter2", "leave2", "req3", "enter3", "leave3" },
  .npre  = { 1, 2, 1, 1, 2, 1, 1, 2, 1 },
  .pre   = { {0}, {1, 9}, {2}, {3}, {4, 9}, {5}, {6}, {7, 9}, {8} },
  .npost = { 1, 1, 2, 1, 1, 2, 1, 1, 2 },
  .post  = { {1}, {2}, {0, 9}, {4}, {5}, {3, 9}, {7}, {8}, {6, 9} } },

/* Greedy competitor: A needs only r and returns it at once; B needs r and q,
 * where q is refilled by a separate loop. B can starve if r always goes to A. */
{ .name = "greedy-pair", .klass = "one shared input per transition, safe",
  .np = 5, .nt = 5,
  .pname = { "r", "ha", "q", "hb", "qi" },
  .m0    = { 1, 0, 0, 0, 1 },
  .tname = { "A", "Aret", "B", "Bret", "fill" },
  .npre  = { 1, 1, 2, 1, 1 },
  .pre   = { {0}, {1}, {0, 2}, {3}, {4} },
  .npost = { 1, 1, 1, 2, 1 },
  .post  = { {1}, {0}, {3}, {0, 4}, {2} } },
};

#define N_EXTRA ((int)(sizeof EXTRA / sizeof EXTRA[0]))

#endif
