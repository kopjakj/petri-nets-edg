#include "edg_pn_choice.h"
#include <string.h>

static void send(edg_choice_post_t post, void *ctx, edg_choice_kind_t kind,
                 unsigned id)
{
    post(ctx, (edg_choice_msg_t){kind, id});
}

bool edg_choice_init(edg_choice_net_t *net, unsigned places,
                     const edg_choice_arcs_t arcs[], unsigned transitions)
{
    if (!net || !arcs || !places || places > EDG_CHOICE_MAX ||
        !transitions || transitions > EDG_CHOICE_MAX) return false;
    uint32_t valid = UINT32_MAX >> (EDG_CHOICE_MAX - places);
    for (unsigned t = 0; t < transitions; ++t)
        if (!arcs[t].inputs || ((arcs[t].inputs | arcs[t].outputs) & ~valid))
            return false;
    memset(net, 0, sizeof *net);
    net->places = places;
    net->transitions = transitions;
    memcpy(net->arcs, arcs, transitions * sizeof *arcs);

    /* Merge components whenever two presets intersect. At wiring time only. */
    for (unsigned t = 0; t < transitions; ++t) {
        net->group_of_transition[t] = t;
        for (unsigned u = 0; u < t; ++u) {
            if (!(arcs[t].inputs & arcs[u].inputs)) continue;
            unsigned old = net->group_of_transition[u];
            for (unsigned v = 0; v < t; ++v)
                if (net->group_of_transition[v] == old)
                    net->group_of_transition[v] = t;
        }
    }
    for (unsigned p = 0; p < places; ++p) {
        net->place[p] = EDG_PN_PLACE_INIT;
        for (unsigned t = 0; t < transitions; ++t) {
            if (!(arcs[t].inputs & EDG_CHOICE_BIT(p))) continue;
            net->consumers[p] |= EDG_CHOICE_BIT(t);
            net->group_of_place[p] = net->group_of_transition[t];
            ++net->transition[t].gate.d; /* every offer wire starts closed */
        }
    }
    for (unsigned t = 0; t < transitions; ++t)
        net->transition[t].gate.have_out = true; /* AND initially false */
    return true;
}

int edg_choice_mailbox(const edg_choice_net_t *net, edg_choice_msg_t msg)
{
    unsigned id = msg.id;
    switch (msg.kind) {
    case EDG_CHOICE_INIT: case EDG_CHOICE_DEPOSIT: case EDG_CHOICE_WITHDRAW:
        if (id < net->places) return (int)id;
        break;
    case EDG_CHOICE_OPEN: case EDG_CHOICE_CLOSE: case EDG_CHOICE_GRANT:
        if (id < net->transitions) return EDG_CHOICE_MAX + (int)id;
        break;
    case EDG_CHOICE_OFFER:
        if (id < net->places && net->consumers[id])
            return 2 * EDG_CHOICE_MAX + (int)net->group_of_place[id];
        break;
    case EDG_CHOICE_REQUEST:
        if (id < net->transitions)
            return 2 * EDG_CHOICE_MAX + (int)net->group_of_transition[id];
        break;
    }
    return -1;
}

static bool place_step(edg_choice_net_t *n, edg_choice_msg_t msg,
                       edg_choice_post_t post, void *ctx)
{
    edg_pn_place_state_t *p = &n->place[msg.id];
    if (msg.kind == EDG_CHOICE_DEPOSIT && p->st.d == INT32_MAX) return false;
    if (msg.kind == EDG_CHOICE_WITHDRAW && (p->offered < 0 || !p->st.d))
        return false;
    /* The first event must initialize the place; reject subsequent INITs. */
    bool initialized = p->st.have_out;
    if (msg.kind == EDG_CHOICE_INIT ? initialized : !initialized) return false;
    edg_event_t ev = {msg.kind == EDG_CHOICE_DEPOSIT, msg.kind == EDG_CHOICE_INIT};
    edg_pn_place_out_t out;
    edg_pn_place_step(p, n->consumers[msg.id] != 0, ev, &out);
    if (out.anomaly) return false;
    if (out.offer) send(post, ctx, EDG_CHOICE_OFFER, msg.id);
    return true;
}

static void broadcast(const edg_choice_net_t *n, unsigned p,
                      edg_choice_kind_t kind, edg_choice_post_t post, void *ctx)
{
    for (unsigned t = 0; t < n->transitions; ++t)
        if (n->consumers[p] & EDG_CHOICE_BIT(t)) send(post, ctx, kind, t);
}

static bool group_step(const edg_choice_net_t *n, edg_choice_group_t *g,
                       edg_choice_msg_t msg, edg_choice_post_t post, void *ctx)
{
    uint32_t bit = EDG_CHOICE_BIT(msg.id);
    if (msg.kind == EDG_CHOICE_OFFER) {
        if (g->open & bit) return false;
        g->open |= bit;
        broadcast(n, msg.id, EDG_CHOICE_OPEN, post, ctx);
    } else {
        if (g->pending & bit) return false;
        g->pending |= bit;
    }

    /* One pass suffices: taking offers can only disable other requests. */
    unsigned first = g->next;
    for (unsigned i = 0; i < n->transitions; ++i) {
        unsigned t = (first + i) % n->transitions;
        uint32_t inputs = n->arcs[t].inputs;
        if (!(g->pending & EDG_CHOICE_BIT(t)) || (g->open & inputs) != inputs)
            continue;
        g->open &= ~inputs;                 /* the firing decision */
        g->pending &= ~EDG_CHOICE_BIT(t);
        g->next = (t + 1) % n->transitions;
        for (unsigned p = 0; p < n->places; ++p)
            if (inputs & EDG_CHOICE_BIT(p)) broadcast(n, p, EDG_CHOICE_CLOSE, post, ctx);
        send(post, ctx, EDG_CHOICE_GRANT, t);
    }
    return true;
}

static bool transition_step(edg_choice_net_t *n, edg_choice_msg_t msg,
                            edg_choice_post_t post, void *ctx)
{
    unsigned t = msg.id;
    edg_choice_transition_t *s = &n->transition[t];
    if (msg.kind == EDG_CHOICE_GRANT) {
        if (!s->requested) return false;
        s->requested = false;
        for (unsigned p = 0; p < n->places; ++p)
            if (n->arcs[t].inputs & EDG_CHOICE_BIT(p))
                send(post, ctx, EDG_CHOICE_WITHDRAW, p);
        for (unsigned p = 0; p < n->places; ++p)
            if (n->arcs[t].outputs & EDG_CHOICE_BIT(p))
                send(post, ctx, EDG_CHOICE_DEPOSIT, p);
    } else {
        edg_event_t out, inverted = {msg.kind == EDG_CHOICE_CLOSE, false};
        edg_gate_step(EDG_LEVEL, 0, &s->gate, inverted, &out);
        if (s->gate.anomaly) return false;
    }
    if (s->gate.d == 0 && !s->requested) {
        s->requested = true;
        send(post, ctx, EDG_CHOICE_REQUEST, t);
    }
    return true;
}

bool edg_choice_step(edg_choice_net_t *net, edg_choice_msg_t msg,
                     edg_choice_post_t post, void *ctx)
{
    if (!net || !post) return false;
    int box = edg_choice_mailbox(net, msg);
    if (box < 0) return false;
    if (box < EDG_CHOICE_MAX) return place_step(net, msg, post, ctx);
    if (box < 2 * EDG_CHOICE_MAX) return transition_step(net, msg, post, ctx);
    return group_step(net, &net->group[box - 2 * EDG_CHOICE_MAX], msg, post, ctx);
}
