/*==============================================================================
 *  edg_pn.h  --  Petri nets on Event-Driven Gates (EDG-PN)
 *
 *  A token player for ordinary place/transition nets built from the EDG block
 *  library (../samples/common/edg.h). Nothing in the gate library is modified:
 *  the message type (edg_event_t), the wire (a FreeRTOS queue) and the pure
 *  gate function (edg_gate_step) are used exactly as they are.
 *
 *  The correspondence is one-to-one:
 *
 *      PLACE       = a level gate. Its distance descriptor d IS the marking.
 *                      (true , *  )  -> d++   one token deposited
 *                      (false, chg)  -> d--   one token withdrawn
 *                      (false, init) -> -     "no initial token"
 *                    Its ordinary gate output is the level "place is marked"
 *                    (an OR gate), usable by any actuator or gate.
 *
 *      TRANSITION  = an AND gate. Its distance descriptor counts the input
 *                    places that have NOT yet offered a token -- the distance
 *                    from enabledness. It fires on reaching d == 0.
 *
 *      ARC         = a wire. An arc place->transition carries offers (true);
 *                    an arc transition->place carries deposits (true); the
 *                    reverse direction of an input arc carries withdrawals
 *                    (false, chg).
 *
 *  What the layer adds to the plain gate is the OFFER discipline: a place does
 *  not broadcast its level to its output transitions, it offers ONE token to
 *  ONE of them at a time and waits for that token to be withdrawn before it
 *  offers the next. This is what makes firing safe under arbitrary task
 *  interleaving (see the paper, and test/pn_explore.c which checks it
 *  exhaustively).
 *
 *  Guarantees (ordinary nets, arc weight 1, no source transitions):
 *    * every firing is legal in the net, for every net and every schedule;
 *    * for conflict-free and free-choice nets the player stops only in a dead
 *      marking. For other nets a token can stay parked at a transition that
 *      is not otherwise enabled -- still safe, but possibly less live.
 *============================================================================*/
#ifndef EDG_PN_H
#define EDG_PN_H

#include "edg.h"

/* Maximum number of arcs on one side of a place or transition. */
#ifndef EDG_PN_MAX_ARCS
#define EDG_PN_MAX_ARCS 8
#endif

/*===========================================================================
 *  Pure cores (no FreeRTOS, no I/O) -- host-testable, like edg_gate_step().
 *=========================================================================*/

/*---------------------------------------------------------------------------
 *  Place
 *-------------------------------------------------------------------------*/
typedef struct {
    edg_state_t st;       /* the gate state; st.d is the marking M(p)        */
    int8_t      offered;  /* consumer index holding the current offer, -1 no */
    uint8_t     next;     /* rotating pointer: who is offered the next token */
} edg_pn_place_state_t;

#define EDG_PN_PLACE_INIT \
    ((edg_pn_place_state_t){ .st = EDG_STATE_INIT, .offered = -1, .next = 0 })

typedef struct {
    bool        level_changed; /* the "marked" level changed (or first report) */
    edg_event_t level;         /* the ordinary gate output: value = marked     */
    bool        offer;         /* an offer has to be sent ...                  */
    uint8_t     offer_to;      /* ... to this consumer index                   */
    bool        anomaly;       /* withdrawal without a token or without offer  */
} edg_pn_place_out_t;

/* Feed one event into a place with `n_consumers` output transitions. */
void edg_pn_place_step(edg_pn_place_state_t *s, uint8_t n_consumers,
                       edg_event_t in, edg_pn_place_out_t *out);

/*---------------------------------------------------------------------------
 *  Transition
 *-------------------------------------------------------------------------*/
typedef struct {
    edg_state_t st;       /* st.d = number of input places not yet offering  */
} edg_pn_trans_state_t;

/* A transition starts from the structurally known state "no input place has
 * offered anything": d = fan-in, output (enabled) = false, already reported. */
static inline edg_pn_trans_state_t edg_pn_trans_init(uint8_t n_inputs)
{
    edg_pn_trans_state_t s = { .st = { .d = n_inputs, .have_out = true,
                                       .last_out = false, .anomaly = false } };
    return s;
}

/* Feed one offer into a transition with `n_inputs` input places. Returns true
 * when the transition fires; the n_inputs offers are then consumed and the
 * descriptor is back at n_inputs. *anomaly reports an offer beyond fan-in. */
bool edg_pn_trans_step(edg_pn_trans_state_t *s, uint8_t n_inputs,
                       edg_event_t offer, bool *anomaly);

/*===========================================================================
 *  FreeRTOS objects (one task per object, one queue per object)
 *=========================================================================*/

typedef struct {
    const char   *name;
    QueueHandle_t in;                          /* deposits and withdrawals     */
    QueueHandle_t consumer[EDG_PN_MAX_ARCS];   /* queues of output transitions */
    uint8_t       n_consumers;
    QueueHandle_t monitor[EDG_MAX_FANOUT];     /* ordinary fan-out: "marked"   */
    uint8_t       n_monitors;
    bool          trace;
} edg_pn_place_t;

typedef struct edg_pn_transition {
    const char   *name;
    QueueHandle_t in;                          /* offers from input places     */
    QueueHandle_t pre[EDG_PN_MAX_ARCS];        /* queues of the input places   */
    uint8_t       n_pre;
    QueueHandle_t post[EDG_PN_MAX_ARCS];       /* queues of the output places  */
    uint8_t       n_post;
    TickType_t    duration_ms;                 /* firing duration, 0 = none    */
    void        (*on_fire)(const struct edg_pn_transition *t); /* optional     */
    void         *ctx;                         /* free for the application     */
    bool          trace;
} edg_pn_transition_t;

/* A token source: turns every rising change of an ordinary EDG signal (an
 * input object, a gate) into one token in each output place. It is the bridge
 * from a gate network, or from the physical world, into the net. */
typedef struct {
    const char   *name;
    QueueHandle_t in;                          /* an ordinary EDG wire         */
    QueueHandle_t post[EDG_PN_MAX_ARCS];
    uint8_t       n_post;
    bool          trace;
} edg_pn_source_t;

void edg_pn_place_task(void *param);       /* param: edg_pn_place_t*      */
void edg_pn_transition_task(void *param);  /* param: edg_pn_transition_t* */
void edg_pn_source_task(void *param);      /* param: edg_pn_source_t*     */

/* Initial marking. Posts `tokens` initialization messages carrying true to the
 * place, or a single (false, init) if tokens == 0, so that every place sends
 * its initial "marked" level exactly as an ordinary gate would. Call once per
 * place, before or after the scheduler starts. */
void edg_pn_mark(QueueHandle_t place_in, unsigned tokens);

#endif /* EDG_PN_H */
