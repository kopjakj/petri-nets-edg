/* Host verification of the 1-bit ALU network (uses real edg_gate_step). */
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

#define NG 32
typedef struct { edg_kind_t kind; uint32_t sym; bool invin, invout; edg_state_t st; int nt; int tgt[8]; bool out; } Gate;
static Gate G[NG]; static int ng=0;
typedef struct { int g; edg_event_t ev; } Ev;
static Ev fifo[300000]; static int head=0, tail=0; static long processed=0;
static void push(int g, edg_event_t e){ fifo[tail].g=g; fifo[tail].ev=e; tail++; }
static int add(edg_kind_t k,uint32_t sym,bool ii,bool io){ Gate*g=&G[ng]; g->kind=k;g->sym=sym;g->invin=ii;g->invout=io;g->st=EDG_STATE_INIT;g->nt=0; return ng++; }
static void link(int a,int b){ G[a].tgt[G[a].nt++]=b; }
static int run(void){ while(head<tail){ if(processed++>250000) return 1; Ev e=fifo[head++]; Gate*g=&G[e.g];
  edg_event_t in=e.ev; if(g->invin) in.value=!in.value; edg_event_t o;
  if(edg_gate_step(g->kind,g->sym,&g->st,in,&o)){ edg_event_t s=o; if(g->invout)s.value=!s.value; g->out=s.value;
    for(int i=0;i<g->nt;i++) push(g->tgt[i],s);} } return 0; }
static void inj(int g,int v){ edg_event_t e={v,true}; push(g,e); }

/* AND=level invin=true; OR=level invin=false invout=true; NOT=level; PAR=parity; SYM */
#define AND()  add(EDG_LEVEL,0,true,false)
#define OR_()  add(EDG_LEVEL,0,false,true)
#define NOT_() add(EDG_LEVEL,0,false,false)
#define PAR()  add(EDG_PARITY,0,false,false)

int main(void){
    uint8_t maj[2]={2,3};
    int fails=0;
    printf("A B Cin S1 S0 | RES CAR  exp_res exp_car op\n");
    for(int m=0;m<32;m++){
        int A=(m>>4)&1,B=(m>>3)&1,Ci=(m>>2)&1,S1=(m>>1)&1,S0=m&1;
        memset(G,0,sizeof G); ng=0; head=tail=0; processed=0;
        int op_and=AND(), op_or=OR_(), op_xor=PAR(), op_sum=PAR(), op_carry=add(EDG_SYMMETRIC,edg_sym_mask(maj,2),false,false);
        int nS0=NOT_(), nS1=NOT_();
        int m0=AND(), m1=AND(), m2=AND(), m3=AND();
        int add_mode=AND(); /* AND(S1,S0) */
        int RES=OR_(); int COUT=AND();
        /* op wiring */
        link(op_and,m0); link(op_or,m1); link(op_xor,m2); link(op_sum,m3);
        link(nS0,m0); link(nS0,m2); link(nS1,m0); link(nS1,m1);
        /* selects already injected to m1,m2,m3,add_mode as true literals */
        link(m0,RES); link(m1,RES); link(m2,RES); link(m3,RES);
        link(op_carry,COUT); link(add_mode,COUT);
        /* inject inputs (init) to all consumers */
        inj(op_and,A); inj(op_or,A); inj(op_xor,A); inj(op_sum,A); inj(op_carry,A);
        inj(op_and,B); inj(op_or,B); inj(op_xor,B); inj(op_sum,B); inj(op_carry,B);
        inj(op_sum,Ci); inj(op_carry,Ci);
        inj(nS0,S0); inj(m1,S0); inj(m3,S0); inj(add_mode,S0);
        inj(nS1,S1); inj(m2,S1); inj(m3,S1); inj(add_mode,S1);
        if(run()){ printf("STORM at m=%d\n",m); fails++; continue; }
        int res=G[RES].out, car=G[COUT].out;
        int er,ec=0; const char*op;
        int sel=S1*2+S0;
        if(sel==0){er=A&B; op="AND";}
        else if(sel==1){er=A|B; op="OR ";}
        else if(sel==2){er=A^B; op="XOR";}
        else {int s=A+B+Ci; er=s&1; ec=(s>>1)&1; op="ADD";}
        int okr=(res==er), okc=(car==ec);
        if(!okr||!okc){fails++; printf("%d %d  %d   %d  %d |  %d   %d    %d      %d   %s  <-- FAIL\n",A,B,Ci,S1,S0,res,car,er,ec,op);}
        else printf("%d %d  %d   %d  %d |  %d   %d    %d      %d   %s\n",A,B,Ci,S1,S0,res,car,er,ec,op);
    }
    printf("\n%s (%d failures)\n", fails?"ALU FAILED":"ALU ALL PASS", fails);
    return fails?1:0;
}
