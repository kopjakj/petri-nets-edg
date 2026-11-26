/*==============================================================================
 *  edg.h  --  Event-Driven logic software Gates : shared "block library"
 *
 *  Reference implementation for the Raspberry Pi Pico 2 (RP2350) of the
 *  event-driven software-gate model from Jozsef Kopjak's dissertation and
 *  papers (SISY 2012/2013). See ../../ANALYSIS.md for the theory.
 *
 *  The whole model is three kinds of object connected by message queues:
 *
 *      INPUT object  (active) --queue--> GATE object (passive) --queue--> ACTUATOR
 *
 *    * an OBJECT is realised as a FreeRTOS task,
 *    * a WIRE   is realised as a FreeRTOS queue carrying edg_event_t messages,
 *    * fan-out (one output feeding several inputs) is one message posted to
 *      several queues.
 *
 *  A gate keeps ONE integer of state -- the "distance descriptor" d -- and never
 *  stores per-input values nor knows how many inputs it has. That single idea
 *  gives AND/OR/NAND/NOR (level gates), XOR/XNOR (parity gates) and threshold /
 *  voting functions such as majority (symmetric gates), all from one counter.
 *============================================================================*/
#ifndef EDG_H
#define EDG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/*---------------------------------------------------------------------------
 *  The message (event)
 *
 *  Identical to the original FreeRTOS/PIC source: two booleans only. A message
 *  carries NO sender identity -- a receiver knows only *what* changed, never
 *  *who* changed it. That is what makes the network freely composable.
 *-------------------------------------------------------------------------*/
typedef struct {
    bool value;    /* the new logic value: true / false                        */
    bool is_init;  /* true  = initialization message (an object's first, once) */
                   /* false = change-notification (output actually changed)    */
} edg_event_t;

/*---------------------------------------------------------------------------
 *  How many downstream queues a single output may fan out to.
 *-------------------------------------------------------------------------*/
#ifndef EDG_MAX_FANOUT
#define EDG_MAX_FANOUT 8
#endif

/*---------------------------------------------------------------------------
 *  Gate flavours
 *
 *    EDG_LEVEL     output = (d == 0)      -> AND/OR/NAND/NOR via the inverters
 *    EDG_PARITY    output = (d is odd)    -> XOR (odd) / XNOR (even)
 *    EDG_SYMMETRIC output = (d in set)    -> majority, threshold, "k-of-n"
 *-------------------------------------------------------------------------*/
typedef enum {
    EDG_LEVEL = 0,
    EDG_PARITY,
    EDG_SYMMETRIC
} edg_kind_t;

/*---------------------------------------------------------------------------
 *  Named level-gate configurations (which inverters to switch on).
 *
 *  The internal counter always does: received true -> d++, received false
 *  (change) -> d--, output_raw = (d==0). Wrapping that in an input inverter
 *  and an output inverter yields every basic gate (De Morgan in one table):
 *
 *      GATE   invert_in  invert_out
 *      AND      yes         no
 *      NAND     yes         yes
 *      OR       no          yes
 *      NOR      no          no
 *      NOT   =  1-input NOR  (out = !in)
 *      BUF   =  1-input OR   (out =  in)
 *-------------------------------------------------------------------------*/
typedef enum { EDG_AND, EDG_NAND, EDG_OR, EDG_NOR, EDG_NOT, EDG_BUF } edg_logic_t;

/*---------------------------------------------------------------------------
 *  A control object (gate). Passive: it blocks on `in` and runs only when a
 *  message arrives. The sample fills in the wiring (in / out[] / fanout) and
 *  the behaviour (kind + inverters, or a symmetry mask) before creating the
 *  task with edg_gate_task.
 *-------------------------------------------------------------------------*/
typedef struct {
    const char   *name;                 /* label used in the serial trace       */
    edg_kind_t    kind;                  /* LEVEL / PARITY / SYMMETRIC            */
    QueueHandle_t in;                    /* the gate's single input queue        */
    QueueHandle_t out[EDG_MAX_FANOUT];   /* output fan-out queues                */
    uint8_t       fanout;                /* number of valid entries in out[]     */
    bool          invert_in;             /* invert every received value          */
    bool          invert_out;            /* invert every sent value              */
    uint32_t      sym_mask;              /* EDG_SYMMETRIC: bit d set => out true  */
    bool          osc_guard;             /* feedback loops: cap metastable storms */
    bool          trace;                 /* print each processed message to UART  */
} edg_gate_t;

/* Oscillation-guard tuning (only used when edg_gate_t.osc_guard is set). If a
 * gate emits more than EDG_OSC_MAX times within EDG_OSC_WINDOW_MS, it is judged
 * to be in a metastable feedback race; it suppresses further output and warns,
 * then recovers automatically after a quiet period. Normal (settling) circuits
 * emit only a few times per event and never trip this. */
#define EDG_OSC_MAX        24
#define EDG_OSC_WINDOW_MS  4

/*---------------------------------------------------------------------------
 *  When does an input send its ONE initialization message?
 *
 *    EDG_INIT_EAGER    At startup, every input reads its pin and sends an init
 *                      message carrying that level. The whole network settles to
 *                      a defined state immediately -- this is the classic model.
 *
 *    EDG_INIT_ON_EDGE  At startup, an input sends nothing. Its init message is
 *                      sent on the FIRST edge it sees, carrying the level read
 *                      then. Until an input actually changes, the gates downstream
 *                      never hear about it. Use when the resting level is unknown
 *                      or irrelevant and only activity should drive the network.
 *-------------------------------------------------------------------------*/
typedef enum {
    EDG_INIT_EAGER   = 0,   /* read + send init at startup (default) */
    EDG_INIT_ON_EDGE,       /* send the init message on the first edge instead */
} edg_input_init_t;

/*---------------------------------------------------------------------------
 *  An input object (active): watches a GPIO and emits init + change messages.
 *
 *  Interrupt-driven: the pin's edges raise a hardware GPIO interrupt, and the
 *  ISR wakes this object's task (see edg_input_task). The task never polls, so
 *  it consumes nothing while the input is idle. `debounce_ms` is the settle time
 *  the task waits after an edge before reading the (now stable) level.
 *-------------------------------------------------------------------------*/
typedef struct {
    const char       *name;
    uint32_t          gpio;              /* input pin (a button)                 */
    bool              active_low;        /* pressed == low (internal pull-up)    */
    TickType_t        debounce_ms;       /* settle time after an edge before read */
    edg_input_init_t  init_mode;         /* EDG_INIT_EAGER (default) or _ON_EDGE  */
    QueueHandle_t     out[EDG_MAX_FANOUT]; /* where the input's events go         */
    uint8_t           fanout;
    bool              invert_out;
    bool              trace;
} edg_input_t;

/*---------------------------------------------------------------------------
 *  A delay object (passive): forwards each message after a fixed delay. Not a
 *  gate -- it is a transport element. Its main use is to bound the frequency of
 *  a feedback loop (e.g. a ring-oscillator clock) so it does not free-run at
 *  the message-processing rate.
 *-------------------------------------------------------------------------*/
typedef struct {
    const char   *name;
    TickType_t    delay_ms;
    QueueHandle_t in;
    QueueHandle_t out[EDG_MAX_FANOUT];
    uint8_t       fanout;
    bool          invert_out;
    bool          trace;
} edg_delay_t;

/*---------------------------------------------------------------------------
 *  An output object (actuator, passive): drives a GPIO (an LED) from messages.
 *-------------------------------------------------------------------------*/
typedef struct {
    const char   *name;
    uint32_t      gpio;                  /* output pin (an LED)                  */
    bool          active_low;
    QueueHandle_t in;
    bool          invert_in;
    bool          trace;
} edg_actuator_t;

/*===========================================================================
 *  API
 *=========================================================================*/

/* Create a wire (a FreeRTOS queue of edg_event_t, depth `depth`). */
QueueHandle_t edg_wire(UBaseType_t depth);

/* Configure a level gate (kind + inverters) from a named logic function. */
void edg_set_logic(edg_gate_t *g, edg_logic_t type);

/*---------------------------------------------------------------------------
 *  The pure gate state machine (no FreeRTOS, no I/O).
 *
 *  This is the actual behaviour of a gate, factored out so it can be unit
 *  tested on a host. edg_gate_task() is a thin wrapper: receive (applying
 *  invert_in) -> edg_gate_step() -> if it returns true, send (applying
 *  invert_out).
 *-------------------------------------------------------------------------*/
typedef struct {
    int32_t d;         /* the distance descriptor                 */
    bool    have_out;  /* has an init message been emitted yet?    */
    bool    last_out;  /* last output value emitted (pre-invert)   */
    bool    anomaly;   /* set true by the last step if d went < 0  */
} edg_state_t;

#define EDG_STATE_INIT ((edg_state_t){ .d = 0, .have_out = false, .last_out = false, .anomaly = false })

/* Feed one already-input-inverted event `in` into a gate of the given kind.
 * Updates *st. If the output changed (or this is the first output), fills
 * *out (value + is_init, both PRE output-inversion) and returns true. */
bool edg_gate_step(edg_kind_t kind, uint32_t sym_mask,
                   edg_state_t *st, edg_event_t in, edg_event_t *out);

/* The three task bodies. Pass the matching *_t struct as the task parameter;
 * the struct must live for the lifetime of the program (declare it static). */
void edg_input_task(void *param);     /* param: edg_input_t*    */
void edg_gate_task(void *param);      /* param: edg_gate_t*     */
void edg_actuator_task(void *param);  /* param: edg_actuator_t* */
void edg_delay_task(void *param);     /* param: edg_delay_t*    */

/* Build a symmetry mask from a list of "true" distances, e.g. majority(3)
 * is edg_sym({2,3}). Returns a bitmask suitable for edg_gate_t.sym_mask. */
uint32_t edg_sym_mask(const uint8_t *true_distances, size_t n);

/* Small helper: initialise UART/USB stdio and print a banner. */
void edg_stdio_init(const char *sample_name);

#endif /* EDG_H */
