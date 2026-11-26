#ifndef EDG_PN_CHOICE_H
#define EDG_PN_CHOICE_H

#include "edg_pn.h"

/* Ordinary unit arcs. Each bit names one place. */
#define EDG_CHOICE_MAX 32
#define EDG_CHOICE_BIT(i) (UINT32_C(1) << (i))

typedef struct {
    uint32_t inputs, outputs;
} edg_choice_arcs_t;

/* 'id' names a place for INIT/DEPOSIT/WITHDRAW/OFFER, or a transition
 * for OPEN/CLOSE/REQUEST/GRANT. The wiring determines the receiver. */
typedef enum {
    EDG_CHOICE_INIT, EDG_CHOICE_DEPOSIT, EDG_CHOICE_WITHDRAW, EDG_CHOICE_OFFER,
    EDG_CHOICE_OPEN, EDG_CHOICE_CLOSE, EDG_CHOICE_REQUEST, EDG_CHOICE_GRANT
} edg_choice_kind_t;

typedef struct { edg_choice_kind_t kind; unsigned id; } edg_choice_msg_t;
typedef void (*edg_choice_post_t)(void *ctx, edg_choice_msg_t msg);

typedef struct {
    edg_state_t gate;
    bool requested;
} edg_choice_transition_t;

typedef struct {
    uint32_t open, pending;
    unsigned next;
} edg_choice_group_t;

typedef struct {
    unsigned places, transitions;
    edg_choice_arcs_t arcs[EDG_CHOICE_MAX];
    uint32_t consumers[EDG_CHOICE_MAX];
    unsigned group_of_place[EDG_CHOICE_MAX], group_of_transition[EDG_CHOICE_MAX];
    edg_pn_place_state_t place[EDG_CHOICE_MAX];
    edg_choice_transition_t transition[EDG_CHOICE_MAX];
    edg_choice_group_t group[EDG_CHOICE_MAX];
} edg_choice_net_t;

/* Set up all objects before starting workers. Each transition needs an input.
 * Send one INIT to each place before its initial DEPOSIT messages. */
bool edg_choice_init(edg_choice_net_t *net, unsigned places,
                     const edg_choice_arcs_t arcs[], unsigned transitions);

/* Mailbox numbers: places [0,32), transitions [32,64), groups [64,96).
 * Returns -1 for an invalid destination. */
int edg_choice_mailbox(const edg_choice_net_t *net, edg_choice_msg_t msg);

/* Deliver once, FIFO per mailbox, with one handler active per mailbox. Different
 * mailboxes may execute concurrently. post must enqueue reliably, in call order,
 * and never re-enter a handler. false signals a protocol or storage error. */
bool edg_choice_step(edg_choice_net_t *net, edg_choice_msg_t msg,
                     edg_choice_post_t post, void *ctx);
#endif
