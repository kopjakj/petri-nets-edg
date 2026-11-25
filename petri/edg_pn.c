/*==============================================================================
 *  edg_pn.c  --  Petri nets on Event-Driven Gates (EDG-PN), version 2
 *
 *  See edg_pn.h. Every change of a distance descriptor in this file goes
 *  through the unmodified edg_gate_step() of the gate library.
 *============================================================================*/
#include "edg_pn.h"

#include <stdio.h>

static const edg_event_t EV_DEPOSIT  = { .value = true,  .is_init = false };
static const edg_event_t EV_WITHDRAW = { .value = false, .is_init = false };
static const edg_event_t EV_OFFER    = { .value = true,  .is_init = false };
static const edg_event_t EV_OFFER_R  = { .value = true,  .is_init = true  };
static const edg_event_t EV_RELEASE  = { .value = false, .is_init = true  };
static const edg_event_t EV_REQUEST  = { .value = true,  .is_init = true  };
static const edg_event_t EV_INIT     = { .value = false, .is_init = true  };

/*===========================================================================
 *  Pure cores
 *=========================================================================*/

void edg_pn_place_step(edg_pn_place_state_t *s, uint8_t n_consumers,
                       edg_event_t in, edg_pn_place_out_t *out)
{
    out->level_changed = false;
    out->offer         = false;
    out->anomaly       = false;

    if (in.is_init && in.value) {
        /* REQUEST: a consumer is ready again; offer another round. */
        out->kind    = EDG_PN_IN_REQUEST;
        s->released  = 0;
    } else if (in.is_init && s->ready) {
        /* RELEASE: the holder gives the offered token back unused. */
        out->kind = EDG_PN_IN_RELEASE;
        if (s->offered < 0) {
            out->anomaly = true;
        } else {
            s->released |= (uint8_t)(1u << s->offered);
            s->offered = -1;
        }
    } else {
        /* Initialization, deposit or withdrawal: the token count is the
         * distance descriptor of a level gate with no input inversion:
         * true -> d++, false change -> d--, raw output = (d == 0). */
        const bool withdrawal = (!in.value && !in.is_init);
        out->kind = in.is_init ? EDG_PN_IN_INIT
                  : (in.value ? EDG_PN_IN_DEPOSIT : EDG_PN_IN_WITHDRAW);
        if (in.is_init) {
            s->ready = true;
        }
        edg_event_t raw;
        out->level_changed = edg_gate_step(EDG_LEVEL, 0u, &s->st, in, &raw);
        if (out->level_changed) {
            out->level.value   = !raw.value;  /* OR configuration: "marked"  */
            out->level.is_init = raw.is_init;
        }
        /* A withdrawal is legal only as the answer to an outstanding offer
         * and only if a token was there (edg_gate_step clamps and flags d<0). */
        out->anomaly = s->st.anomaly || (withdrawal && s->offered < 0);
        if (withdrawal) {
            s->offered  = -1;
            s->released = 0;
        }
    }

    /* Offer discipline: at most one token on offer, to one consumer, in
     * round-robin order, skipping consumers that have released it. */
    if (s->offered < 0 && s->st.d > 0 && n_consumers > 0) {
        for (uint8_t k = 0; k < n_consumers; k++) {
            uint8_t c = (uint8_t)((s->next + k) % n_consumers);
            if (!((s->released >> c) & 1u)) {
                out->offer    = true;
                out->offer_to = c;
                s->offered    = (int8_t)c;
                s->next       = (uint8_t)((c + 1u) % n_consumers);
                break;
            }
        }
    }
}

static void trans_reset(edg_pn_trans_state_t *s, uint8_t n_kept)
{
    /* The kept offers are consumed in the firing step itself, so the n_kept
     * status falls are applied here instead of being awaited from the places
     * (which would race with their next offers). */
    const edg_event_t fall = { .value = true, .is_init = false };
    edg_event_t y;
    for (uint8_t i = 0; i < n_kept; i++) {
        (void)edg_gate_step(EDG_LEVEL, 0u, &s->st, fall, &y);
    }
}

edg_pn_action_t edg_pn_trans_step(edg_pn_trans_state_t *s, uint8_t n_kept,
                                  bool has_release, edg_event_t offer,
                                  bool *anomaly)
{
    if (anomaly) {
        *anomaly = false;
    }
    if (!offer.value) {                       /* transitions receive offers only */
        if (anomaly) {
            *anomaly = true;
        }
        return EDG_PN_WAIT;
    }

    if (offer.is_init) {
        /* Offer from the release input: use it now or give it back now. */
        if (!has_release) {
            if (anomaly) {
                *anomaly = true;
            }
            return EDG_PN_WAIT;
        }
        if (s->st.d == 0) {
            trans_reset(s, n_kept);
            return EDG_PN_FIRE;
        }
        return EDG_PN_RELEASE;
    }

    /* Kept offer. AND configuration: the input inverter turns it into a false
     * change, which decrements the count of missing offers. */
    edg_event_t in = offer;
    in.value = !in.value;
    edg_event_t y;
    bool emit = edg_gate_step(EDG_LEVEL, 0u, &s->st, in, &y);
    if (anomaly) {
        *anomaly = s->st.anomaly;
    }
    if (!(emit && y.value)) {
        return EDG_PN_WAIT;                   /* kept offers still missing   */
    }
    if (has_release) {
        return EDG_PN_REQUEST;                /* ready except the release input */
    }
    trans_reset(s, n_kept);
    return EDG_PN_FIRE;
}

/*===========================================================================
 *  FreeRTOS objects
 *=========================================================================*/

static void post(QueueHandle_t q, edg_event_t ev)
{
    /* Block rather than drop: a lost token message would corrupt a marking. */
    xQueueSendToBack(q, &ev, portMAX_DELAY);
}

void edg_pn_mark(QueueHandle_t place_in, unsigned tokens)
{
    post(place_in, EV_INIT);                  /* (false, init): initialization */
    for (unsigned i = 0; i < tokens; i++) {
        post(place_in, EV_DEPOSIT);           /* (true, chg): one token        */
    }
}

static const char *const IN_NAME[] = { "+1", "-1", "init", "release", "request" };

void edg_pn_place_task(void *param)
{
    edg_pn_place_t *p = (edg_pn_place_t *)param;
    edg_pn_place_state_t s = EDG_PN_PLACE_INIT;

    for (;;) {
        edg_event_t ev;
        xQueueReceive(p->in, &ev, portMAX_DELAY);

        edg_pn_place_out_t o;
        edg_pn_place_step(&s, p->n_consumers, ev, &o);

        if (o.anomaly) {
            printf("[%-7s] !! protocol anomaly: %s without token/offer\n",
                   p->name, IN_NAME[o.kind]);
        }
        if (p->trace) {
            printf("[%-7s] %s  M=%d\n", p->name, IN_NAME[o.kind], (int)s.st.d);
        }
        if (o.level_changed) {
            for (uint8_t i = 0; i < p->n_monitors; i++) {
                post(p->monitor[i], o.level);
            }
        }
        if (o.offer) {
            bool r = (p->release_mask >> o.offer_to) & 1u;
            post(p->consumer[o.offer_to], r ? EV_OFFER_R : EV_OFFER);
        }
    }
}

void edg_pn_transition_task(void *param)
{
    edg_pn_transition_t *t = (edg_pn_transition_t *)param;
    const bool    has_release = (t->release_input != 0);
    const uint8_t n_kept = (uint8_t)(t->n_pre - (has_release ? 1u : 0u));
    edg_pn_trans_state_t s = edg_pn_trans_init(n_kept);

    for (;;) {
        edg_event_t ev;
        xQueueReceive(t->in, &ev, portMAX_DELAY);

        bool anomaly = false;
        edg_pn_action_t a = edg_pn_trans_step(&s, n_kept, has_release, ev, &anomaly);
        if (anomaly) {
            printf("[%-7s] !! protocol anomaly: unexpected offer\n", t->name);
        }
        if (a == EDG_PN_WAIT) {
            continue;
        }
        if (a == EDG_PN_RELEASE || a == EDG_PN_REQUEST) {
            if (t->trace) {
                printf("[%-7s] %s\n", t->name, a == EDG_PN_RELEASE ? "release" : "request");
            }
            post(t->pre[t->release_input - 1u], a == EDG_PN_RELEASE ? EV_RELEASE : EV_REQUEST);
            continue;
        }

        /* --- fire --- */
        if (t->on_fire) {
            t->on_fire(t);
        }
        if (t->trace) {
            printf("[%-7s] fire\n", t->name);
        }
        for (uint8_t i = 0; i < t->n_pre; i++) {
            post(t->pre[i], EV_WITHDRAW);
        }
        if (t->duration_ms) {
            vTaskDelay(pdMS_TO_TICKS(t->duration_ms));
        }
        for (uint8_t i = 0; i < t->n_post; i++) {
            post(t->post[i], EV_DEPOSIT);
        }
    }
}

void edg_pn_source_task(void *param)
{
    edg_pn_source_t *src = (edg_pn_source_t *)param;

    for (;;) {
        edg_event_t ev;
        xQueueReceive(src->in, &ev, portMAX_DELAY);

        /* Only a rising CHANGE makes a token: an initialization message
         * announces a resting level, not an event. */
        if (!ev.value || ev.is_init) {
            continue;
        }
        if (src->trace) {
            printf("[%-7s] token\n", src->name);
        }
        for (uint8_t i = 0; i < src->n_post; i++) {
            post(src->post[i], EV_DEPOSIT);
        }
    }
}

/*===========================================================================
 *  Configuration
 *=========================================================================*/

size_t edg_pn_auto_release(edg_pn_place_t *places, size_t n_places,
                           edg_pn_transition_t *transitions, size_t n_transitions)
{
    size_t outside = 0;
    for (size_t j = 0; j < n_places; j++) {
        places[j].release_mask = 0;
    }
    for (size_t i = 0; i < n_transitions; i++) {
        edg_pn_transition_t *t = &transitions[i];
        int shared = 0, k_found = -1;
        size_t j_found = 0;
        t->release_input = 0;
        for (uint8_t k = 0; k < t->n_pre; k++) {
            for (size_t j = 0; j < n_places; j++) {
                if (places[j].in == t->pre[k] && places[j].n_consumers >= 2) {
                    shared++;
                    k_found = k;
                    j_found = j;
                }
            }
        }
        if (shared >= 2) {
            outside++;
            continue;
        }
        if (shared == 1) {
            edg_pn_place_t *p = &places[j_found];
            for (uint8_t c = 0; c < p->n_consumers; c++) {
                if (p->consumer[c] == t->in) {
                    p->release_mask |= (uint8_t)(1u << c);
                    t->release_input = (uint8_t)(k_found + 1);
                }
            }
        }
    }
    return outside;
}
