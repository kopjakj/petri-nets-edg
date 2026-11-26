/*==============================================================================
 *  01 - Petri net on event-driven gates: a kitting cell
 *
 *      PART ─►(src)─►( IN )─┬─►[ toA ]─►( QA )─►[ workA 1 s ]─►( RA )─┐
 *                           │                                         ├─►[ join ]─►( DONE )─┐
 *                           └─►[ toB ]─►( QB )─►[ workB 2 s ]─►( RB )─┘                     ├─►[ ship ]
 *      SHIP ─►(src)─►( TAKE )───────────────────────────────────────────────────────────────┘
 *
 *      ( ) place      [ ] transition      (src) token source
 *
 *  Every PART flip puts one part into the cell. Parts are dispatched to two
 *  lanes in turn (a free choice at place IN, resolved by rotation), worked on
 *  for 1 s / 2 s, and joined pairwise into a kit. Every SHIP flip takes one
 *  finished kit away.
 *
 *  Demonstrates (see ../../petri/edg_pn.h):
 *    * a PLACE is a level gate whose distance descriptor is the marking:
 *      a `true` message deposits a token, a `false` change withdraws one;
 *    * a TRANSITION is an AND gate whose distance descriptor counts the input
 *      places that have not yet offered a token; it fires at d == 0;
 *    * a place offers ONE token to ONE output transition at a time, which is
 *      what keeps firing legal under any task interleaving;
 *    * the "marked" level of a place is an ordinary gate output: here it
 *      drives four LEDs through ordinary actuator objects;
 *    * a token source turns the rising change of an ordinary input object
 *      into a token, bridging the gate world and the net.
 *
 *  LEDs:  IN   - parts waiting to be dispatched
 *         QA   - lane A has work        QB - lane B has work
 *         DONE - at least one finished kit is waiting to be shipped
 *
 *  Each place, transition, source, input and LED is a FreeRTOS task; each has
 *  one FreeRTOS queue. Watch the serial monitor for the token moves.
 *============================================================================*/
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#include "edg.h"
#include "edg_pn.h"

/* --- pins (see diagram.json) --- */
#define PIN_SW_PART     14
#define PIN_SW_SHIP     15
#define PIN_LED_IN      16
#define PIN_LED_QA      17
#define PIN_LED_QB      18
#define PIN_LED_DONE    19

#define WORK_A_MS       1000
#define WORK_B_MS       2000

#define PRIO       (tskIDLE_PRIORITY + 1)
#define STACK      512

/* Objects must outlive the scheduler start, so give them static storage. */
static edg_input_t         sw_part, sw_ship;
static edg_pn_source_t     src_part, src_ship;
static edg_pn_place_t      p_in, p_qa, p_qb, p_ra, p_rb, p_done, p_take;
static edg_pn_transition_t t_toA, t_toB, t_workA, t_workB, t_join, t_ship;
static edg_actuator_t      led_in, led_qa, led_qb, led_done;

int main(void)
{
    edg_stdio_init("01 Petri net: kitting cell");

    /* --- queues: one per object ------------------------------------------ */
    QueueHandle_t q_src_part = edg_wire(8), q_src_ship = edg_wire(8);
    QueueHandle_t q_in = edg_wire(8), q_qa = edg_wire(8), q_qb = edg_wire(8),
                  q_ra = edg_wire(8), q_rb = edg_wire(8), q_done = edg_wire(8),
                  q_take = edg_wire(8);
    QueueHandle_t q_toA = edg_wire(8), q_toB = edg_wire(8), q_workA = edg_wire(8),
                  q_workB = edg_wire(8), q_join = edg_wire(8), q_ship = edg_wire(8);
    QueueHandle_t q_led_in = edg_wire(8), q_led_qa = edg_wire(8),
                  q_led_qb = edg_wire(8), q_led_done = edg_wire(8);
    configASSERT(q_src_part && q_src_ship && q_in && q_qa && q_qb && q_ra && q_rb &&
                 q_done && q_take && q_toA && q_toB && q_workA && q_workB &&
                 q_join && q_ship && q_led_in && q_led_qa && q_led_qb && q_led_done);

    /* --- the physical world: two switches, each feeding a token source ---- */
    sw_part = (edg_input_t){ .name = "PART", .gpio = PIN_SW_PART, .active_low = true,
                             .debounce_ms = 20, .init_mode = EDG_INIT_EAGER,
                             .out = { q_src_part }, .fanout = 1, .trace = true };
    sw_ship = (edg_input_t){ .name = "SHIP", .gpio = PIN_SW_SHIP, .active_low = true,
                             .debounce_ms = 20, .init_mode = EDG_INIT_EAGER,
                             .out = { q_src_ship }, .fanout = 1, .trace = true };
    src_part = (edg_pn_source_t){ .name = "arrive", .in = q_src_part,
                                  .post = { q_in },   .n_post = 1, .trace = true };
    src_ship = (edg_pn_source_t){ .name = "order",  .in = q_src_ship,
                                  .post = { q_take }, .n_post = 1, .trace = true };

    /* --- places: who may take a token (consumers), who watches (monitors) -- */
    p_in   = (edg_pn_place_t){ .name = "IN",   .in = q_in,
                               .consumer = { q_toA, q_toB }, .n_consumers = 2,
                               .monitor  = { q_led_in },     .n_monitors  = 1, .trace = true };
    p_qa   = (edg_pn_place_t){ .name = "QA",   .in = q_qa,
                               .consumer = { q_workA },      .n_consumers = 1,
                               .monitor  = { q_led_qa },     .n_monitors  = 1, .trace = true };
    p_qb   = (edg_pn_place_t){ .name = "QB",   .in = q_qb,
                               .consumer = { q_workB },      .n_consumers = 1,
                               .monitor  = { q_led_qb },     .n_monitors  = 1, .trace = true };
    p_ra   = (edg_pn_place_t){ .name = "RA",   .in = q_ra,
                               .consumer = { q_join },       .n_consumers = 1, .trace = true };
    p_rb   = (edg_pn_place_t){ .name = "RB",   .in = q_rb,
                               .consumer = { q_join },       .n_consumers = 1, .trace = true };
    p_done = (edg_pn_place_t){ .name = "DONE", .in = q_done,
                               .consumer = { q_ship },       .n_consumers = 1,
                               .monitor  = { q_led_done },   .n_monitors  = 1, .trace = true };
    p_take = (edg_pn_place_t){ .name = "TAKE", .in = q_take,
                               .consumer = { q_ship },       .n_consumers = 1, .trace = true };

    /* --- transitions: input places (pre), output places (post) ------------ */
    t_toA   = (edg_pn_transition_t){ .name = "toA",   .in = q_toA,
                                     .pre = { q_in }, .n_pre = 1,
                                     .post = { q_qa }, .n_post = 1, .trace = true };
    t_toB   = (edg_pn_transition_t){ .name = "toB",   .in = q_toB,
                                     .pre = { q_in }, .n_pre = 1,
                                     .post = { q_qb }, .n_post = 1, .trace = true };
    t_workA = (edg_pn_transition_t){ .name = "workA", .in = q_workA,
                                     .pre = { q_qa }, .n_pre = 1,
                                     .post = { q_ra }, .n_post = 1,
                                     .duration_ms = WORK_A_MS, .trace = true };
    t_workB = (edg_pn_transition_t){ .name = "workB", .in = q_workB,
                                     .pre = { q_qb }, .n_pre = 1,
                                     .post = { q_rb }, .n_post = 1,
                                     .duration_ms = WORK_B_MS, .trace = true };
    t_join  = (edg_pn_transition_t){ .name = "join",  .in = q_join,
                                     .pre = { q_ra, q_rb }, .n_pre = 2,
                                     .post = { q_done }, .n_post = 1, .trace = true };
    t_ship  = (edg_pn_transition_t){ .name = "ship",  .in = q_ship,
                                     .pre = { q_done, q_take }, .n_pre = 2,
                                     .n_post = 0, .trace = true };

    /* --- ordinary actuators on the "marked" levels ------------------------ */
    led_in   = (edg_actuator_t){ .name = "L_IN",   .gpio = PIN_LED_IN,   .in = q_led_in,   .trace = false };
    led_qa   = (edg_actuator_t){ .name = "L_QA",   .gpio = PIN_LED_QA,   .in = q_led_qa,   .trace = false };
    led_qb   = (edg_actuator_t){ .name = "L_QB",   .gpio = PIN_LED_QB,   .in = q_led_qb,   .trace = false };
    led_done = (edg_actuator_t){ .name = "L_DONE", .gpio = PIN_LED_DONE, .in = q_led_done, .trace = false };

    /* --- initial marking: the cell starts empty --------------------------- */
    edg_pn_mark(q_in, 0);   edg_pn_mark(q_qa, 0);   edg_pn_mark(q_qb, 0);
    edg_pn_mark(q_ra, 0);   edg_pn_mark(q_rb, 0);   edg_pn_mark(q_done, 0);
    edg_pn_mark(q_take, 0);

    /* --- create the tasks -------------------------------------------------- */
    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(edg_input_task,         "PART",   STACK, &sw_part,  PRIO, NULL);
    ok &= xTaskCreate(edg_input_task,         "SHIP",   STACK, &sw_ship,  PRIO, NULL);
    ok &= xTaskCreate(edg_pn_source_task,     "arrive", STACK, &src_part, PRIO, NULL);
    ok &= xTaskCreate(edg_pn_source_task,     "order",  STACK, &src_ship, PRIO, NULL);
    ok &= xTaskCreate(edg_pn_place_task,      "IN",     STACK, &p_in,     PRIO, NULL);
    ok &= xTaskCreate(edg_pn_place_task,      "QA",     STACK, &p_qa,     PRIO, NULL);
    ok &= xTaskCreate(edg_pn_place_task,      "QB",     STACK, &p_qb,     PRIO, NULL);
    ok &= xTaskCreate(edg_pn_place_task,      "RA",     STACK, &p_ra,     PRIO, NULL);
    ok &= xTaskCreate(edg_pn_place_task,      "RB",     STACK, &p_rb,     PRIO, NULL);
    ok &= xTaskCreate(edg_pn_place_task,      "DONE",   STACK, &p_done,   PRIO, NULL);
    ok &= xTaskCreate(edg_pn_place_task,      "TAKE",   STACK, &p_take,   PRIO, NULL);
    ok &= xTaskCreate(edg_pn_transition_task, "toA",    STACK, &t_toA,    PRIO, NULL);
    ok &= xTaskCreate(edg_pn_transition_task, "toB",    STACK, &t_toB,    PRIO, NULL);
    ok &= xTaskCreate(edg_pn_transition_task, "workA",  STACK, &t_workA,  PRIO, NULL);
    ok &= xTaskCreate(edg_pn_transition_task, "workB",  STACK, &t_workB,  PRIO, NULL);
    ok &= xTaskCreate(edg_pn_transition_task, "join",   STACK, &t_join,   PRIO, NULL);
    ok &= xTaskCreate(edg_pn_transition_task, "ship",   STACK, &t_ship,   PRIO, NULL);
    ok &= xTaskCreate(edg_actuator_task,      "L_IN",   STACK, &led_in,   PRIO, NULL);
    ok &= xTaskCreate(edg_actuator_task,      "L_QA",   STACK, &led_qa,   PRIO, NULL);
    ok &= xTaskCreate(edg_actuator_task,      "L_QB",   STACK, &led_qb,   PRIO, NULL);
    ok &= xTaskCreate(edg_actuator_task,      "L_DONE", STACK, &led_done, PRIO, NULL);
    configASSERT(ok == pdPASS);

    printf("starting scheduler...\n");
    vTaskStartScheduler();

    /* Only reached if the kernel could not allocate the idle or timer task. */
    printf("!! vTaskStartScheduler() returned -- out of heap\n");
    for (;;) { }
}
