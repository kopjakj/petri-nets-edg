/*==============================================================================
 *  02 - Petri net + gates: a batch mixer (sequential control)
 *
 *                      ┌─►( FILL_A )─►[ endA ]─►( FULL_A )─┐
 *   START─►(src)─►( REQ )─►[ start ]                        ├─►[ join ]─►( MIXING )─►[ mixdone ]─►( DRAIN )─►[ endD ]─┐
 *          ( IDLE )──────►    │    └─►( FILL_B )─►[ endB ]─►( FULL_B )─┘                                              │
 *             ▲                                                                                                       │
 *             └───────────────────────────────────────────────────────────────────────────────────────────────────────┘
 *
 *  A step ends on a CONDITION, and the condition is computed by ordinary gates:
 *
 *      FILL_A marked ──┐
 *                      ├─►[ AND ]─►(src)─►( evA )─►[ endA ]        "level A reached while filling A"
 *      sensor LVL_A ───┘
 *
 *      MIXING marked ─►[ DELAY 2 s ]─►(src)─►( evT )─►[ mixdone ]  "2 s after mixing began"
 *
 *  Demonstrates:
 *    * a sequential controller with two parallel branches (fork at `start`,
 *      synchronisation at `join`) written as a Petri net;
 *    * the two worlds working together: the "marked" level of a step place and
 *      a sensor level meet in an ordinary EDG AND gate, and the rising change
 *      of that gate becomes a token (Grafcet would call the AND a receptivity);
 *    * a step timer built from the ordinary DELAY object of the block library;
 *    * actuators (valves, mixer motor) driven by the "marked" levels of places.
 *
 *  Switches: START   flip on  -> one batch is requested (requests are counted)
 *            LVL_A   on = level of ingredient A reached
 *            LVL_B   on = level of ingredient B reached
 *            EMPTY   on = tank empty
 *  LEDs:     VALVE_A, VALVE_B (filling), MIXER (mixing), DRAIN (draining)
 *
 *  If a condition already holds when its step begins (LVL_A still on from the
 *  previous batch), the step ends at once: the AND gate rises when the step
 *  place becomes marked. That is the intended behaviour of a level condition.
 *============================================================================*/
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#include "edg.h"
#include "edg_pn.h"

/* --- pins (see diagram.json) --- */
#define PIN_SW_START    2
#define PIN_SW_LVL_A    3
#define PIN_SW_LVL_B    4
#define PIN_SW_EMPTY    5
#define PIN_LED_VALVE_A 16
#define PIN_LED_VALVE_B 17
#define PIN_LED_MIXER   18
#define PIN_LED_DRAIN   19

#define MIX_MS          2000

#define PRIO       (tskIDLE_PRIORITY + 1)
#define STACK      512

static edg_input_t         sw_start, sw_lvlA, sw_lvlB, sw_empty;
static edg_gate_t          g_A, g_B, g_E;                 /* the conditions   */
static edg_delay_t         dly_T;                         /* the step timer   */
static edg_pn_source_t     src_req, src_A, src_B, src_T, src_E;
static edg_pn_place_t      p_idle, p_req, p_fillA, p_fillB, p_fullA, p_fullB,
                           p_mixing, p_drain, p_evA, p_evB, p_evT, p_evE;
static edg_pn_transition_t t_start, t_endA, t_endB, t_join, t_mixdone, t_endD;
static edg_actuator_t      led_A, led_B, led_mix, led_drain;

#define TASK(fn, name, obj)  ok &= xTaskCreate((fn), (name), STACK, &(obj), PRIO, NULL)

int main(void)
{
    edg_stdio_init("02 Petri net + gates: batch mixer");

    /* --- queues: one per object ------------------------------------------ */
    QueueHandle_t q_src_req = edg_wire(8), q_src_A = edg_wire(8), q_src_B = edg_wire(8),
                  q_src_T = edg_wire(8), q_src_E = edg_wire(8);
    QueueHandle_t q_gA = edg_wire(8), q_gB = edg_wire(8), q_gE = edg_wire(8), q_dly = edg_wire(8);
    QueueHandle_t q_idle = edg_wire(8), q_req = edg_wire(8), q_fillA = edg_wire(8),
                  q_fillB = edg_wire(8), q_fullA = edg_wire(8), q_fullB = edg_wire(8),
                  q_mixing = edg_wire(8), q_drain = edg_wire(8), q_evA = edg_wire(8),
                  q_evB = edg_wire(8), q_evT = edg_wire(8), q_evE = edg_wire(8);
    QueueHandle_t q_start = edg_wire(8), q_endA = edg_wire(8), q_endB = edg_wire(8),
                  q_join = edg_wire(8), q_mixdone = edg_wire(8), q_endD = edg_wire(8);
    QueueHandle_t q_ledA = edg_wire(8), q_ledB = edg_wire(8), q_ledM = edg_wire(8),
                  q_ledD = edg_wire(8);
    configASSERT(q_src_req && q_src_A && q_src_B && q_src_T && q_src_E && q_gA && q_gB &&
                 q_gE && q_dly && q_idle && q_req && q_fillA && q_fillB && q_fullA &&
                 q_fullB && q_mixing && q_drain && q_evA && q_evB && q_evT && q_evE &&
                 q_start && q_endA && q_endB && q_join && q_mixdone && q_endD &&
                 q_ledA && q_ledB && q_ledM && q_ledD);

    /* --- the physical world: four switches -------------------------------- */
    sw_start = (edg_input_t){ .name = "START", .gpio = PIN_SW_START, .active_low = true,
                              .debounce_ms = 20, .init_mode = EDG_INIT_EAGER,
                              .out = { q_src_req }, .fanout = 1, .trace = true };
    sw_lvlA  = (edg_input_t){ .name = "LVL_A", .gpio = PIN_SW_LVL_A, .active_low = true,
                              .debounce_ms = 20, .init_mode = EDG_INIT_EAGER,
                              .out = { q_gA }, .fanout = 1, .trace = true };
    sw_lvlB  = (edg_input_t){ .name = "LVL_B", .gpio = PIN_SW_LVL_B, .active_low = true,
                              .debounce_ms = 20, .init_mode = EDG_INIT_EAGER,
                              .out = { q_gB }, .fanout = 1, .trace = true };
    sw_empty = (edg_input_t){ .name = "EMPTY", .gpio = PIN_SW_EMPTY, .active_low = true,
                              .debounce_ms = 20, .init_mode = EDG_INIT_EAGER,
                              .out = { q_gE }, .fanout = 1, .trace = true };

    /* --- conditions: ordinary gates over a place level and a sensor ------- */
    g_A = (edg_gate_t){ .name = "condA", .in = q_gA, .out = { q_src_A }, .fanout = 1 };
    g_B = (edg_gate_t){ .name = "condB", .in = q_gB, .out = { q_src_B }, .fanout = 1 };
    g_E = (edg_gate_t){ .name = "condE", .in = q_gE, .out = { q_src_E }, .fanout = 1 };
    edg_set_logic(&g_A, EDG_AND);
    edg_set_logic(&g_B, EDG_AND);
    edg_set_logic(&g_E, EDG_AND);

    /* --- step timer: the MIXING level, delayed ---------------------------- */
    dly_T = (edg_delay_t){ .name = "timer", .delay_ms = MIX_MS, .in = q_dly,
                           .out = { q_src_T }, .fanout = 1 };

    /* --- token sources: a rising change becomes one token ----------------- */
    src_req = (edg_pn_source_t){ .name = "request", .in = q_src_req, .post = { q_req }, .n_post = 1, .trace = true };
    src_A   = (edg_pn_source_t){ .name = "evA",     .in = q_src_A,   .post = { q_evA }, .n_post = 1, .trace = true };
    src_B   = (edg_pn_source_t){ .name = "evB",     .in = q_src_B,   .post = { q_evB }, .n_post = 1, .trace = true };
    src_T   = (edg_pn_source_t){ .name = "evT",     .in = q_src_T,   .post = { q_evT }, .n_post = 1, .trace = true };
    src_E   = (edg_pn_source_t){ .name = "evE",     .in = q_src_E,   .post = { q_evE }, .n_post = 1, .trace = true };

    /* --- places ------------------------------------------------------------ */
    p_idle   = (edg_pn_place_t){ .name = "IDLE",   .in = q_idle,   .consumer = { q_start },   .n_consumers = 1 };
    p_req    = (edg_pn_place_t){ .name = "REQ",    .in = q_req,    .consumer = { q_start },   .n_consumers = 1 };
    p_fillA  = (edg_pn_place_t){ .name = "FILL_A", .in = q_fillA,  .consumer = { q_endA },    .n_consumers = 1,
                                 .monitor = { q_ledA, q_gA },  .n_monitors = 2 };
    p_fillB  = (edg_pn_place_t){ .name = "FILL_B", .in = q_fillB,  .consumer = { q_endB },    .n_consumers = 1,
                                 .monitor = { q_ledB, q_gB },  .n_monitors = 2 };
    p_fullA  = (edg_pn_place_t){ .name = "FULL_A", .in = q_fullA,  .consumer = { q_join },    .n_consumers = 1 };
    p_fullB  = (edg_pn_place_t){ .name = "FULL_B", .in = q_fullB,  .consumer = { q_join },    .n_consumers = 1 };
    p_mixing = (edg_pn_place_t){ .name = "MIXING", .in = q_mixing, .consumer = { q_mixdone }, .n_consumers = 1,
                                 .monitor = { q_ledM, q_dly }, .n_monitors = 2 };
    p_drain  = (edg_pn_place_t){ .name = "DRAIN",  .in = q_drain,  .consumer = { q_endD },    .n_consumers = 1,
                                 .monitor = { q_ledD, q_gE },  .n_monitors = 2 };
    p_evA    = (edg_pn_place_t){ .name = "evA",    .in = q_evA,    .consumer = { q_endA },    .n_consumers = 1 };
    p_evB    = (edg_pn_place_t){ .name = "evB",    .in = q_evB,    .consumer = { q_endB },    .n_consumers = 1 };
    p_evT    = (edg_pn_place_t){ .name = "evT",    .in = q_evT,    .consumer = { q_mixdone }, .n_consumers = 1 };
    p_evE    = (edg_pn_place_t){ .name = "evE",    .in = q_evE,    .consumer = { q_endD },    .n_consumers = 1 };

    /* --- transitions --------------------------------------------------------- */
    t_start   = (edg_pn_transition_t){ .name = "start",   .in = q_start,
                    .pre = { q_idle, q_req },     .n_pre = 2,
                    .post = { q_fillA, q_fillB }, .n_post = 2, .trace = true };
    t_endA    = (edg_pn_transition_t){ .name = "endA",    .in = q_endA,
                    .pre = { q_fillA, q_evA },    .n_pre = 2,
                    .post = { q_fullA },          .n_post = 1, .trace = true };
    t_endB    = (edg_pn_transition_t){ .name = "endB",    .in = q_endB,
                    .pre = { q_fillB, q_evB },    .n_pre = 2,
                    .post = { q_fullB },          .n_post = 1, .trace = true };
    t_join    = (edg_pn_transition_t){ .name = "join",    .in = q_join,
                    .pre = { q_fullA, q_fullB },  .n_pre = 2,
                    .post = { q_mixing },         .n_post = 1, .trace = true };
    t_mixdone = (edg_pn_transition_t){ .name = "mixdone", .in = q_mixdone,
                    .pre = { q_mixing, q_evT },   .n_pre = 2,
                    .post = { q_drain },          .n_post = 1, .trace = true };
    t_endD    = (edg_pn_transition_t){ .name = "endD",    .in = q_endD,
                    .pre = { q_drain, q_evE },    .n_pre = 2,
                    .post = { q_idle },           .n_post = 1, .trace = true };

    /* --- actuators on the "marked" levels ---------------------------------- */
    led_A     = (edg_actuator_t){ .name = "VALVE_A", .gpio = PIN_LED_VALVE_A, .in = q_ledA };
    led_B     = (edg_actuator_t){ .name = "VALVE_B", .gpio = PIN_LED_VALVE_B, .in = q_ledB };
    led_mix   = (edg_actuator_t){ .name = "MIXER",   .gpio = PIN_LED_MIXER,   .in = q_ledM };
    led_drain = (edg_actuator_t){ .name = "DRAIN",   .gpio = PIN_LED_DRAIN,   .in = q_ledD };

    /* --- initial marking: the controller is idle --------------------------- */
    edg_pn_mark(q_idle, 1);
    edg_pn_mark(q_req, 0);    edg_pn_mark(q_fillA, 0);  edg_pn_mark(q_fillB, 0);
    edg_pn_mark(q_fullA, 0);  edg_pn_mark(q_fullB, 0);  edg_pn_mark(q_mixing, 0);
    edg_pn_mark(q_drain, 0);  edg_pn_mark(q_evA, 0);    edg_pn_mark(q_evB, 0);
    edg_pn_mark(q_evT, 0);    edg_pn_mark(q_evE, 0);

    /* --- create the tasks ---------------------------------------------------- */
    BaseType_t ok = pdPASS;
    TASK(edg_input_task, "START", sw_start);   TASK(edg_input_task, "LVL_A", sw_lvlA);
    TASK(edg_input_task, "LVL_B", sw_lvlB);    TASK(edg_input_task, "EMPTY", sw_empty);
    TASK(edg_gate_task, "condA", g_A);         TASK(edg_gate_task, "condB", g_B);
    TASK(edg_gate_task, "condE", g_E);         TASK(edg_delay_task, "timer", dly_T);
    TASK(edg_pn_source_task, "request", src_req);
    TASK(edg_pn_source_task, "evA", src_A);    TASK(edg_pn_source_task, "evB", src_B);
    TASK(edg_pn_source_task, "evT", src_T);    TASK(edg_pn_source_task, "evE", src_E);
    TASK(edg_pn_place_task, "IDLE", p_idle);       TASK(edg_pn_place_task, "REQ", p_req);
    TASK(edg_pn_place_task, "FILL_A", p_fillA);    TASK(edg_pn_place_task, "FILL_B", p_fillB);
    TASK(edg_pn_place_task, "FULL_A", p_fullA);    TASK(edg_pn_place_task, "FULL_B", p_fullB);
    TASK(edg_pn_place_task, "MIXING", p_mixing);   TASK(edg_pn_place_task, "DRAIN", p_drain);
    TASK(edg_pn_place_task, "p_evA", p_evA);       TASK(edg_pn_place_task, "p_evB", p_evB);
    TASK(edg_pn_place_task, "p_evT", p_evT);       TASK(edg_pn_place_task, "p_evE", p_evE);
    TASK(edg_pn_transition_task, "start", t_start);     TASK(edg_pn_transition_task, "endA", t_endA);
    TASK(edg_pn_transition_task, "endB", t_endB);       TASK(edg_pn_transition_task, "join", t_join);
    TASK(edg_pn_transition_task, "mixdone", t_mixdone); TASK(edg_pn_transition_task, "endD", t_endD);
    TASK(edg_actuator_task, "VALVE_A", led_A);   TASK(edg_actuator_task, "VALVE_B", led_B);
    TASK(edg_actuator_task, "MIXER", led_mix);   TASK(edg_actuator_task, "DRAIN", led_drain);
    configASSERT(ok == pdPASS);

    printf("starting scheduler...\n");
    vTaskStartScheduler();

    /* Only reached if the kernel could not allocate the idle or timer task. */
    printf("!! vTaskStartScheduler() returned -- out of heap\n");
    for (;;) { }
}
