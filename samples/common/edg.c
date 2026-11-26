/*==============================================================================
 *  edg.c  --  Event-Driven logic software Gates : shared "block library"
 *
 *  See edg.h for the API and ../../ANALYSIS.md for the underlying theory.
 *============================================================================*/
#include "edg.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"   /* gpio_set_irq_enabled_with_callback, GPIO_IRQ_* */

/*---------------------------------------------------------------------------
 *  Internal helpers -- send / receive, applying the configurable inverters.
 *  These mirror vSendEventMsg / vReceiveEventMsg in the original PIC source.
 *-------------------------------------------------------------------------*/

/* Fan an event out to every configured output queue, inverting first if asked.
 * portMAX_DELAY: if a downstream queue is momentarily full we block rather than
 * drop -- a dropped change message would desynchronise a distance counter. */
static void send_event(QueueHandle_t *out, uint8_t fanout, bool invert,
                       edg_event_t ev)
{
    if (invert) {
        ev.value = !ev.value;
    }
    for (uint8_t i = 0; i < fanout; i++) {
        xQueueSendToBack(out[i], &ev, portMAX_DELAY);
    }
}

/* Block until a message arrives, inverting the received value if asked. */
static void receive_event(QueueHandle_t in, bool invert, edg_event_t *ev)
{
    xQueueReceive(in, ev, portMAX_DELAY);
    if (invert) {
        ev->value = !ev->value;
    }
}

static void trace_msg(const char *who, const char *dir, edg_event_t ev, int d)
{
    /* Only a GATE carries a distance descriptor. Every other object (input,
     * delay, actuator) passes d < 0 to say "no distance", and we drop the field
     * rather than print a meaningless d=-1. */
    if (d < 0) {
        /* e.g.  [SW_A   ] in =1 (chg)   */
        printf("[%-7s] %s=%d (%s)\n",
               who, dir, ev.value ? 1 : 0, ev.is_init ? "init" : "chg");
    } else {
        /* e.g.  [AND    ] out=1 (chg)  d=0   */
        printf("[%-7s] %s=%d (%s)  d=%d\n",
               who, dir, ev.value ? 1 : 0, ev.is_init ? "init" : "chg", d);
    }
}

/*===========================================================================
 *  Public helpers
 *=========================================================================*/

QueueHandle_t edg_wire(UBaseType_t depth)
{
    return xQueueCreate(depth, sizeof(edg_event_t));
}

void edg_set_logic(edg_gate_t *g, edg_logic_t type)
{
    g->kind = EDG_LEVEL;
    switch (type) {
        case EDG_AND:  g->invert_in = true;  g->invert_out = false; break;
        case EDG_NAND: g->invert_in = true;  g->invert_out = true;  break;
        case EDG_OR:   g->invert_in = false; g->invert_out = true;  break;
        case EDG_NOR:  g->invert_in = false; g->invert_out = false; break;
        /* A one-input gate: NOT = 1-input NOR (out = !in), BUF = 1-input OR. */
        case EDG_NOT:  g->invert_in = false; g->invert_out = false; break;
        case EDG_BUF:  g->invert_in = false; g->invert_out = true;  break;
    }
}

bool edg_gate_step(edg_kind_t kind, uint32_t sym_mask,
                   edg_state_t *st, edg_event_t in, edg_event_t *out)
{
    /* --- update the distance counter --- */
    if (in.value) {
        st->d++;
    } else if (!in.is_init) {
        st->d--;
    }

    /* Protocol-anomaly guard: d must never go negative. If it does, a
     * change-'false' arrived with no matching earlier 'true' -- a wiring
     * mistake or a lost init. Clamp so the count cannot corrupt. */
    st->anomaly = false;
    if (st->d < 0) {
        st->d = 0;
        st->anomaly = true;
    }

    /* --- decide the output value from d --- */
    bool value;
    switch (kind) {
        case EDG_PARITY:                              /* XOR / XNOR          */
            value = (st->d & 1);
            break;
        case EDG_SYMMETRIC:                            /* majority, k-of-n    */
            value = (st->d < 32) && ((sym_mask >> st->d) & 1u);
            break;
        case EDG_LEVEL:                                /* AND/OR/NAND/NOR     */
        default:
            value = (st->d == 0);
            break;
    }

    /* --- emit only when the output actually changed (or first time) --- */
    bool emit = (!st->have_out || value != st->last_out);
    if (emit) {
        out->value   = value;
        out->is_init = !st->have_out;
        st->last_out = value;
        st->have_out = true;
    }
    return emit;
}

uint32_t edg_sym_mask(const uint8_t *true_distances, size_t n)
{
    uint32_t m = 0;
    for (size_t i = 0; i < n; i++) {
        if (true_distances[i] < 32) {
            m |= (1u << true_distances[i]);
        }
    }
    return m;
}

void edg_stdio_init(const char *sample_name)
{
    stdio_init_all();
    /* Give a host serial monitor a moment to attach before the banner. */
    sleep_ms(300);
    printf("\n=== Event-Driven Gates :: %s ===\n", sample_name);
    printf("(distance-descriptor software gates on RP2350 + FreeRTOS)\n\n");
}

/*===========================================================================
 *  Kernel hooks
 *
 *  The idle task halts the core via configUSE_TICKLESS_IDLE (see
 *  FreeRTOSConfig.h): the port's vPortSuppressTicksAndSleep() stops SysTick and
 *  executes `wfi` until the next task is due, so no idle or tick hook is needed.
 *
 *  configCHECK_FOR_STACK_OVERFLOW / configUSE_MALLOC_FAILED_HOOK turn the two
 *  most common ways a FreeRTOS app dies -- a task overruns its stack, or the heap
 *  runs out while creating a task or queue -- from silent hangs into a panic.
 *=========================================================================*/
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    panic("FreeRTOS: stack overflow in task '%s'", name);
}

void vApplicationMallocFailedHook(void)
{
    panic("FreeRTOS: heap exhausted (raise configTOTAL_HEAP_SIZE)");
}

/*===========================================================================
 *  INPUT object (active) -- interrupt driven
 *
 *  The only "active" object: the source of every event in the network. It does
 *  NOT poll. Instead the pin's rising/falling edges raise the hardware GPIO
 *  interrupt (IO_IRQ_BANK0); the shared ISR below wakes the owning task, which
 *  debounces, reads the settled level, and emits a message if the level changed.
 *
 *  While the input is idle the task is blocked on a notification and costs
 *  nothing -- which, with configUSE_TICKLESS_IDLE, lets the core halt.
 *
 *  Initialization (edg_input_t.init_mode):
 *    EDG_INIT_EAGER    read the pin at startup and send the init message now.
 *    EDG_INIT_ON_EDGE  send nothing at startup; the first edge sends the init.
 *=========================================================================*/

/* One IO_IRQ_BANK0 interrupt serves every GPIO, and its callback carries no
 * context, so map each pin to the task that owns it. Indexed by GPIO number;
 * NUM_BANK0_GPIOS is 30 on the RP2040 and 48 on the RP2350. One input object per
 * pin (a second on the same pin would overwrite the first). */
static TaskHandle_t s_input_task_by_gpio[NUM_BANK0_GPIOS];

/* Runs in the GPIO-bank ISR on every configured edge. It only wakes the owning
 * task -- the debounce and the pin read happen in the task, not in here. The
 * notification also un-halts a tickless-idle core, because IO_IRQ_BANK0 is a
 * real NVIC interrupt. */
static void edg_gpio_callback(uint gpio, uint32_t events)
{
    (void)events;
    if (gpio < NUM_BANK0_GPIOS && s_input_task_by_gpio[gpio] != NULL) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_input_task_by_gpio[gpio],
                               &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/* Read the logic level of an input, applying its active-low sense. */
static bool edg_input_read(const edg_input_t *in)
{
    bool raw = gpio_get(in->gpio);
    return in->active_low ? !raw : raw;
}

void edg_input_task(void *param)
{
    edg_input_t *in = (edg_input_t *)param;

    gpio_init(in->gpio);
    gpio_set_dir(in->gpio, GPIO_IN);
    if (in->active_low) {
        gpio_pull_up(in->gpio);
    } else {
        gpio_pull_down(in->gpio);
    }

    /* Publish our handle BEFORE enabling the interrupt, so an edge arriving the
     * instant we arm it already finds a task to wake. Then enable both edges on
     * this pin; gpio_set_irq_enabled_with_callback also installs the one shared
     * callback and enables the IO_IRQ_BANK0 line (both idempotent, so it is safe
     * for every input task to call). */
    s_input_task_by_gpio[in->gpio] = xTaskGetCurrentTaskHandle();
    gpio_set_irq_enabled_with_callback(in->gpio,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, edg_gpio_callback);

    bool last     = false;   /* last level we reported                         */
    bool sent_init = false;  /* has the one init message gone out yet?          */

    /* EDG_INIT_EAGER: announce the resting level right away. */
    if (in->init_mode == EDG_INIT_EAGER) {
        last = edg_input_read(in);
        edg_event_t ev = { .value = last, .is_init = true };
        if (in->trace) {
            trace_msg(in->name, "in ", ev, -1);
        }
        send_event(in->out, in->fanout, in->invert_out, ev);
        sent_init = true;
    }

    for (;;) {
        /* Block until the ISR reports the first edge of a transition. */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Debounce. A real contact -- and a Wokwi switch, which models bounce by
         * default -- does not flip once: it "opens and closes tens or hundreds
         * of times" over the settling window. Each of those edges is a separate
         * interrupt, so if we left the pin's IRQ armed here every bounce edge
         * would post another notification; those pile up and re-trigger this
         * loop the instant we finish, and successive reads catching opposite
         * bounce phases spray the trace with in=1/in=0/in=1/... chatter.
         *
         * So silence THIS pin's interrupt while the line settles (other inputs
         * keep theirs -- gpio_set_irq_enabled is per-GPIO), wait, then read the
         * level that actually stuck. */
        gpio_set_irq_enabled(in->gpio,
                             GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        vTaskDelay(pdMS_TO_TICKS(in->debounce_ms));
        bool level = edg_input_read(in);

        /* Discard every notification the bounce burst left pending (non-blocking
         * take, clears the count), then re-arm. Both done with the IRQ still off
         * so no genuine later edge can slip in and be cleared: the next real edge
         * after re-arming wakes us cleanly. */
        ulTaskNotifyTake(pdTRUE, 0);
        gpio_set_irq_enabled(in->gpio,
                             GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

        if (!sent_init) {
            /* EDG_INIT_ON_EDGE: the first edge carries the init message. */
            edg_event_t ev = { .value = level, .is_init = true };
            if (in->trace) {
                trace_msg(in->name, "in ", ev, -1);
            }
            send_event(in->out, in->fanout, in->invert_out, ev);
            last = level;
            sent_init = true;
        } else if (level != last) {
            edg_event_t ev = { .value = level, .is_init = false };
            if (in->trace) {
                trace_msg(in->name, "in ", ev, -1);
            }
            send_event(in->out, in->fanout, in->invert_out, ev);
            last = level;
        }
        /* Bounce that settles back to `last` yields no message -- correct. */
    }
}

/*===========================================================================
 *  GATE / control object (passive)
 *
 *  The heart of the model. Blocks on its input queue; when a message arrives it
 *  updates the single distance counter d and, if that changes its output, emits
 *  one message. It never learns how many inputs it has.
 *
 *  Counter rule (identical for every flavour):
 *      received true          -> d++      (a "dominant" value seen)
 *      received false, change -> d--      (a previously-seen dominant retracted)
 *      received false, init   -> (nothing; there was no prior true to cancel)
 *
 *  Output rule depends on the flavour (see edg_kind_t).
 *=========================================================================*/
void edg_gate_task(void *param)
{
    edg_gate_t *g = (edg_gate_t *)param;
    edg_state_t st = EDG_STATE_INIT;

    /* oscillation-guard state (only active when g->osc_guard) */
    TickType_t last_tick = xTaskGetTickCount();
    uint32_t   osc = 0;
    bool       suppressed = false;

    for (;;) {
        edg_event_t ev;
        receive_event(g->in, g->invert_in, &ev);   /* applies invert_in */

        edg_event_t o;
        bool emit = edg_gate_step(g->kind, g->sym_mask, &st, ev, &o);

        if (st.anomaly) {
            printf("[%-7s] !! protocol anomaly: d<0 (clamped)\n", g->name);
        }

        if (emit && g->osc_guard) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_tick) > pdMS_TO_TICKS(EDG_OSC_WINDOW_MS)) {
                osc = 0;                 /* quiet period -> recovered */
                suppressed = false;
            }
            last_tick = now;
            if (++osc > EDG_OSC_MAX) {
                if (!suppressed) {
                    printf("[%-7s] !! METASTABLE oscillation - output suppressed "
                           "(bring the latch up through a defined state)\n", g->name);
                }
                suppressed = true;
                emit = false;            /* break the storm */
            }
        }

        if (emit) {
            if (g->trace) {
                edg_event_t shown = o;
                if (g->invert_out) shown.value = !shown.value;  /* as actually sent */
                trace_msg(g->name, "out", shown, (int)st.d);
            }
            send_event(g->out, g->fanout, g->invert_out, o);  /* applies invert_out */
        }
    }
}

/*===========================================================================
 *  DELAY object (passive transport)
 *
 *  Forwards each received message after a fixed delay. Used to set the period
 *  of a feedback loop (a ring-oscillator clock) so it does not free-run.
 *=========================================================================*/
void edg_delay_task(void *param)
{
    edg_delay_t *dl = (edg_delay_t *)param;
    for (;;) {
        edg_event_t ev;
        xQueueReceive(dl->in, &ev, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(dl->delay_ms));
        if (dl->trace) {
            trace_msg(dl->name, "dly", ev, -1);
        }
        send_event(dl->out, dl->fanout, dl->invert_out, ev);
    }
}

/*===========================================================================
 *  OUTPUT object (actuator, passive)
 *
 *  Blocks on its input queue and drives a GPIO (an LED). It does not
 *  distinguish init from change messages -- the first message simply sets the
 *  output. It never sends anything.
 *=========================================================================*/
void edg_actuator_task(void *param)
{
    edg_actuator_t *a = (edg_actuator_t *)param;

    gpio_init(a->gpio);
    gpio_set_dir(a->gpio, GPIO_OUT);
    gpio_put(a->gpio, a->active_low ? 1 : 0);   /* start de-asserted */

    for (;;) {
        edg_event_t ev;
        receive_event(a->in, a->invert_in, &ev);

        bool on = ev.value;
        gpio_put(a->gpio, a->active_low ? !on : on);
        if (a->trace) {
            trace_msg(a->name, "LED", ev, -1);
        }
    }
}
