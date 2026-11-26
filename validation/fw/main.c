/*==============================================================================
 *  One RP2040 firmware for the concurrency study. The emulator runner patches
 *  the CFG block in the flash image to select the net, the policy, the seed,
 *  the scheduling jitter, the firing target and the queue depth.
 *
 *  Policies
 *    offer-rr      S1 library tasks, no release inputs (plain offer rule)
 *    offer-rand    same, but a place offers to a random consumer
 *    release-rr    S1 library tasks, edg_pn_auto_release (release and request)
 *    release-rand  same, random consumer
 *    grouped-rr    GitHub edg_pn_choice.c (grouped offers), one task per mailbox
 *    grouped-rand  same, the group starts its scan at a random transition
 *    central-lru   one token-player task, least recently fired enabled transition
 *    central-rand  one token-player task, random enabled transition
 *
 *  Trace: "F <t>" at every firing decision (library: on_fire, before the
 *  withdrawals; grouped: when the group posts GRANT; central: in the player).
 *============================================================================*/
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "pico/stdlib.h"

#include "edg.h"
#include "edg_pn.h"
#include "edg_pn_choice.h"
#include "nets.h"
#include "nets_extra.h"

#define PRIO  (tskIDLE_PRIORITY + 1)
#define STACK 512

/* A/B regression switch: only the test adapter's initialization is changed. */
#ifndef DEFER_GROUP_INIT
#define DEFER_GROUP_INIT 1
#endif

enum { OFFER_RR, OFFER_RAND, REL_RR, REL_RAND, GRP_RR, GRP_RAND, CEN_RR, CEN_RAND, N_POL };
static const char *const POL_NAME[N_POL] = {
    "offer-rr", "offer-rand", "release-rr", "release-rand",
    "grouped-rr", "grouped-rand", "central-lru", "central-rand"
};

typedef struct {
    uint32_t magic0, net, policy, seed, jitter, target, qdepth, magic1;
} cfg_t;
__attribute__((used)) static volatile const cfg_t CFG = {
    0x50E7C0F1u, 0, 0, 1, 1, 2000, 16, 0x50E7C0F2u
};

static const net_t *N;
static unsigned policy, jitter_on, target, qdepth;
static uint32_t rng = 1;

static uint32_t rnd(void)
{
    taskENTER_CRITICAL();
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x;
    taskEXIT_CRITICAL();
    return x;
}

static void jitter(void)
{
    if (!jitter_on) return;
    for (uint32_t n = rnd() % 4u; n; --n) taskYIELD();
}

/*--- logging ---------------------------------------------------------------*/
static SemaphoreHandle_t log_mx;
static volatile uint32_t fired, qmax;
static volatile bool done;

static void logmsg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    xSemaphoreTake(log_mx, portMAX_DELAY);
    vprintf(fmt, ap);
    xSemaphoreGive(log_mx);
    va_end(ap);
}

static void log_fire(unsigned t)
{
    xSemaphoreTake(log_mx, portMAX_DELAY);
    if (!done) {
        printf("F %u\n", t);
        if (++fired >= target) {
            done = true;
            printf("END fired=%lu qmax=%lu\n", (unsigned long)fired, (unsigned long)qmax);
        }
    }
    xSemaphoreGive(log_mx);
}

/*--- S1 library policies ---------------------------------------------------*/
static edg_pn_place_t P[MAXP];
static edg_pn_transition_t T[MAXT];
static QueueHandle_t qp[MAXP], qt[MAXT];

static const edg_event_t EV_OFFER   = { .value = true, .is_init = false };
static const edg_event_t EV_OFFER_R = { .value = true, .is_init = true  };

static void lib_post(QueueHandle_t q, edg_event_t ev)
{
    xQueueSendToBack(q, &ev, portMAX_DELAY);
    UBaseType_t w = uxQueueMessagesWaiting(q);
    if (w > qmax) qmax = w;
}

static void on_fire(const edg_pn_transition_t *t)
{
    log_fire((unsigned)(uintptr_t)t->ctx);
    jitter();
}

/* edg_pn_place_task of the library, except for the random consumer choice. */
static void rand_place_task(void *param)
{
    edg_pn_place_t *p = param;
    edg_pn_place_state_t s = EDG_PN_PLACE_INIT;
    for (;;) {
        edg_event_t ev;
        xQueueReceive(p->in, &ev, portMAX_DELAY);
        jitter();
        if (p->n_consumers) s.next = (uint8_t)(rnd() % p->n_consumers);
        edg_pn_place_out_t o;
        edg_pn_place_step(&s, p->n_consumers, ev, &o);
        if (o.anomaly) logmsg("!! anomaly at %s\n", p->name);
        if (o.offer) {
            bool r = (p->release_mask >> o.offer_to) & 1u;
            lib_post(p->consumer[o.offer_to], r ? EV_OFFER_R : EV_OFFER);
        }
    }
}

static void setup_lib(bool release, bool random)
{
    for (int p = 0; p < N->np; ++p) qp[p] = xQueueCreate(qdepth, sizeof(edg_event_t));
    for (int t = 0; t < N->nt; ++t) qt[t] = xQueueCreate(qdepth, sizeof(edg_event_t));
    for (int p = 0; p < N->np; ++p) {
        P[p] = (edg_pn_place_t){ .name = N->pname[p], .in = qp[p] };
        for (int t = 0; t < N->nt; ++t)
            for (int k = 0; k < N->npre[t]; ++k)
                if (N->pre[t][k] == p) P[p].consumer[P[p].n_consumers++] = qt[t];
    }
    for (int t = 0; t < N->nt; ++t) {
        T[t] = (edg_pn_transition_t){ .name = N->tname[t], .in = qt[t],
                                      .on_fire = on_fire, .ctx = (void *)(uintptr_t)t };
        for (int k = 0; k < N->npre[t]; ++k) T[t].pre[T[t].n_pre++] = qp[N->pre[t][k]];
        for (int k = 0; k < N->npost[t]; ++k) T[t].post[T[t].n_post++] = qp[N->post[t][k]];
    }
    if (release)
        printf("REL keep-every-offer=%u\n", (unsigned)edg_pn_auto_release(P, N->np, T, N->nt));
    for (int p = 0; p < N->np; ++p) edg_pn_mark(qp[p], (unsigned)N->m0[p]);
    for (int p = 0; p < N->np; ++p)
        xTaskCreate(random ? rand_place_task : edg_pn_place_task, N->pname[p], STACK, &P[p], PRIO, NULL);
    for (int t = 0; t < N->nt; ++t)
        xTaskCreate(edg_pn_transition_task, N->tname[t], STACK, &T[t], PRIO, NULL);
}

/*--- grouped offers (GitHub module) ----------------------------------------*/
static edg_choice_net_t CN;
static QueueHandle_t box[3 * EDG_CHOICE_MAX];
static bool started, grp_random;

static void cpost(void *ctx, edg_choice_msg_t m)
{
    (void)ctx;
    int b = edg_choice_mailbox(&CN, m);
    if (b < 0 || !box[b]) {
        logmsg("!! route kind=%d id=%u\n", (int)m.kind, m.id);
        for (;;) vTaskDelay(1000);
    }
    if (m.kind == EDG_CHOICE_GRANT) log_fire(m.id);
    if (xQueueSendToBack(box[b], &m, started ? pdMS_TO_TICKS(3000) : 0) != pdPASS) {
        logmsg("QFULL box=%d started=%u kind=%u id=%u\n",
               b, (unsigned)started, (unsigned)m.kind, m.id);
        xQueueSendToBack(box[b], &m, portMAX_DELAY);
    }
    UBaseType_t w = uxQueueMessagesWaiting(box[b]);
    if (w > qmax) qmax = w;
}

static void cworker(void *arg)
{
    int b = (int)(intptr_t)arg;
    for (;;) {
        edg_choice_msg_t m;
        xQueueReceive(box[b], &m, portMAX_DELAY);
        jitter();
        if (grp_random && b >= 2 * EDG_CHOICE_MAX)
            CN.group[b - 2 * EDG_CHOICE_MAX].next = rnd() % CN.transitions;
        if (!edg_choice_step(&CN, m, cpost, NULL))
            logmsg("!! choice error kind=%d id=%u\n", (int)m.kind, m.id);
    }
}

static void make_box(int b, const char *name)
{
    box[b] = xQueueCreate(qdepth, sizeof(edg_choice_msg_t));
    xTaskCreate(cworker, name, STACK, (void *)(intptr_t)b, PRIO, NULL);
}

#if DEFER_GROUP_INIT
static void grouped_init_task(void *unused)
{
    (void)unused;
    started = true; /* This initializer runs only after FreeRTOS starts. */
    logmsg("INIT_BEGIN\n");
    /* Initialize every mailbox before any token can cause cross-place output.
     * Per-mailbox FIFO keeps each INIT ahead of every subsequent DEPOSIT. */
    for (int p = 0; p < N->np; ++p)
        cpost(NULL, (edg_choice_msg_t){ EDG_CHOICE_INIT, (unsigned)p });
    for (int p = 0; p < N->np; ++p)
        for (int k = 0; k < N->m0[p]; ++k)
            cpost(NULL, (edg_choice_msg_t){ EDG_CHOICE_DEPOSIT, (unsigned)p });
    logmsg("INIT_READY\n");
    vTaskDelete(NULL);
}
#endif

static void setup_grouped(bool random)
{
    edg_choice_arcs_t arcs[EDG_CHOICE_MAX] = {0};
    for (int t = 0; t < N->nt; ++t) {
        for (int k = 0; k < N->npre[t]; ++k) arcs[t].inputs |= EDG_CHOICE_BIT(N->pre[t][k]);
        for (int k = 0; k < N->npost[t]; ++k) arcs[t].outputs |= EDG_CHOICE_BIT(N->post[t][k]);
    }
    if (!edg_choice_init(&CN, (unsigned)N->np, arcs, (unsigned)N->nt)) {
        printf("!! edg_choice_init rejected the net\n");
        return;
    }
    grp_random = random;
    for (int p = 0; p < N->np; ++p) make_box(p, N->pname[p]);
    for (int t = 0; t < N->nt; ++t) make_box(EDG_CHOICE_MAX + t, N->tname[t]);
    unsigned groups = 0;
    for (int t = 0; t < N->nt; ++t) {
        int b = 2 * EDG_CHOICE_MAX + (int)CN.group_of_transition[t];
        if (!box[b]) { make_box(b, "group"); ++groups; }
    }
    printf("GROUPS %u\n", groups);
#if DEFER_GROUP_INIT
    if (xTaskCreate(grouped_init_task, "init", STACK, NULL, PRIO, NULL) != pdPASS)
        panic("startup task allocation failed");
#else
    /* Original adapter, retained only to reproduce the startup failure. */
    for (int p = 0; p < N->np; ++p) {
        cpost(NULL, (edg_choice_msg_t){ EDG_CHOICE_INIT, (unsigned)p });
        for (int k = 0; k < N->m0[p]; ++k)
            cpost(NULL, (edg_choice_msg_t){ EDG_CHOICE_DEPOSIT, (unsigned)p });
    }
    started = true;
#endif
}

/*--- central token player (baseline) ---------------------------------------*/
static void central_task(void *arg)
{
    bool random = (bool)(intptr_t)arg;
    int M[MAXP];
    uint32_t last[MAXT] = {0}, step = 0;
    for (int p = 0; p < N->np; ++p) M[p] = N->m0[p];
    for (;;) {
        unsigned en[MAXT], ne = 0;
        for (int t = 0; t < N->nt; ++t) {
            bool ok = true;
            for (int k = 0; k < N->npre[t]; ++k) if (M[N->pre[t][k]] <= 0) ok = false;
            if (ok) en[ne++] = (unsigned)t;
        }
        if (!ne) { logmsg("DEAD central\n"); vTaskSuspend(NULL); }
        unsigned pick = en[0];
        if (random) {
            pick = en[rnd() % ne];
        } else {
            /* least recently fired enabled transition (ties: lowest index) */
            for (unsigned i = 1; i < ne; ++i) if (last[en[i]] < last[pick]) pick = en[i];
        }
        last[pick] = ++step;
        for (int k = 0; k < N->npre[pick]; ++k) M[N->pre[pick][k]]--;
        for (int k = 0; k < N->npost[pick]; ++k) M[N->post[pick][k]]++;
        log_fire(pick);
        jitter();
        taskYIELD();
    }
}

int main(void)
{
    stdio_init_all();
    unsigned net = CFG.net;
    policy = CFG.policy; jitter_on = CFG.jitter; target = CFG.target; qdepth = CFG.qdepth;
    rng = CFG.seed ? CFG.seed : 1;
    N = net < (unsigned)N_NETS ? &NETS[net]
      : net < (unsigned)(N_NETS + N_EXTRA) ? &EXTRA[net - N_NETS] : NULL;
    if (!N || policy >= N_POL) { printf("NONET\n"); for (;;) tight_loop_contents(); }

    printf("NET %s np=%d nt=%d POL %s SEED %lu JIT %u Q %u\n", N->name, N->np, N->nt,
           POL_NAME[policy], (unsigned long)rng, jitter_on, qdepth);
    for (int t = 0; t < N->nt; ++t) {
        printf("T %d %s pre", t, N->tname[t]);
        for (int k = 0; k < N->npre[t]; ++k) printf(" %d", N->pre[t][k]);
        printf(" post");
        for (int k = 0; k < N->npost[t]; ++k) printf(" %d", N->post[t][k]);
        printf("\n");
    }
    printf("M0");
    for (int p = 0; p < N->np; ++p) printf(" %d", N->m0[p]);
    printf("\n");

    log_mx = xSemaphoreCreateMutex();
    switch (policy) {
    case OFFER_RR:   setup_lib(false, false); break;
    case OFFER_RAND: setup_lib(false, true);  break;
    case REL_RR:     setup_lib(true, false);  break;
    case REL_RAND:   setup_lib(true, true);   break;
    case GRP_RR:     setup_grouped(false);    break;
    case GRP_RAND:   setup_grouped(true);     break;
    default:
        xTaskCreate(central_task, "central", STACK, (void *)(intptr_t)(policy == CEN_RAND), PRIO, NULL);
    }
    printf("HEAP free=%u\nSTART\n", (unsigned)xPortGetFreeHeapSize());
    vTaskStartScheduler();
    for (;;) {}
}
