/*==============================================================================
 *  edg_pn.h  --  Petri nets on Event-Driven Gates (EDG-PN), version 2
 *
 *  A token player for ordinary place/transition nets built from the EDG block
 *  library (edg.h). Nothing in the gate library is modified: the message type
 *  (edg_event_t, two booleans, no sender identity), the wire (a FreeRTOS queue)
 *  and the pure gate function (edg_gate_step) are used exactly as they are.
 *
 *      PLACE       = a level gate. Its descriptor d is the locally processed
 *                    token count, not necessarily the reference marking M(p)
 *                    while deposits or withdrawals are in flight. Its ordinary
 *                    gate output is the level "place is marked" (an OR gate).
 *      TRANSITION  = an AND gate. Its descriptor counts the offers it still
 *                    holds no token for; it fires when nothing is missing.
 *
 *  Messages, by direction (value, is_init):
 *
 *      to a place       (true , chg)   deposit: one token added
 *                       (false, chg)   withdrawal: the offered token was used
 *                       (false, init)  first message: initialization;
 *                                      afterwards: RELEASE, the consumer gives
 *                                      the offered token back unused
 *                       (true , init)  REQUEST: a consumer that gave a token
 *                                      back is now ready for it
 *      to a transition  (true , chg)   offer that the transition keeps
 *                       (true , init)  offer from its RELEASE INPUT, which the
 *                                      transition either uses at once or gives
 *                                      back at once
 *
 *  Offer discipline (version 1): a place offers ONE token to ONE consumer at a
 *  time, in round-robin order, and offers again only after that offer has
 *  been answered. This makes every firing legal. Its weakness: a place could
 *  reserve its token for a transition whose other inputs are empty while
 *  another consumer was enabled ("parked token").
 *
 *  Release and request (version 2): each transition may name one input place,
 *  its RELEASE INPUT, normally the one input it shares with other transitions.
 *  An offer from the release input is used only if every other input has
 *  already offered; otherwise it is released and the place offers the token
 *  to the next consumer that has not released it. When every consumer has
 *  released, the place stops offering until one of them sends a request,
 *  which it does when its other inputs become complete. A withdrawal or a
 *  request clears the record of releases. Use edg_pn_auto_release() to
 *  configure the release inputs from the wiring.
 *
 *  Guarantees (ordinary unit-arc nets, correct wiring, exactly-once FIFO
 *  queues, representable counters, each task completing the sends of one step
 *  before its next receive):
 *    * each sequence of firing decisions is legal in the reference net
 *      (with or without release inputs);
 *    * if every transition has at most one input place with several
 *      consumers and that place is its release input (edg_pn_auto_release()
 *      returns 0), a quiescent closed execution has a dead reference marking.
 *      This includes free-choice nets, mutual exclusion and choices steered by
 *      condition places. Transitions with two or more shared inputs keep the
 *      version-1 behaviour and can still park a token.
 *  Round-robin choice, timing and coherence of the place levels with the
 *  marking are not addressed by these guarantees.
 *============================================================================*/
#ifndef EDG_PN_H
#define EDG_PN_H

#include <stddef.h>
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
    edg_state_t st;       /* st.d is the locally processed token count       */
    int8_t      offered;  /* consumer index holding the current offer, -1 no */
    uint8_t     next;     /* rotating pointer: who is offered the next token */
    uint8_t     released; /* bit c: consumer c released since the last
                             withdrawal or request; such consumers are not
                             offered again, and offering stops when every
                             consumer's bit is set                          */
    bool        ready;    /* the (false, init) initialization was processed  */
} edg_pn_place_state_t;

#define EDG_PN_PLACE_INIT \
    ((edg_pn_place_state_t){ .st = EDG_STATE_INIT, .offered = -1, .next = 0, \
                             .released = 0, .ready = false })

typedef enum {
    EDG_PN_IN_DEPOSIT = 0,  /* (true , chg)                                 */
    EDG_PN_IN_WITHDRAW,     /* (false, chg)                                 */
    EDG_PN_IN_INIT,         /* (false, init), first one                     */
    EDG_PN_IN_RELEASE,      /* (false, init), after initialization          */
    EDG_PN_IN_REQUEST       /* (true , init)                                */
} edg_pn_place_input_t;

typedef struct {
    edg_pn_place_input_t kind; /* how the message was interpreted          */
    bool        level_changed; /* the "marked" level changed (or first report) */
    edg_event_t level;         /* the ordinary gate output: value = marked     */
    bool        offer;         /* an offer has to be sent ...                  */
    uint8_t     offer_to;      /* ... to this consumer index                   */
    bool        anomaly;       /* withdrawal/release that answers no offer, or
                                  withdrawal without a token                   */
} edg_pn_place_out_t;

/* Feed one event into a place with `n_consumers` output transitions. */
void edg_pn_place_step(edg_pn_place_state_t *s, uint8_t n_consumers,
                       edg_event_t in, edg_pn_place_out_t *out);

/*---------------------------------------------------------------------------
 *  Transition
 *-------------------------------------------------------------------------*/
typedef struct {
    edg_state_t st;       /* st.d = kept offers still missing for the firing */
} edg_pn_trans_state_t;

typedef enum {
    EDG_PN_WAIT = 0,      /* nothing to send                                 */
    EDG_PN_FIRE,          /* fire: withdrawals, (delay), deposits            */
    EDG_PN_RELEASE,       /* give the release input's offer back             */
    EDG_PN_REQUEST        /* ask the release input for a token               */
} edg_pn_action_t;

/* A transition starts from the structurally known state "no input place has
 * offered anything": d = number of kept inputs (all inputs except the release
 * input), output already reported. */
static inline edg_pn_trans_state_t edg_pn_trans_init(uint8_t n_kept)
{
    edg_pn_trans_state_t s = { .st = { .d = n_kept, .have_out = true,
                                       .last_out = false, .anomaly = false } };
    return s;
}

/* Feed one offer into a transition. `n_kept` is the number of its inputs
 * whose offers it keeps; `has_release` tells whether it has a release input.
 * A kept offer is (true, chg); an offer from the release input is
 * (true, init). On EDG_PN_FIRE all offers are consumed and d is back at
 * n_kept. *anomaly reports a gate counter underflow. */
edg_pn_action_t edg_pn_trans_step(edg_pn_trans_state_t *s, uint8_t n_kept,
                                  bool has_release, edg_event_t offer,
                                  bool *anomaly);

/*===========================================================================
 *  FreeRTOS objects (one task per object, one queue per object)
 *=========================================================================*/

typedef struct {
    const char   *name;
    QueueHandle_t in;                          /* all messages to the place    */
    QueueHandle_t consumer[EDG_PN_MAX_ARCS];   /* queues of output transitions */
    uint8_t       n_consumers;
    uint8_t       release_mask;                /* bit k: consumer k has this
                                                  place as its release input  */
    QueueHandle_t monitor[EDG_MAX_FANOUT];     /* ordinary fan-out: "marked"   */
    uint8_t       n_monitors;
    bool          trace;
} edg_pn_place_t;

typedef struct edg_pn_transition {
    const char   *name;
    QueueHandle_t in;                          /* offers from input places     */
    QueueHandle_t pre[EDG_PN_MAX_ARCS];        /* queues of the input places   */
    uint8_t       n_pre;
    uint8_t       release_input;               /* 1-based index into pre[] of
                                                  the release input, 0 = none */
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

/* Initial marking. Posts one (false, init) initialization message followed by
 * `tokens` deposits (true, chg). Call once per place, before the scheduler
 * starts, so that the initialization is the first message the place sees. */
void edg_pn_mark(QueueHandle_t place_in, unsigned tokens);

/* Configure release inputs from the wiring (matching queue handles). A
 * transition gets a release input when exactly one of its input places has
 * several consumers. Clears any previous configuration. Returns the number of
 * transitions with two or more such inputs: they keep every offer (version-1
 * behaviour) and are outside the no-premature-quiescence guarantee. */
size_t edg_pn_auto_release(edg_pn_place_t *places, size_t n_places,
                           edg_pn_transition_t *transitions, size_t n_transitions);

#endif /* EDG_PN_H */
