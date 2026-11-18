/* Host verification of the safety-interlock network (seal-in RUN latch). */
#include <stdio.h>
#include <string.h>
#include "edg.h"
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

#define NG 16
typedef struct { const char*nm; edg_kind_t kind; uint32_t sym; bool ii,io; edg_state_t st; int nt; int tgt[8]; bool haveout,out; } Gate;
static Gate G[NG]; static int ng=0;
typedef struct { int g; edg_event_t ev; } Ev;
static Ev fifo[200000]; static int head=0,tail=0; static long proc=0;
static void push(int g,edg_event_t e){fifo[tail].g=g;fifo[tail].ev=e;tail++;}
static int A(const char*n,edg_kind_t k,uint32_t s,bool ii,bool io){Gate*g=&G[ng];g->nm=n;g->kind=k;g->sym=s;g->ii=ii;g->io=io;g->st=EDG_STATE_INIT;g->nt=0;g->haveout=false;return ng++;}
static void L(int a,int b){G[a].tgt[G[a].nt++]=b;}
static int run(void){while(head<tail){if(proc++>150000)return 1;Ev e=fifo[head++];Gate*g=&G[e.g];
 edg_event_t in=e.ev; if(g->ii)in.value=!in.value; edg_event_t o;
 if(edg_gate_step(g->kind,g->sym,&g->st,in,&o)){edg_event_t s=o;if(g->io)s.value=!s.value;g->haveout=true;g->out=s.value;
   for(int i=0;i<g->nt;i++)push(g->tgt[i],s);}}return 0;}
#define AND(n) A(n,EDG_LEVEL,0,true,false)
#define OR(n)  A(n,EDG_LEVEL,0,false,true)
#define NOT(n) A(n,EDG_LEVEL,0,false,false)

/* gate indices */
static int nE,nT,nS,PERMIT,ALARM,OR1,RUN;
/* input->gate injection helpers: buttons feed specific gate queues */
static int gGUARD, gESTOP, gTEMP, gSTOP, gSTART;
static void build(void){
    memset(G,0,sizeof G); ng=0;
    nE=NOT("!ESTOP"); nT=NOT("!TEMP"); nS=NOT("!STOP");
    PERMIT=AND("PERMIT"); ALARM=NOT("ALARM"); OR1=OR("OR1"); RUN=AND("RUN");
    /* which gate each button feeds */
    gGUARD=PERMIT; gESTOP=nE; gTEMP=nT; gSTOP=nS; gSTART=OR1;
    L(nE,PERMIT); L(nT,PERMIT);
    L(PERMIT,RUN); L(PERMIT,ALARM);
    L(OR1,RUN); L(nS,RUN);
    L(RUN,OR1);   /* seal-in feedback */
}
static int guard,estop,temp,stopb,startb;
static void drive(int g,int v,bool init){edg_event_t e={v,init};push(g,e);}
static void snap(const char*tag){
    printf("  %-26s RUN=%d ALARM=%d PERMIT=%d\n", tag, G[RUN].haveout?G[RUN].out:-1,
           G[ALARM].haveout?G[ALARM].out:-1, G[PERMIT].haveout?G[PERMIT].out:-1);
}
int main(void){
    build(); head=tail=0; proc=0;
    /* power up: guard OPEN(0), estop released(0), temp ok(0), stop released(0), start(0) */
    guard=0;estop=0;temp=0;stopb=0;startb=0;
    drive(gGUARD,guard,true); drive(gESTOP,estop,true); drive(gTEMP,temp,true);
    drive(gSTOP,stopb,true); drive(gSTART,startb,true);
    if(run()){printf("STORM at power-up\n");return 1;} snap("power-up (guard open)");

    /* close guard */ guard=1; drive(gGUARD,1,false); run(); snap("close guard -> permit");
    /* press start */ drive(gSTART,1,false); run(); snap("press START");
    /* release start (seal-in) */ drive(gSTART,0,false); run(); snap("release START (sealed)");
    /* press stop */ drive(gSTOP,1,false); run(); snap("press STOP");
    /* release stop */ drive(gSTOP,0,false); run(); snap("release STOP");
    /* start again */ drive(gSTART,1,false); run(); drive(gSTART,0,false); run(); snap("START again (running)");
    /* e-stop trips while running */ drive(gESTOP,1,false); run(); snap("E-STOP pressed (fault)");
    /* release e-stop (must restart) */ drive(gESTOP,0,false); run(); snap("release E-STOP (permit, off)");
    /* start */ drive(gSTART,1,false); run(); drive(gSTART,0,false); run(); snap("START (running)");
    /* guard opened while running */ drive(gGUARD,0,false); run(); snap("guard OPEN while running");
    /* overtemp test: reclose guard, start, then temp */
    drive(gGUARD,1,false); run(); drive(gSTART,1,false); run(); drive(gSTART,0,false); run(); snap("reclose+START");
    drive(gTEMP,1,false); run(); snap("OVERTEMP (fault)");
    printf("\n(verify: RUN=1 only after START while permit; any fault -> RUN=0; restart required)\n");
    return 0;
}
