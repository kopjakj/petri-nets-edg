/* Host unit test of the REAL edg.c gate logic (edg_gate_step, edg_set_logic,
 * edg_sym_mask). Stubs satisfy the FreeRTOS/pico symbols edg.c references but
 * that the gate step does not use. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edg.h"

/* --- stub bodies (never actually called by these tests) --- */
QueueHandle_t xQueueCreate(UBaseType_t l, UBaseType_t i){(void)l;(void)i;return (void*)1;}
BaseType_t xQueueSendToBack(QueueHandle_t q,const void*it,TickType_t t){(void)q;(void)it;(void)t;return 1;}
BaseType_t xQueueReceive(QueueHandle_t q,void*b,TickType_t t){(void)q;(void)b;(void)t;return 1;}
TickType_t xTaskGetTickCount(void){return 0;}
void vTaskDelayUntil(TickType_t*p,TickType_t i){(void)p;(void)i;}
BaseType_t xTaskCreate(void(*f)(void*),const char*n,uint32_t s,void*p,UBaseType_t pr,TaskHandle_t*h){(void)f;(void)n;(void)s;(void)p;(void)pr;(void)h;return 1;}
void vTaskStartScheduler(void){}
void vTaskDelay(TickType_t i){(void)i;}
void gpio_init(uint32_t g){(void)g;} void gpio_set_dir(uint32_t g,int d){(void)g;(void)d;}
void gpio_pull_up(uint32_t g){(void)g;} void gpio_pull_down(uint32_t g){(void)g;}
int gpio_get(uint32_t g){(void)g;return 0;} void gpio_put(uint32_t g,int v){(void)g;(void)v;}
void stdio_init_all(void){} void sleep_ms(uint32_t m){(void)m;}

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("  FAIL: %s\n", msg); failures++; } } while(0)

/* Drive a gate with a full set of inputs given as an array of initial bools,
 * simulating: each input sends its init value, then we can flip inputs.
 * Returns the gate's final output value AS SENT (post output-inversion). */
typedef struct { edg_kind_t kind; uint32_t sym; bool inv_in, inv_out; } gcfg_t;

/* Apply one physical input event to the gate (handles invert_in like the task).
 * Returns true and sets *sent (post invert_out) if the gate emitted. */
static bool feed(gcfg_t c, edg_state_t *st, bool phys_value, bool is_init, bool *sent)
{
    edg_event_t in = { .value = c.inv_in ? !phys_value : phys_value, .is_init = is_init };
    edg_event_t out;
    bool emit = edg_gate_step(c.kind, c.sym, st, in, &out);
    if (emit && sent) *sent = c.inv_out ? !out.value : out.value;
    return emit;
}

/* Bring a gate up with N inputs all at their init values, then return the
 * final emitted output value (post-inversion). Then optionally toggle inputs. */
static bool eval_gate(gcfg_t c, const bool *inputs, int n)
{
    edg_state_t st = EDG_STATE_INIT;
    bool sent = false, any = false, s;
    /* init wave: every input announces its starting value once */
    for (int i = 0; i < n; i++) {
        if (feed(c, &st, inputs[i], /*init*/true, &s)) { sent = s; any = true; }
    }
    (void)any;
    return sent;
}

static gcfg_t logic_cfg(edg_logic_t t) {
    edg_gate_t g; edg_set_logic(&g, t);
    gcfg_t c = { g.kind, g.sym_mask, g.invert_in, g.invert_out };
    return c;
}

/* exhaustive truth-table check for a 2-input level gate */
static void test_level(const char*name, edg_logic_t t, int truth[4]) {
    gcfg_t c = logic_cfg(t);
    printf("%s:\n", name);
    for (int a=0;a<2;a++) for (int b=0;b<2;b++) {
        bool in[2] = {a,b};
        bool got = eval_gate(c, in, 2);
        char m[64]; snprintf(m,sizeof m,"%s(%d,%d) expected %d got %d",name,a,b,truth[a*2+b],got);
        CHECK(got == truth[a*2+b], m);
    }
}

int main(void) {
    /* AND, OR, NAND, NOR truth tables indexed [a*2+b], order (0,0)(0,1)(1,0)(1,1) */
    int AND[4]  = {0,0,0,1};
    int OR[4]   = {0,1,1,1};
    int NAND[4] = {1,1,1,0};
    int NOR[4]  = {1,0,0,0};
    test_level("AND",  EDG_AND,  AND);
    test_level("OR",   EDG_OR,   OR);
    test_level("NAND", EDG_NAND, NAND);
    test_level("NOR",  EDG_NOR,  NOR);

    /* NOT / BUF (1 input) */
    { gcfg_t c = logic_cfg(EDG_NOT);
      bool i0[1]={0}, i1[1]={1};
      CHECK(eval_gate(c,i0,1)==1, "NOT(0)=1");
      CHECK(eval_gate(c,i1,1)==0, "NOT(1)=0");
      printf("NOT: checked\n"); }
    { gcfg_t c = logic_cfg(EDG_BUF);
      bool i0[1]={0}, i1[1]={1};
      CHECK(eval_gate(c,i0,1)==0, "BUF(0)=0");
      CHECK(eval_gate(c,i1,1)==1, "BUF(1)=1");
      printf("BUF: checked\n"); }

    /* XOR / parity over 2 and 3 inputs (odd number of 1s -> 1) */
    { gcfg_t c = { EDG_PARITY, 0, false, false };
      printf("XOR/parity:\n");
      for (int n=2;n<=3;n++) for (int m=0;m<(1<<n);m++) {
        bool in[3]; int ones=0;
        for(int i=0;i<n;i++){in[i]=(m>>i)&1; ones+=in[i];}
        bool got = eval_gate(c,in,n);
        char msg[64]; snprintf(msg,sizeof msg,"XOR n=%d mask=%d expected %d got %d",n,m,ones&1,got);
        CHECK(got==(ones&1), msg);
      } }

    /* XNOR = parity with output inverted */
    { gcfg_t c = { EDG_PARITY, 0, false, true };
      for (int m=0;m<4;m++){ bool in[2]={m&1,(m>>1)&1}; int ones=in[0]+in[1];
        bool got=eval_gate(c,in,2); char msg[64];
        snprintf(msg,sizeof msg,"XNOR(%d,%d) expected %d got %d",in[0],in[1],!(ones&1),got);
        CHECK(got==!(ones&1),msg);} printf("XNOR: checked\n"); }

    /* Majority(3) = symmetric with d in {2,3} (AND config: dominant=false so
     * d counts 0-inputs; but our majority uses raw counter counting 1s -> use
     * a symmetric gate that counts TRUE inputs, mask over number-of-trues). */
    { uint8_t ones_true[2] = {2,3};
      gcfg_t c = { EDG_SYMMETRIC, edg_sym_mask(ones_true,2), false, false };
      printf("MAJORITY(3):\n");
      for (int m=0;m<8;m++){ bool in[3]; int ones=0;
        for(int i=0;i<3;i++){in[i]=(m>>i)&1;ones+=in[i];}
        bool got=eval_gate(c,in,3); int exp=(ones>=2);
        char msg[80]; snprintf(msg,sizeof msg,"MAJ(%d%d%d) ones=%d expected %d got %d",in[0],in[1],in[2],ones,exp,got);
        CHECK(got==exp,msg);} }

    /* Dynamic toggling: AND gate, bring up 2 inputs true (out=1), flip one to
     * false (out->0), flip back (out->1). Verifies change-notification path. */
    { gcfg_t c = logic_cfg(EDG_AND); edg_state_t st=EDG_STATE_INIT; bool s=0;
      feed(c,&st,true,true,&s);  feed(c,&st,true,true,&s);   /* both true init */
      CHECK(s==1,"AND init both-true -> 1");
      bool e = feed(c,&st,false,false,&s); CHECK(e && s==0,"AND flip a->0 => out 0");
      e = feed(c,&st,true,false,&s);       CHECK(e && s==1,"AND flip a->1 => out 1");
      printf("AND dynamic toggle: checked\n"); }

    printf("\n%s (%d failures)\n", failures? "TESTS FAILED":"ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
