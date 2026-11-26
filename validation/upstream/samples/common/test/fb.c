/* Host simulator for FEEDBACK networks, using the real edg_gate_step().
 * Models a deterministic FIFO scheduler (one particular task ordering) and
 * caps total events to detect non-convergence (message storms). */
#include <stdio.h>
#include <string.h>
#include "edg.h"

/* stubs so we can link edg.c (its task/gpio symbols are unused here) */
QueueHandle_t xQueueCreate(UBaseType_t l,UBaseType_t i){(void)l;(void)i;return (void*)1;}
BaseType_t xQueueSendToBack(QueueHandle_t q,const void*it,TickType_t t){(void)q;(void)it;(void)t;return 1;}
BaseType_t xQueueReceive(QueueHandle_t q,void*b,TickType_t t){(void)q;(void)b;(void)t;return 1;}
TickType_t xTaskGetTickCount(void){return 0;}
void vTaskDelayUntil(TickType_t*p,TickType_t i){(void)p;(void)i;}
void vTaskDelay(TickType_t i){(void)i;}
BaseType_t xTaskCreate(void(*f)(void*),const char*n,uint32_t s,void*p,UBaseType_t pr,TaskHandle_t*h){(void)f;(void)n;(void)s;(void)p;(void)pr;(void)h;return 1;}
void vTaskStartScheduler(void){}
void gpio_init(uint32_t g){(void)g;} void gpio_set_dir(uint32_t g,int d){(void)g;(void)d;}
void gpio_pull_up(uint32_t g){(void)g;} void gpio_pull_down(uint32_t g){(void)g;}
int gpio_get(uint32_t g){(void)g;return 0;} void gpio_put(uint32_t g,int v){(void)g;(void)v;}
void stdio_init_all(void){} void sleep_ms(uint32_t m){(void)m;}

#define NG 8
typedef struct { const char*name; edg_kind_t kind; uint32_t sym; bool invin, invout;
                 edg_state_t st; int nt; int tgt[NG]; bool haveout; bool out; } Gate;
static Gate G[NG]; static int ngates=0;

typedef struct { int g; edg_event_t ev; } Ev;
static Ev fifo[100000]; static int head=0, tail=0;
static long processed=0;
static void push(int g, edg_event_t ev){ fifo[tail].g=g; fifo[tail].ev=ev; tail++; }

static int addgate(const char*n, edg_kind_t k, uint32_t sym, bool invin, bool invout){
    Gate*g=&G[ngates]; g->name=n; g->kind=k; g->sym=sym; g->invin=invin; g->invout=invout;
    g->st=EDG_STATE_INIT; g->nt=0; g->haveout=false; return ngates++;
}
static void link(int from,int to){ G[from].tgt[G[from].nt++]=to; }

/* returns 0 if converged (fifo drained), 1 if storm */
static int run(long cap){
    while(head<tail){
        if(processed++>cap) return 1;
        Ev e=fifo[head++];
        Gate*g=&G[e.g];
        edg_event_t in=e.ev; if(g->invin) in.value=!in.value;
        edg_event_t out;
        if(edg_gate_step(g->kind,g->sym,&g->st,in,&out)){
            edg_event_t sent=out; if(g->invout) sent.value=!sent.value;
            g->haveout=true; g->out=sent.value;
            for(int i=0;i<g->nt;i++) push(g->tgt[i], sent);
        }
    }
    return 0;
}
static void show(const char*tag){
    printf("  %-18s: ", tag);
    for(int i=0;i<ngates;i++) printf("%s=%d ", G[i].name, G[i].haveout?G[i].out:-1);
    printf("(events=%ld)\n", processed);
}
static void inject(int g, int val, bool init){ edg_event_t e={val,init}; push(g,e); }

int main(void){
    printf("=== SR latch (cross-coupled NAND) ===\n");
    /* NAND config from edg_set_logic(EDG_NAND): invin=true, invout=true */
    int N1=addgate("Q",  EDG_LEVEL,0,true,true);   /* NAND1 -> Q     */
    int N2=addgate("nQ", EDG_LEVEL,0,true,true);   /* NAND2 -> /Q    */
    link(N1,N2); link(N2,N1);                       /* cross-couple  */

    /* Power-up: /S line=1 (released), /R line=0 (RESET asserted) -> defined. */
    processed=0; head=tail=0;
    inject(N1, 1, true);   /* /S init = 1 */
    inject(N2, 0, true);   /* /R init = 0 (reset) */
    int storm=run(200000); show(storm?"POWER-UP STORM":"power-up (/R asserted)");

    /* release /R (both released -> HOLD). Expect latch holds RESET: Q=0 */
    inject(N2, 1, false); run(200000); show("release /R (hold)");

    /* assert /S (set). Expect Q=1 */
    inject(N1, 0, false); run(200000); show("assert /S (set)");
    inject(N1, 1, false); run(200000); show("release /S (hold)");

    /* assert /R (reset). Expect Q=0 */
    inject(N2, 0, false); run(200000); show("assert /R (reset)");
    inject(N2, 1, false); run(200000); show("release /R (hold)");

    /* ---- cold start BOTH released (the indeterminate/forbidden hold) ---- */
    printf("\n=== SR latch cold start with BOTH released (metastable test) ===\n");
    memset(G,0,sizeof G); ngates=0; processed=0; head=tail=0;
    N1=addgate("Q",EDG_LEVEL,0,true,true); N2=addgate("nQ",EDG_LEVEL,0,true,true);
    link(N1,N2); link(N2,N1);
    inject(N1,1,true); inject(N2,1,true);
    storm=run(200000); show(storm?"COLD-START STORM (metastable)":"cold start settled");

    /* ============ Gated D latch from 4 NANDs ============ */
    printf("\n=== Gated D latch (4 NANDs) ===\n");
    memset(G,0,sizeof G); ngates=0; processed=0; head=tail=0;
    /* all EDG_NAND (invin=true, invout=true) */
    int gN1=addgate("N1",EDG_LEVEL,0,true,true);
    int gN2=addgate("N2",EDG_LEVEL,0,true,true);
    int gQ =addgate("Q", EDG_LEVEL,0,true,true);
    int gNQ=addgate("nQ",EDG_LEVEL,0,true,true);
    link(gN1,gN2); link(gN1,gQ);   /* N1 -> N2, Q  */
    link(gN2,gNQ);                  /* N2 -> /Q     */
    link(gQ,gNQ);  link(gNQ,gQ);    /* Q<->/Q cross */
    /* Bring up with E=1 so the latch is defined (transparent). D=1 -> Q=1. */
    inject(gN1,1,true);  /* D=1 -> N1 */
    inject(gN1,1,true);  /* E=1 -> N1 */
    inject(gN2,1,true);  /* E=1 -> N2 */
    storm=run(200000); show(storm?"E=1,D=1 STORM":"E=1 D=1 (transparent)");
    /* change D to 0 while E=1 -> Q follows to 0 */
    inject(gN1,0,false); /* D=0 */  run(200000); show("E=1 D=0 (Q follows)");
    /* set D=1 again */
    inject(gN1,1,false); run(200000); show("E=1 D=1 (Q follows)");
    /* now disable: E=0 -> HOLD (Q stays 1 even if D changes) */
    inject(gN1,0,false); /* E=0 -> N1 */
    inject(gN2,0,false); /* E=0 -> N2 */
    run(200000); show("E=0 (latch closed)");
    inject(gN1,0,false); /* D=0 while disabled (through N1) */
    run(200000); show("E=0 D=0 (HOLD: Q unchanged)");

    /* ============ 2-to-4 decoder ============ */
    printf("\n=== 2-to-4 decoder (expect one-hot) ===\n");
    for (int a1=0;a1<2;a1++) for (int a0=0;a0<2;a0++){
        memset(G,0,sizeof G); ngates=0; processed=0; head=tail=0;
        int inA0=addgate("nA0",EDG_LEVEL,0,false,false); /* EDG_NOT */
        int inA1=addgate("nA1",EDG_LEVEL,0,false,false);
        int Y0=addgate("Y0",EDG_LEVEL,0,true,false);     /* EDG_AND */
        int Y1=addgate("Y1",EDG_LEVEL,0,true,false);
        int Y2=addgate("Y2",EDG_LEVEL,0,true,false);
        int Y3=addgate("Y3",EDG_LEVEL,0,true,false);
        link(inA0,Y0); link(inA0,Y2);   /* !A0 -> Y0,Y2 */
        link(inA1,Y0); link(inA1,Y1);   /* !A1 -> Y0,Y1 */
        /* inject A0 to {nA0,Y1,Y3}, A1 to {nA1,Y2,Y3} */
        inject(inA0,a0,true); inject(Y1,a0,true); inject(Y3,a0,true);
        inject(inA1,a1,true); inject(Y2,a1,true); inject(Y3,a1,true);
        run(200000);
        char tag[32]; snprintf(tag,sizeof tag,"A1=%d A0=%d",a1,a0);
        printf("  %-9s: Y0=%d Y1=%d Y2=%d Y3=%d  (expect Y%d)\n",
               tag, G[Y0].out,G[Y1].out,G[Y2].out,G[Y3].out, a1*2+a0);
    }
    return 0;
}
