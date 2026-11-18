/*==============================================================================
 *  edg_pn.c  --  Petri nets on Event-Driven Gates (EDG-PN)
 *
 *  See edg_pn.h. Every change of a distance descriptor in this file goes
 *  through the unmodified edg_gate_step() of the gate library.
 *============================================================================*/
#include "edg_pn.h"

#include <stdio.h>

static const edg_event_t EV_DEPOSIT  = { .value = true,  .is_init = false };
static const edg_event_t EV_WITHDRAW = { .value = false, .is_init = false };
static const edg_event_t EV_OFFER    = { .value = true,  .is_init = false };

/*===========================================================================
 *  Pure cores
 *=========================================================================*/

void edg_pn_place_step(edg_pn_place_state_t *s, uint8_t n_consumers,
                       edg_event_t in, edg_pn_place_out_t *out)
{
    const bool withdrawal = (!in.value && !in.is_init);

    /* The marking is the distance descriptor of a level gate with no input
     * inversion: true -> d++, false change -> d--, raw output = (d == 0). */
    edg_event_t raw;
    out->level_changed = edg_gate_step(EDG_LEVEL, 0u, &s->st, in, &raw);
    if (out->level_changed) {
        out->level.value   = !raw.value;      /* OR configuration: "marked"  */
        out->level.is_init = raw.is_init;
    }

    /* A withdrawal is legal only as the answer to an outstanding offer and
     * only if a token was there (edg_gate_step clamps and flags d < 0). */
    out->anomaly = s->st.anomaly || (withdrawal && s->offered < 0);
    if (withdrawal) {
        s->offered = -1;
    }

    /* Offer discipline: at most one token on offer, to one consumer. */
    out->offer = false;
    if (s->offered < 0 && s->st.d > 0 && n_consumers > 0) {
        out->offer    = true;
        out->offer_to = s->next;
        s->offered    = (int8_t)s->next;
        s->next       = (uint8_t)((s->next + 1u) % n_consumers);
    }
}

bool edg_pn_trans_step(edg_pn_trans_state_t *s, uint8_t n_inputs,
                       edg_event_t offer, bool *anomaly)
{
    /* AND configuration: the input inverter turns an offer (true) into a
     * false change, which decrements the count of missing input places. */
    edg_event_t in = offer;
    in.value = !in.value;

    edg_event_t y;
    bool emit = edg_gate_step(EDG_LEVEL, 0u, &s->st, in, &y);
    if (anomaly) {
        *anomaly = s->st.anomaly;
    }
    if (!(emit && y.value)) {
        return false;                         /* not (yet) enabled           */
    }

    /* Enabled: fire. The n_inputs offers are consumed in the same step, so
     * the n_inputs status falls are applied here instead of being awaited
     * from the places (which would race with their next offers). */
    const edg_event_t fall = { .value = true, .is_init = false };
    for (uint8_t i = 0; i < n_inputs; i++) {
        (void)edg_gate_step(EDG_LEVEL, 0u, &s->st, fall, &y);
    }
    return true;
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
    edg_event_t ev = { .value = (tokens > 0), .is_init = true };
    if (tokens == 0) {
        post(place_in, ev);                   /* (false, init): report empty */
    }
    for (unsigned i = 0; i < tokens; i++) {
        post(place_in, ev);                   /* (true, init): one token     */
    }
}

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
            printf("[%-7s] !! protocol anomaly: withdrawal without token/offer\n",
                   p->name);
        }
        if (p->trace) {
            printf("[%-7s] %s  M=%d\n", p->name,
                   ev.value ? "+1" : (ev.is_init ? "init" : "-1"), (int)s.st.d);
        }
        if (o.level_changed) {
            for (uint8_t i = 0; i < p->n_monitors; i++) {
                post(p->monitor[i], o.level);
            }
        }
        if (o.offer) {
            post(p->consumer[o.offer_to], EV_OFFER);
        }
    }
}

void edg_pn_transition_task(void *param)
{
    edg_pn_transition_t *t = (edg_pn_transition_t *)param;
    edg_pn_trans_state_t s = edg_pn_trans_init(t->n_pre);

    for (;;) {
        edg_event_t ev;
        xQueueReceive(t->in, &ev, portMAX_DELAY);

        bool anomaly = false;
        if (!edg_pn_trans_step(&s, t->n_pre, ev, &anomaly)) {
            if (anomaly) {
                printf("[%-7s] !! protocol anomaly: offer beyond fan-in\n",
                       t->name);
            }
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
