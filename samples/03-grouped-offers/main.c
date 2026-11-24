/* Competing AND gates, delivered asynchronously. Build with "make run". */
#include "edg_pn_choice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIT EDG_CHOICE_BIT
typedef struct {
    edg_choice_net_t net;
    edg_choice_msg_t queue[256];
    unsigned count, fired[EDG_CHOICE_MAX], grants;
    int reference[EDG_CHOICE_MAX];
    uint32_t seed;
    bool trace;
} example_t;

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "choice example: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void enqueue(void *ctx, edg_choice_msg_t msg)
{
    example_t *e = ctx;
    if (msg.kind == EDG_CHOICE_GRANT) {
        edg_choice_arcs_t arcs = e->net.arcs[msg.id];
        for (unsigned p = 0; p < e->net.places; ++p)
            if (arcs.inputs & BIT(p)) check(e->reference[p] > 0, "illegal grant");
        for (unsigned p = 0; p < e->net.places; ++p) {
            if (arcs.inputs & BIT(p)) --e->reference[p];
            if (arcs.outputs & BIT(p)) ++e->reference[p];
        }
        ++e->fired[msg.id];
        ++e->grants;
        if (e->trace) printf("  grant %c\n", 'A' + (int)msg.id);
    }
    check(e->count < sizeof e->queue / sizeof e->queue[0], "mailbox storage exhausted");
    e->queue[e->count++] = msg;
}

static void start(example_t *e, const edg_choice_arcs_t *arcs, unsigned nt,
                  const int *initial, unsigned np, uint32_t seed, bool trace)
{
    *e = (example_t){.seed = seed, .trace = trace};
    check(edg_choice_init(&e->net, np, arcs, nt), "invalid wiring");
    for (unsigned p = 0; p < np; ++p) {
        e->reference[p] = initial[p];
        enqueue(e, (edg_choice_msg_t){EDG_CHOICE_INIT, p});
        for (int k = 0; k < initial[p]; ++k)
            enqueue(e, (edg_choice_msg_t){EDG_CHOICE_DEPOSIT, p});
    }
}

/* Pick among mailbox heads, preserving FIFO at each destination. */
static void deliver(example_t *e)
{
    bool seen[3 * EDG_CHOICE_MAX] = {false};
    unsigned heads[3 * EDG_CHOICE_MAX], count = 0;
    for (unsigned i = 0; i < e->count; ++i) {
        int box = edg_choice_mailbox(&e->net, e->queue[i]);
        check(box >= 0, "unknown mailbox");
        if (!seen[box]) {
            seen[box] = true;
            heads[count++] = i;
        }
    }
    check(count > 0, "unexpected quiescence");
    e->seed = e->seed * UINT32_C(1664525) + UINT32_C(1013904223);
    unsigned index = heads[(e->seed >> 8) % count];
    edg_choice_msg_t m = e->queue[index];
    --e->count;
    memmove(e->queue + index, e->queue + index + 1,
            (e->count - index) * sizeof m);
    check(edg_choice_step(&e->net, m, enqueue, e), "protocol error");
}

static void finish(example_t *e)
{
    for (unsigned steps = 0; e->count; ++steps) {
        check(steps < 1000, "example did not settle");
        deliver(e);
    }
    for (unsigned p = 0; p < e->net.places; ++p)
        check(e->net.place[p].st.d == e->reference[p], "incorrect final marking");
    for (unsigned t = 0; t < e->net.transitions; ++t) {
        bool enabled = true;
        for (unsigned p = 0; p < e->net.places; ++p)
            if ((e->net.arcs[t].inputs & BIT(p)) && !e->reference[p]) enabled = false;
        check(!enabled, "enabled transition stalled");
    }
}

int main(int argc, char **argv)
{
    bool test = argc == 2 && strcmp(argv[1], "--check") == 0;
    if (argc != 1 && !test) {
        fprintf(stderr, "usage: %s [--check]\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* P and Q feed both transitions. A produces DoneA and B produces DoneB. */
    const edg_choice_arcs_t shared[] = {{BIT(0)|BIT(1), BIT(2)}, {BIT(0)|BIT(1), BIT(3)}};
    /* A needs P,Q and B needs P,R. Q is initially empty. */
    const edg_choice_arcs_t partial[] = {{BIT(0)|BIT(1), BIT(3)}, {BIT(0)|BIT(2), BIT(4)}};
    example_t e;
    unsigned winners = 0;
    for (unsigned seed = 0; seed < (test ? 256u : 1u); ++seed) {
        if (!test) puts("P=1 Q=1; A and B each need P,Q");
        start(&e, shared, 2, (int[]){1,1,0,0}, 4, seed, !test);
        finish(&e);
        check(e.grants == 1, "one pair must grant exactly once");
        winners |= e.fired[0] ? 1u : 2u;
        if (!test) printf("  P=0 Q=0 DoneA=%d DoneB=%d\n", e.reference[2], e.reference[3]);

        if (!test) puts("P=1 Q=0 R=1; A needs P,Q and B needs P,R");
        start(&e, partial, 2, (int[]){1,0,1,0,0}, 5, seed, !test);
        finish(&e);
        check(e.fired[0] == 0 && e.fired[1] == 1, "expected B");
        if (!test) printf("  DoneA=%d DoneB=%d\n", e.reference[3], e.reference[4]);

        if (!test) continue;
        start(&e, shared, 2, (int[]){2,2,0,0}, 4, seed, false);
        finish(&e);
        check(e.grants == 2, "remaining tokens must be offered again");

        /* Two groups connected by production. A has a self-loop on P.
           Each grant of A forwards a Q token to R, which enables B. */
        const edg_choice_arcs_t chain[] = {{BIT(0)|BIT(1), BIT(0)|BIT(2)}, {BIT(2),0}};
        start(&e, chain, 2, (int[]){1,3,0}, 3, seed, false);
        check(e.net.group_of_transition[0] != e.net.group_of_transition[1],
              "sharing only an output-to-input arc must not merge groups");
        finish(&e);
        check(e.fired[0] == 3 && e.fired[1] == 3, "cross-group delivery failed");

        /* A-B-C form one transitive conflict group. A and C can both win. */
        const edg_choice_arcs_t bridge[] = {{BIT(0),0}, {BIT(0)|BIT(1),0}, {BIT(1),0}};
        start(&e, bridge, 3, (int[]){1,1}, 2, seed, false);
        check(e.net.group_of_transition[0] == e.net.group_of_transition[2],
              "transitive conflict was split");
        finish(&e);

        const edg_choice_arcs_t cycle[] = {{BIT(0),BIT(1)}, {BIT(1),BIT(0)}};
        start(&e, cycle, 2, (int[]){1,0}, 2, seed, false);
        for (unsigned steps = 0; e.grants < 50; ++steps) {
            check(steps < 1000, "cycle stalled");
            deliver(&e);
        }
        check(e.fired[0] == 25 && e.fired[1] == 25, "incorrect cycle trace");
    }
    if (test) {
        check(winners == 3, "both competing transitions must be reachable");
        puts("Passed 256 mailbox schedules for each of 6 nets; both P/Q winners observed.");
    }
    return EXIT_SUCCESS;
}
