/* Exhaustive interleaving search of the repository's grouped-offer module.
 * Drives the unmodified edg_choice_step() from petri/edg_pn_choice.c.
 * A state is the dynamic part of the net plus all mailbox contents plus an
 * independent reference marking, updated when the group posts GRANT.
 * Every distinct mailbox head may be delivered next (FIFO per mailbox). */
#include "edg_pn_choice.h"
#include "nets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIT EDG_CHOICE_BIT
#ifndef QMAX
#define QMAX 96          /* total messages in flight            */
#endif
#ifndef BOXCAP
#define BOXCAP 16        /* per-mailbox capacity                */
#endif

typedef struct {
    const char *name;
    int np, nt;
    edg_choice_arcs_t arcs[EDG_CHOICE_MAX];
    int m0[EDG_CHOICE_MAX];
} gnet_t;

typedef struct {
    edg_choice_net_t net;
    uint16_t q[QMAX]; int qn;
    int ref[EDG_CHOICE_MAX];
    int illegal, overflow;
    unsigned fired_mask;
} work_t;

static gnet_t G;
static edg_choice_net_t TEMPLATE;
static size_t MAX_STATES = 20000000;

static uint16_t pack(edg_choice_msg_t m) { return (uint16_t)(m.kind << 8 | m.id); }
static edg_choice_msg_t unpack(uint16_t v) { return (edg_choice_msg_t){(edg_choice_kind_t)(v >> 8), v & 0xff}; }
static int box_of(uint16_t v) { return edg_choice_mailbox(&TEMPLATE, unpack(v)); }

static void post(void *ctx, edg_choice_msg_t m)
{
    work_t *w = ctx;
    if (m.kind == EDG_CHOICE_GRANT) {
        edg_choice_arcs_t a = G.arcs[m.id];
        for (int p = 0; p < G.np; ++p)
            if ((a.inputs & BIT(p)) && w->ref[p] <= 0) w->illegal++;
        for (int p = 0; p < G.np; ++p) {
            if (a.inputs & BIT(p)) --w->ref[p];
            if (a.outputs & BIT(p)) ++w->ref[p];
        }
        w->fired_mask |= BIT(m.id);
    }
    if (w->qn == QMAX) { w->overflow = 1; return; }
    int b = edg_choice_mailbox(&TEMPLATE, m), inbox = 0;
    for (int i = 0; i < w->qn; ++i) inbox += box_of(w->q[i]) == b;
    if (inbox >= BOXCAP) { w->overflow = 1; return; }
    w->q[w->qn++] = pack(m);
}

/* canonical order: by mailbox, FIFO preserved inside each mailbox */
static void canon(work_t *w)
{
    for (int i = 1; i < w->qn; ++i) {
        uint16_t v = w->q[i]; int b = box_of(v), j = i;
        while (j > 0 && box_of(w->q[j - 1]) > b) { w->q[j] = w->q[j - 1]; --j; }
        w->q[j] = v;
    }
}

/* ---- serialisation of the dynamic state ---- */
static size_t ser(const work_t *w, uint8_t *o)
{
    size_t n = 0;
#define PUT(x) do { memcpy(o + n, &(x), sizeof(x)); n += sizeof(x); } while (0)
    for (int p = 0; p < G.np; ++p) {
        const edg_pn_place_state_t *s = &w->net.place[p];
        PUT(s->st.d); PUT(s->st.have_out); PUT(s->st.last_out); PUT(s->st.anomaly);
        PUT(s->offered); PUT(s->next);
#ifdef S1LIB
        PUT(s->released); PUT(s->ready);
#endif
        int16_t r = (int16_t)w->ref[p]; PUT(r);
    }
    for (int t = 0; t < G.nt; ++t) {
        const edg_choice_transition_t *s = &w->net.transition[t];
        PUT(s->gate.d); PUT(s->gate.have_out); PUT(s->gate.last_out); PUT(s->gate.anomaly);
        PUT(s->requested);
        const edg_choice_group_t *g = &w->net.group[t];
        PUT(g->open); PUT(g->pending); PUT(g->next);
    }
    uint8_t qn = (uint8_t)w->qn; PUT(qn);
    for (int i = 0; i < w->qn; ++i) PUT(w->q[i]);
#undef PUT
    return n;
}

static void deser(work_t *w, const uint8_t *o)
{
    size_t n = 0;
    memcpy(&w->net, &TEMPLATE, sizeof w->net);
#define GET(x) do { memcpy(&(x), o + n, sizeof(x)); n += sizeof(x); } while (0)
    for (int p = 0; p < G.np; ++p) {
        edg_pn_place_state_t *s = &w->net.place[p];
        GET(s->st.d); GET(s->st.have_out); GET(s->st.last_out); GET(s->st.anomaly);
        GET(s->offered); GET(s->next);
#ifdef S1LIB
        GET(s->released); GET(s->ready);
#endif
        int16_t r; GET(r); w->ref[p] = r;
    }
    for (int t = 0; t < G.nt; ++t) {
        edg_choice_transition_t *s = &w->net.transition[t];
        GET(s->gate.d); GET(s->gate.have_out); GET(s->gate.last_out); GET(s->gate.anomaly);
        GET(s->requested);
        edg_choice_group_t *g = &w->net.group[t];
        GET(g->open); GET(g->pending); GET(g->next);
    }
    uint8_t qn; GET(qn); w->qn = qn;
    for (int i = 0; i < w->qn; ++i) GET(w->q[i]);
#undef GET
    w->illegal = w->overflow = 0; w->fired_mask = 0;
}

/* ---- state store: arena + open-addressing hash ---- */
static uint8_t *arena; static size_t arena_len, arena_cap;
static size_t *offs; static size_t nstates, offs_cap;
static size_t *table; static size_t table_cap;

static uint64_t hash(const uint8_t *b, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

static void rehash(void);
static int intern(const uint8_t *b, size_t n)
{
    if ((nstates + 1) * 2 > table_cap) rehash();
    uint64_t h = hash(b, n);
    for (size_t i = h & (table_cap - 1);; i = (i + 1) & (table_cap - 1)) {
        if (table[i] == SIZE_MAX) {
            if (arena_len + n + 2 > arena_cap) {
                arena_cap = arena_cap ? arena_cap * 2 : (1 << 24);
                arena = realloc(arena, arena_cap);
                if (!arena) { perror("arena"); exit(2); }
            }
            if (nstates == offs_cap) {
                offs_cap = offs_cap ? offs_cap * 2 : (1 << 20);
                offs = realloc(offs, offs_cap * sizeof *offs);
            }
            uint16_t len = (uint16_t)n;
            memcpy(arena + arena_len, &len, 2);
            memcpy(arena + arena_len + 2, b, n);
            offs[nstates] = arena_len; arena_len += n + 2;
            table[i] = nstates++;
            return 1;
        }
        const uint8_t *s = arena + offs[table[i]];
        uint16_t len; memcpy(&len, s, 2);
        if (len == n && !memcmp(s + 2, b, n)) return 0;
    }
}

static void rehash(void)
{
    size_t cap = table_cap ? table_cap * 2 : (1 << 21);
    free(table); table = malloc(cap * sizeof *table); table_cap = cap;
    for (size_t i = 0; i < cap; ++i) table[i] = SIZE_MAX;
    for (size_t k = 0; k < nstates; ++k) {
        const uint8_t *s = arena + offs[k]; uint16_t len; memcpy(&len, s, 2);
        uint64_t h = hash(s + 2, len);
        size_t i = h & (cap - 1);
        while (table[i] != SIZE_MAX) i = (i + 1) & (cap - 1);
        table[i] = k;
    }
}

/* ---- reference reachability (for coverage) ---- */
#define MAXM 100000
static uint8_t refm[MAXM][EDG_CHOICE_MAX]; static int nrefm;
static uint8_t seenm[MAXM];

static int find_mark(const uint8_t *m)
{
    for (int i = 0; i < nrefm; ++i) if (!memcmp(refm[i], m, G.np)) return i;
    return -1;
}

static void reference_graph(void)
{
    nrefm = 0;
    for (int p = 0; p < G.np; ++p) refm[0][p] = (uint8_t)G.m0[p];
    nrefm = 1;
    for (int k = 0; k < nrefm; ++k)
        for (int t = 0; t < G.nt; ++t) {
            uint8_t m[EDG_CHOICE_MAX]; memcpy(m, refm[k], EDG_CHOICE_MAX);
            int ok = 1;
            for (int p = 0; p < G.np; ++p) if ((G.arcs[t].inputs & BIT(p)) && !m[p]) ok = 0;
            if (!ok) continue;
            for (int p = 0; p < G.np; ++p) {
                if (G.arcs[t].inputs & BIT(p)) m[p]--;
                if (G.arcs[t].outputs & BIT(p)) m[p]++;
            }
            if (find_mark(m) < 0 && nrefm < MAXM) memcpy(refm[nrefm++], m, EDG_CHOICE_MAX);
        }
}

static int enabled_any(const int *ref)
{
    for (int t = 0; t < G.nt; ++t) {
        int ok = 1;
        for (int p = 0; p < G.np; ++p) if ((G.arcs[t].inputs & BIT(p)) && ref[p] <= 0) ok = 0;
        if (ok) return 1;
    }
    return 0;
}

/* Invariants checked in every explored state.
 * lost:    a transition waiting for a grant (requested) has its request pending
 *          at the group, in flight to the group, or its GRANT in flight.
 * balance: reference marking = local count + deposits in flight - withdrawals in
 *          flight + net effect of every GRANT not yet processed. */
static void invariants(const work_t *s, long *lost, long *balance)
{
    for (int t = 0; t < G.nt; ++t) {
        if (!s->net.transition[t].requested) continue;
        int g = (int)TEMPLATE.group_of_transition[t], ok = (s->net.group[g].pending >> t) & 1;
        for (int i = 0; i < s->qn && !ok; ++i) {
            edg_choice_msg_t m = unpack(s->q[i]);
            ok = (m.kind == EDG_CHOICE_REQUEST || m.kind == EDG_CHOICE_GRANT) && (int)m.id == t;
        }
        if (!ok) (*lost)++;
    }
    for (int p = 0; p < G.np; ++p) {
        long v = s->net.place[p].st.d;
        for (int i = 0; i < s->qn; ++i) {
            edg_choice_msg_t m = unpack(s->q[i]);
            if (m.kind == EDG_CHOICE_DEPOSIT && (int)m.id == p) v++;
            if (m.kind == EDG_CHOICE_WITHDRAW && (int)m.id == p) v--;
            if (m.kind == EDG_CHOICE_GRANT) {
                if (G.arcs[m.id].outputs & BIT(p)) v++;
                if (G.arcs[m.id].inputs & BIT(p)) v--;
            }
        }
        if (v != s->ref[p]) (*balance)++;
    }
}

static int run(void)
{
    if (!edg_choice_init(&TEMPLATE, G.np, G.arcs, G.nt)) {
        printf("%-16s edg_choice_init rejected the net\n", G.name);
        return 1;
    }
    reference_graph();
    memset(seenm, 0, sizeof seenm);
    arena_len = 0; nstates = 0; free(table); table = NULL; table_cap = 0;

    work_t w; memset(&w, 0, sizeof w);
    memcpy(&w.net, &TEMPLATE, sizeof w.net);
    for (int p = 0; p < G.np; ++p) {
        w.ref[p] = G.m0[p];
        post(&w, (edg_choice_msg_t){EDG_CHOICE_INIT, (unsigned)p});
        for (int k = 0; k < G.m0[p]; ++k) post(&w, (edg_choice_msg_t){EDG_CHOICE_DEPOSIT, (unsigned)p});
    }
    canon(&w);
    uint8_t buf[4096];
    intern(buf, ser(&w, buf));

    long illegal = 0, errors = 0, overflow = 0, stalls = 0, dead = 0, badfinal = 0, lost = 0, balance = 0;
    unsigned fired = 0; int maxq = 0, capped = 0;
    for (size_t k = 0; k < nstates; ++k) {
        if (nstates > MAX_STATES) { capped = 1; break; }
        uint8_t cur[4096]; uint16_t len; memcpy(&len, arena + offs[k], 2);
        memcpy(cur, arena + offs[k] + 2, len);
        work_t s; deser(&s, cur);
        if (s.qn > maxq) maxq = s.qn;
        invariants(&s, &lost, &balance);
        uint8_t m[EDG_CHOICE_MAX] = {0};
        for (int p = 0; p < G.np; ++p) m[p] = (uint8_t)s.ref[p];
        int mi = find_mark(m); if (mi >= 0) seenm[mi] = 1;
        if (s.qn == 0) {
            if (enabled_any(s.ref)) stalls++; else dead++;
            for (int p = 0; p < G.np; ++p) if (s.net.place[p].st.d != s.ref[p]) badfinal++;
            continue;
        }
        int seen_box[3 * EDG_CHOICE_MAX] = {0};
        for (int i = 0; i < s.qn; ++i) {
            int b = box_of(s.q[i]);
            if (seen_box[b]) continue;
            seen_box[b] = 1;
            work_t n; deser(&n, cur);
            uint16_t v = n.q[i];
            memmove(n.q + i, n.q + i + 1, (size_t)(n.qn - i - 1) * sizeof n.q[0]); n.qn--;
            if (!edg_choice_step(&n.net, unpack(v), post, &n)) { errors++; continue; }
            illegal += n.illegal; fired |= n.fired_mask;
            if (n.overflow) { overflow++; continue; }
            canon(&n);
            intern(buf, ser(&n, buf));
        }
    }
    int cov = 0; for (int i = 0; i < nrefm; ++i) cov += seenm[i];
    char fm[40] = ""; int nf = 0;
    for (int t = 0; t < G.nt; ++t) if (fired & BIT(t)) nf++;
    snprintf(fm, sizeof fm, "%d/%d", nf, G.nt);
    printf("%-16s states=%-9zu illegal=%-3ld errors=%-3ld lost=%-3ld balance=%-3ld overflow=%-4ld stalls=%-3ld "
           "dead=%-3ld badfinal=%-3ld markings=%d/%d fired=%s maxq=%d%s\n",
           G.name, nstates, illegal, errors, lost, balance, overflow, stalls, dead, badfinal, cov, nrefm, fm, maxq,
           capped ? "  INCOMPLETE (state cap)" : overflow ? "  INCOMPLETE (overflow pruned)" : "  complete");
    fflush(stdout);
    return illegal || errors || stalls || badfinal || lost || balance;
}

static void from_nets_h(const net_t *n)
{
    memset(&G, 0, sizeof G);
    G.name = n->name; G.np = n->np; G.nt = n->nt;
    for (int p = 0; p < n->np; ++p) G.m0[p] = n->m0[p];
    for (int t = 0; t < n->nt; ++t) {
        for (int k = 0; k < n->npre[t]; ++k) G.arcs[t].inputs |= BIT(n->pre[t][k]);
        for (int k = 0; k < n->npost[t]; ++k) G.arcs[t].outputs |= BIT(n->post[t][k]);
    }
}

static void def(const char *name, int np, const int *m0, int nt, const edg_choice_arcs_t *a)
{
    memset(&G, 0, sizeof G);
    G.name = name; G.np = np; G.nt = nt;
    memcpy(G.m0, m0, (size_t)np * sizeof *m0);
    memcpy(G.arcs, a, (size_t)nt * sizeof *a);
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : NULL;
    if (getenv("GX_MAX")) MAX_STATES = (size_t)atoll(getenv("GX_MAX"));
    int rc = 0;
#define RUN() do { if (!only || !strcmp(only, G.name)) rc |= run(); } while (0)
    /* the six nets of samples/03-grouped-offers/main.c */
    { edg_choice_arcs_t a[] = {{BIT(0)|BIT(1), BIT(2)}, {BIT(0)|BIT(1), BIT(3)}};
      def("s-shared", 4, (int[]){1,1,0,0}, 2, a); RUN();
      def("s-shared-2tok", 4, (int[]){2,2,0,0}, 2, a); RUN(); }
    { edg_choice_arcs_t a[] = {{BIT(0)|BIT(1), BIT(3)}, {BIT(0)|BIT(2), BIT(4)}};
      def("s-partial", 5, (int[]){1,0,1,0,0}, 2, a); RUN(); }
    { edg_choice_arcs_t a[] = {{BIT(0)|BIT(1), BIT(0)|BIT(2)}, {BIT(2), 0}};
      def("s-chain", 3, (int[]){1,3,0}, 2, a); RUN(); }
    { edg_choice_arcs_t a[] = {{BIT(0), 0}, {BIT(0)|BIT(1), 0}, {BIT(1), 0}};
      def("s-bridge", 2, (int[]){1,1}, 3, a); RUN(); }
    { edg_choice_arcs_t a[] = {{BIT(0), BIT(1)}, {BIT(1), BIT(0)}};
      def("s-cycle", 2, (int[]){1,0}, 2, a); RUN(); }
    /* additional nets */
    { edg_choice_arcs_t a[] = {{BIT(0)|BIT(1), BIT(2)}, {BIT(0)|BIT(1), BIT(3)}, {BIT(0)|BIT(1), BIT(4)}};
      def("x-three-way", 5, (int[]){1,1,0,0,0}, 3, a); RUN(); }
    { edg_choice_arcs_t a[] = {{BIT(0)|BIT(2), BIT(4)}, {BIT(0)|BIT(1), BIT(5)}, {BIT(2)|BIT(3), BIT(6)}};
      def("x-cross", 7, (int[]){1,1,1,1,0,0,0}, 3, a); RUN(); }
    /* the validation nets of the nets.h on the include path */
    for (int i = 0; i < N_NETS; ++i) { from_nets_h(&NETS[i]); RUN(); }
    return rc;
}
