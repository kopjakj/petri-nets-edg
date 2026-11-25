/*==============================================================================
 *  pn_freertos.c  --  the EDG-PN TASK layer under a real FreeRTOS scheduler
 *
 *  Builds each validation net from edg_pn_place_task / edg_pn_transition_task
 *  objects (one FreeRTOS task and one FreeRTOS queue per place and per
 *  transition), runs it under the FreeRTOS POSIX/Linux port, and monitors it:
 *
 *    * every firing is checked, at the instant the transition decides to fire,
 *      against the abstract marking (M0 plus all earlier firings);
 *    * every "!!" protocol anomaly reported by the library is counted;
 *    * when nothing fires any more, the abstract marking must be dead.
 *
 *  The schedule is perturbed by pseudo-random yields before queue sends (see
 *  posix/shim.h); the seed is a command-line argument, so a run is repeatable
 *  in its inputs although not in its interleaving (the port's tick is a real
 *  timer signal).
 *
 *  The design is "offer" (version 1: every offer is kept) or "release"
 *  (version 2: release inputs configured by edg_pn_auto_release()).
 *
 *  usage: pn_freertos <net-name> <firings> [seed] [offer|release]
 *============================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "edg_pn.h"
#include "nets.h"

/* --- stubs for the Pico symbols edg.c references (unused here) ------------ */
void gpio_init(uint32_t g){(void)g;} void gpio_set_dir(uint32_t g,int d){(void)g;(void)d;}
void gpio_pull_up(uint32_t g){(void)g;} void gpio_pull_down(uint32_t g){(void)g;}
int gpio_get(uint32_t g){(void)g;return 0;} void gpio_put(uint32_t g,int v){(void)g;(void)v;}
void stdio_init_all(void){} void sleep_ms(uint32_t m){(void)m;}
void harness_assert(const char *f, int l){ fprintf(stderr,"configASSERT %s:%d\n",f,l); _exit(3); }

/* --- schedule perturbation and anomaly counting (see posix/shim.h) --------- */
static unsigned rng = 1u;
static unsigned rnd(void){ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static volatile unsigned long n_sends, n_yields, n_anomalies;

BaseType_t harness_send(QueueHandle_t q, const void *item, TickType_t t)
{
    n_sends++;
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && (rnd() & 3u) == 0u) {
        n_yields++; taskYIELD();
    }
    return xQueueGenericSend(q, item, t, queueSEND_TO_BACK);
}
int harness_printf(const char *fmt, ...)
{
    if (strstr(fmt, "!!")) { n_anomalies++; }
    (void)fmt; return 0;                       /* tracing is off in this run */
}

/* --- the monitor ------------------------------------------------------------ */
static const net_t *N;
static int  mabs[MAXP];
static volatile unsigned long n_fire, n_illegal, fire_count[MAXT];
static unsigned long target;
static const char *design = "release";
static size_t outside;

static int is_enabled(int t){ for(int i=0;i<N->npre[t];i++) if(mabs[N->pre[t][i]]<=0) return 0; return 1; }

static void on_fire(const edg_pn_transition_t *tr)
{
    int t = (int)(intptr_t)tr->ctx;
    taskENTER_CRITICAL();
    if (!is_enabled(t)) { n_illegal++; }
    for (int i=0;i<N->npre[t];i++)  mabs[N->pre[t][i]]--;
    for (int i=0;i<N->npost[t];i++) mabs[N->post[t][i]]++;
    n_fire++; fire_count[t]++;
    taskEXIT_CRITICAL();
}

/* --- the network -------------------------------------------------------------- */
static edg_pn_place_t      P[MAXP];
static edg_pn_transition_t T[MAXT];

static void supervisor(void *arg)
{
    (void)arg;
    unsigned long last = 0; int idle = 0; const char *why = "target reached";
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (n_fire >= target) break;
        if (n_fire == last) { if (++idle >= 25) { why = "no firing for 500 ms"; break; } }
        else { idle = 0; last = n_fire; }
    }
    vTaskSuspendAll();
    int dead = 1; for (int t=0;t<N->nt;t++) if (is_enabled(t)) dead = 0;
    printf("%-15s %-7s firings=%-8lu illegal=%-3lu anomalies=%-3lu sends=%-9lu yields=%-8lu stop: %s%s\n",
           N->name, design, n_fire, n_illegal, n_anomalies, n_sends, n_yields, why,
           strcmp(why,"target reached") ? (dead ? " (dead marking)" : " (STALL: marking not dead)") : "");
    printf("                per transition:");
    for (int t=0;t<N->nt;t++) printf(" %s=%lu", N->tname[t], fire_count[t]);
    printf("\n");
    fflush(stdout);
    _exit(n_illegal || n_anomalies ? 1 : 0);   /* leave at once: other tasks are still alive */
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr,"usage: %s <net> <firings> [seed] [offer|release]\n", argv[0]); return 2; }
    for (int i=0;i<N_NETS;i++) if (!strcmp(NETS[i].name, argv[1])) N = &NETS[i];
    if (!N) { fprintf(stderr,"unknown net\n"); return 2; }
    target = strtoul(argv[2], NULL, 10);
    if (argc > 3) rng = (unsigned)strtoul(argv[3], NULL, 10) | 1u;
    if (argc > 4) design = argv[4];
    if (strcmp(design, "offer") && strcmp(design, "release")) { fprintf(stderr,"unknown design\n"); return 2; }

    QueueHandle_t qp[MAXP], qt[MAXT];
    for (int p=0;p<N->np;p++) { qp[p] = edg_wire(8); configASSERT(qp[p]); mabs[p] = N->m0[p]; }
    for (int t=0;t<N->nt;t++) { qt[t] = edg_wire(8); configASSERT(qt[t]); }

    for (int p=0;p<N->np;p++) {
        P[p] = (edg_pn_place_t){ .name = N->pname[p], .in = qp[p] };
        for (int t=0;t<N->nt;t++) for (int k=0;k<N->npre[t];k++)
            if (N->pre[t][k] == p) P[p].consumer[P[p].n_consumers++] = qt[t];
    }
    for (int t=0;t<N->nt;t++) {
        T[t] = (edg_pn_transition_t){ .name = N->tname[t], .in = qt[t],
                                      .on_fire = on_fire, .ctx = (void*)(intptr_t)t };
        for (int k=0;k<N->npre[t];k++)  T[t].pre[T[t].n_pre++]   = qp[N->pre[t][k]];
        for (int k=0;k<N->npost[t];k++) T[t].post[T[t].n_post++] = qp[N->post[t][k]];
    }

    outside = edg_pn_auto_release(P, (size_t)N->np, T, (size_t)N->nt);
    if (!strcmp(design, "offer")) {                  /* version 1: keep every offer */
        for (int p=0;p<N->np;p++) P[p].release_mask = 0;
        for (int t=0;t<N->nt;t++) T[t].release_input = 0;
    }

    BaseType_t ok = pdPASS;
    for (int p=0;p<N->np;p++) ok &= xTaskCreate(edg_pn_place_task,      N->pname[p], 2048, &P[p], 1, NULL);
    for (int t=0;t<N->nt;t++) ok &= xTaskCreate(edg_pn_transition_task, N->tname[t], 2048, &T[t], 1, NULL);
    ok &= xTaskCreate(supervisor, "super", 2048, NULL, 3, NULL);
    configASSERT(ok == pdPASS);

    /* initial marking: at most 8 messages per queue, so this cannot block */
    for (int p=0;p<N->np;p++) edg_pn_mark(qp[p], (unsigned)N->m0[p]);

    vTaskStartScheduler();
    return 0;
}
