/*==============================================================================
 *  pn_explore.c  --  exhaustive interleaving exploration of EDG-PN networks
 *
 *  Drives the REAL pure cores (edg_gate_step, edg_pn_place_step,
 *  edg_pn_trans_step) under EVERY possible task interleaving and compares the
 *  result with the reference token game of the net.
 *
 *  Model of the run-time system (faithful to one task per object, one FIFO
 *  queue per object, preemptive scheduling at a single priority):
 *    * an object with pending sends performs its next send        (one step)
 *    * otherwise an object with a non-empty queue takes the head message and
 *      runs its pure core                                         (one step)
 *    * the initial-marking messages are sent in any order         (one step)
 *  Every send is a step of its own, so a task may be preempted between any
 *  two sends; every interleaving of these steps is explored (breadth first,
 *  with duplicate detection).
 *
 *  Checked in every reachable state / on every step:
 *    SOUND    every firing is enabled in the abstract marking, i.e. the
 *             marking obtained by playing all earlier firings in the net;
 *    ANOMALY  no place sees a withdrawal without a token or without an offer;
 *    STALL    a quiescent state (nothing in flight) must be a dead marking of
 *             the net, and the descriptors must equal that marking;
 *    COVER    which reachable markings / edges of the net's reachability
 *             graph are realised.
 *
 *  Three designs are compared:
 *    credit   the EDG-PN offer discipline of edg_pn.c (round-robin choice);
 *    credit*  the same, but a choice place may pick ANY consumer
 *             (to check that nothing reachable in the net is excluded);
 *    naive    the "obvious" mapping: place = OR gate broadcasting its level to
 *             all output transitions, transition = AND gate that fires on the
 *             rising edge of its output. Started from the empty net (preset)
 *             or with the ordinary eager initialization wave (eager).
 *============================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "edg_pn.h"
#include "nets.h"

#define QCAP 8                      /* queue depth of every object          */
static size_t MAX_STATES = 60000000;/* safety limit; a hit is reported      */
static bool   PRELOAD = false;      /* --preload: see main()                */

/* --- link stubs: edg.c's task/gpio symbols are unused here --------------- */
QueueHandle_t xQueueCreate(UBaseType_t l,UBaseType_t i){(void)l;(void)i;return (void*)1;}
BaseType_t xQueueSendToBack(QueueHandle_t q,const void*it,TickType_t t){(void)q;(void)it;(void)t;return 1;}
BaseType_t xQueueReceive(QueueHandle_t q,void*b,TickType_t t){(void)q;(void)b;(void)t;return 1;}
TickType_t xTaskGetTickCount(void){return 0;}
void vTaskDelay(TickType_t i){(void)i;}
void gpio_init(uint32_t g){(void)g;} void gpio_set_dir(uint32_t g,int d){(void)g;(void)d;}
void gpio_pull_up(uint32_t g){(void)g;} void gpio_pull_down(uint32_t g){(void)g;}
int gpio_get(uint32_t g){(void)g;return 0;} void gpio_put(uint32_t g,int v){(void)g;(void)v;}
void stdio_init_all(void){} void sleep_ms(uint32_t m){(void)m;}

/* --- design under test ---------------------------------------------------- */
typedef enum { CREDIT_RR, CREDIT_ANY, NAIVE_PRESET, NAIVE_EAGER } design_t;
static const char *DESIGN_NAME[] = { "credit", "credit*", "naive-preset", "naive-eager" };

/* --- the global state of the run-time system ------------------------------ */
typedef struct {
    /* place objects */
    int8_t  pd[MAXP];  uint8_t pho[MAXP], plo[MAXP];   /* edg_state_t        */
    int8_t  poff[MAXP]; uint8_t pnext[MAXP];           /* offer discipline   */
    uint8_t ppend[MAXP];   /* credit: consumer+1 ; naive: sends left         */
    uint8_t pmsg[MAXP];    /* naive: the level message being broadcast       */
    uint8_t pqn[MAXP];  uint8_t pq[MAXP][QCAP];        /* input queue        */
    /* transition objects */
    int8_t  td[MAXT];  uint8_t tho[MAXT], tlo[MAXT];   /* edg_state_t        */
    uint8_t tpend[MAXT];   /* sends left of the firing sequence              */
    uint8_t tqn[MAXT];  uint8_t tq[MAXT][QCAP];        /* input queue        */
    /* initial-marking messages not yet sent */
    uint8_t itrue[MAXP], ifalse[MAXP];
    /* the abstract marking: M0 plus the effect of all firings so far */
    int8_t  mabs[MAXP];
} state_t;

/* message encoding in a queue: bit0 = value, bit1 = is_init */
#define MSG(v,i)   ((uint8_t)(((v)?1:0) | ((i)?2:0)))
static edg_event_t dec(uint8_t m){ edg_event_t e={ .value=m&1, .is_init=(m>>1)&1 }; return e; }

/* --- net-derived tables ---------------------------------------------------- */
static const net_t *N;
static design_t D;
static int ncons[MAXP], cons[MAXP][MAXT];      /* output transitions of p   */

/* --- state store (BFS queue + hash set) -----------------------------------
 * States are kept bit-packed (a few dozen bytes each) so that tens of millions
 * fit in memory; pack()/unpack() are exact inverses on every reachable state. */
static uint8_t *SB; static size_t L;            /* packed records, L bytes each */
static uint32_t *PARENT; static uint32_t *WHY;
static size_t nS, capS;
static uint32_t *HT; static size_t capH;

typedef struct { uint8_t *b; size_t bit; } bitw_t;
static void put(bitw_t *w, unsigned v, int n){
    for(int i=0;i<n;i++,w->bit++) if((v>>i)&1u) w->b[w->bit>>3] |= (uint8_t)(1u<<(w->bit&7));
}
static unsigned get(bitw_t *w, int n){
    unsigned v=0; for(int i=0;i<n;i++,w->bit++) v |= (unsigned)((w->b[w->bit>>3]>>(w->bit&7))&1u)<<i; return v;
}
static size_t packed_len(void){ return (size_t)(N->np*(40+5+4) + N->nt*30 + 7)/8; }
static void pack(const state_t *s, uint8_t *buf){
    memset(buf,0,L); bitw_t w={buf,0};
    for(int p=0;p<N->np;p++){
        put(&w,(unsigned)s->pd[p],4); put(&w,s->pho[p],1); put(&w,s->plo[p],1);
        put(&w,(unsigned)(s->poff[p]+1),4); put(&w,s->pnext[p],3); put(&w,s->ppend[p],4);
        put(&w,s->pmsg[p],2); put(&w,s->pqn[p],4);
        for(int k=0;k<QCAP;k++) put(&w,s->pq[p][k],2);
        put(&w,s->itrue[p],4); put(&w,s->ifalse[p],1); put(&w,(unsigned)s->mabs[p],4);
    }
    for(int t=0;t<N->nt;t++){
        put(&w,(unsigned)s->td[t],4); put(&w,s->tho[t],1); put(&w,s->tlo[t],1);
        put(&w,s->tpend[t],4); put(&w,s->tqn[t],4);
        for(int k=0;k<QCAP;k++) put(&w,s->tq[t][k],2);
    }
}
static void unpack(const uint8_t *buf, state_t *s){
    memset(s,0,sizeof *s); bitw_t w={(uint8_t*)buf,0};
    for(int p=0;p<N->np;p++){
        s->pd[p]=(int8_t)get(&w,4); s->pho[p]=(uint8_t)get(&w,1); s->plo[p]=(uint8_t)get(&w,1);
        s->poff[p]=(int8_t)((int)get(&w,4)-1); s->pnext[p]=(uint8_t)get(&w,3); s->ppend[p]=(uint8_t)get(&w,4);
        s->pmsg[p]=(uint8_t)get(&w,2); s->pqn[p]=(uint8_t)get(&w,4);
        for(int k=0;k<QCAP;k++) s->pq[p][k]=(uint8_t)get(&w,2);
        s->itrue[p]=(uint8_t)get(&w,4); s->ifalse[p]=(uint8_t)get(&w,1); s->mabs[p]=(int8_t)get(&w,4);
    }
    for(int t=0;t<N->nt;t++){
        s->td[t]=(int8_t)get(&w,4); s->tho[t]=(uint8_t)get(&w,1); s->tlo[t]=(uint8_t)get(&w,1);
        s->tpend[t]=(uint8_t)get(&w,4); s->tqn[t]=(uint8_t)get(&w,4);
        for(int k=0;k<QCAP;k++) s->tq[t][k]=(uint8_t)get(&w,2);
    }
}
static uint64_t hash(const uint8_t *b){
    uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<L;i++){ h^=b[i]; h*=1099511628211ull; }
    h^=h>>29; return h;
}
static void ht_grow(void){
    size_t ncap=capH*2; uint32_t *n=malloc(ncap*sizeof *n);
    if(!n){ fprintf(stderr,"out of memory\n"); exit(2); }
    memset(n,0xff,ncap*sizeof *n);
    for(size_t i=0;i<nS;i++){ size_t k=hash(SB+i*L)&(ncap-1); while(n[k]!=UINT32_MAX) k=(k+1)&(ncap-1); n[k]=(uint32_t)i; }
    free(HT); HT=n; capH=ncap;
}
/* returns index, sets *isnew */
static uint32_t intern(const state_t *s, uint32_t parent, uint32_t why, bool *isnew){
    uint8_t buf[256]; pack(s,buf);
#ifdef PACK_SELFTEST
    { state_t r; unpack(buf,&r); if(memcmp(&r,s,sizeof r)){ fprintf(stderr,"pack/unpack mismatch\n"); exit(3);} }
#endif
    size_t k=hash(buf)&(capH-1);
    while(HT[k]!=UINT32_MAX){ if(!memcmp(SB+(size_t)HT[k]*L,buf,L)){ *isnew=false; return HT[k]; } k=(k+1)&(capH-1); }
    if(nS==capS){ capS*=2; SB=realloc(SB,capS*L); PARENT=realloc(PARENT,capS*sizeof *PARENT); WHY=realloc(WHY,capS*sizeof *WHY);
                  if(!SB||!PARENT||!WHY){ fprintf(stderr,"out of memory\n"); exit(2);} }
    memcpy(SB+nS*L,buf,L); PARENT[nS]=parent; WHY[nS]=why; HT[k]=(uint32_t)nS; *isnew=true; nS++;
    if(nS*2>capH) ht_grow();
    return (uint32_t)(nS-1);
}
static state_t state_at(size_t i){ state_t s; unpack(SB+i*L,&s); return s; }

/* --- reference token game --------------------------------------------------- */
typedef struct { int8_t m[MAXP]; } mark_t;
static mark_t *RS; static size_t nRS; static uint8_t *RS_seen; /* covered by impl */
static uint8_t *RG_edge;  /* [marking][t] : 1 = edge exists, 2 = covered     */

static bool enabled(const int8_t *m, int t){
    for(int i=0;i<N->npre[t];i++) if(m[N->pre[t][i]]<=0) return false;
    return true;
}
static void fire(int8_t *m, int t){
    for(int i=0;i<N->npre[t];i++)  m[N->pre[t][i]]--;
    for(int i=0;i<N->npost[t];i++) m[N->post[t][i]]++;
}
static long rs_find(const int8_t *m){
    for(size_t i=0;i<nRS;i++) if(!memcmp(RS[i].m,m,MAXP)) return (long)i;
    return -1;
}
static void build_reference(void){
    size_t cap=4096; RS=malloc(cap*sizeof *RS); nRS=0;
    mark_t m0; memset(&m0,0,sizeof m0); for(int p=0;p<N->np;p++) m0.m[p]=(int8_t)N->m0[p];
    RS[nRS++]=m0;
    for(size_t i=0;i<nRS;i++){
        for(int t=0;t<N->nt;t++) if(enabled(RS[i].m,t)){
            mark_t n=RS[i]; fire(n.m,t);
            for(int p=0;p<N->np;p++) if(n.m[p]>100){ fprintf(stderr,"net unbounded?\n"); exit(2); }
            if(rs_find(n.m)<0){ if(nRS==cap){cap*=2; RS=realloc(RS,cap*sizeof *RS);} RS[nRS++]=n; }
        }
    }
    RS_seen=calloc(nRS,1); RG_edge=calloc(nRS*MAXT,1);
    for(size_t i=0;i<nRS;i++) for(int t=0;t<N->nt;t++) if(enabled(RS[i].m,t)) RG_edge[i*MAXT+t]=1;
}

/* --- statistics -------------------------------------------------------------- */
static struct {
    size_t steps, firings, unsound, anomalies, quiescent, stalls, mismatch, blocked;
    int maxq;
    long first_unsound, first_anomaly, first_stall;   /* state index or -1   */
} R;

/* --- WHY encoding for counterexample traces --------------------------------- */
enum { W_INIT=1, W_PSEND, W_PRECV, W_TSEND, W_TRECV, W_TFIRE };
#define WHYCODE(kind,obj,arg) ((uint32_t)((kind)<<16 | (obj)<<8 | (arg)))

static bool push_p(state_t *s,int p,uint8_t m){ if(s->pqn[p]==QCAP) return false; s->pq[p][s->pqn[p]++]=m; if(s->pqn[p]>R.maxq) R.maxq=s->pqn[p]; return true; }
static bool push_t(state_t *s,int t,uint8_t m){ if(s->tqn[t]==QCAP) return false; s->tq[t][s->tqn[t]++]=m; if(s->tqn[t]>R.maxq) R.maxq=s->tqn[t]; return true; }
static uint8_t pop_p(state_t *s,int p){ uint8_t m=s->pq[p][0]; memmove(s->pq[p],s->pq[p]+1,QCAP-1); s->pq[p][QCAP-1]=0; s->pqn[p]--; return m; }
static uint8_t pop_t(state_t *s,int t){ uint8_t m=s->tq[t][0]; memmove(s->tq[t],s->tq[t]+1,QCAP-1); s->tq[t][QCAP-1]=0; s->tqn[t]--; return m; }

static bool naive(void){ return D==NAIVE_PRESET||D==NAIVE_EAGER; }

/* add successor; returns its index */
static void succ(const state_t *n, uint32_t from, uint32_t why){
    bool isnew; (void)intern(n,from,why,&isnew); R.steps++;
}

static void print_trace(long idx);

/* expand one state: enumerate every enabled step */
static void expand(uint32_t i){
    const state_t cur=state_at(i);
    bool any=false;

    /* initial-marking messages, in any order */
    for(int p=0;p<N->np;p++){
        if(cur.itrue[p]){ state_t n=cur; n.itrue[p]--;
            if(push_p(&n,p,MSG(1, D!=NAIVE_PRESET))){ any=true; succ(&n,i,WHYCODE(W_INIT,p,1)); } else R.blocked++; }
        if(cur.ifalse[p]){ state_t n=cur; n.ifalse[p]=0;
            if(push_p(&n,p,MSG(0,1))){ any=true; succ(&n,i,WHYCODE(W_INIT,p,0)); } else R.blocked++; }
    }

    /* place objects */
    for(int p=0;p<N->np;p++){
        if(cur.ppend[p]){                                   /* a pending send */
            state_t n=cur; bool ok;
            if(!naive()){ int c=cur.ppend[p]-1; n.ppend[p]=0; ok=push_t(&n,cons[p][c],MSG(1,0)); }
            else { int c=ncons[p]-cur.ppend[p]; n.ppend[p]--; ok=push_t(&n,cons[p][c],cur.pmsg[p]); }
            if(ok){ any=true; succ(&n,i,WHYCODE(W_PSEND,p,0)); } else R.blocked++;
            continue;
        }
        if(!cur.pqn[p]) continue;
        any=true;
        state_t n=cur; edg_event_t ev=dec(pop_p(&n,p));
        if(!naive()){
            edg_pn_place_state_t ps={ .st={ .d=cur.pd[p], .have_out=cur.pho[p], .last_out=cur.plo[p], .anomaly=false },
                                      .offered=cur.poff[p], .next=cur.pnext[p] };
            edg_pn_place_out_t o; edg_pn_place_step(&ps,(uint8_t)ncons[p],ev,&o);
            n.pd[p]=(int8_t)ps.st.d; n.pho[p]=ps.st.have_out; n.plo[p]=ps.st.last_out;
            if(o.anomaly){ R.anomalies++; if(R.first_anomaly<0){ bool nw; R.first_anomaly=intern(&n,i,WHYCODE(W_PRECV,p,0),&nw); R.steps++; } continue; }
            if(D==CREDIT_ANY && o.offer){            /* any consumer may be chosen */
                for(int c=0;c<ncons[p];c++){ state_t m=n; m.poff[p]=(int8_t)c; m.pnext[p]=0; m.ppend[p]=(uint8_t)(c+1);
                    succ(&m,i,WHYCODE(W_PRECV,p,0)); }
                continue;
            }
            n.poff[p]=ps.offered; n.pnext[p]=ps.next; n.ppend[p]=o.offer?(uint8_t)(o.offer_to+1):0;
        } else {
            /* naive: the place IS an OR gate; it broadcasts its level */
            edg_state_t st={ .d=cur.pd[p], .have_out=cur.pho[p], .last_out=cur.plo[p], .anomaly=false };
            edg_event_t raw; bool emit=edg_gate_step(EDG_LEVEL,0u,&st,ev,&raw);
            n.pd[p]=(int8_t)st.d; n.pho[p]=st.have_out; n.plo[p]=st.last_out;
            if(st.anomaly){ R.anomalies++; if(R.first_anomaly<0){ bool nw; R.first_anomaly=intern(&n,i,WHYCODE(W_PRECV,p,0),&nw); R.steps++; } continue; }
            if(emit && ncons[p]){ n.ppend[p]=(uint8_t)ncons[p]; n.pmsg[p]=MSG(!raw.value,raw.is_init); }
        }
        succ(&n,i,WHYCODE(W_PRECV,p,0));
    }

    /* transition objects */
    for(int t=0;t<N->nt;t++){
        if(cur.tpend[t]){                                   /* a pending send */
            state_t n=cur; int k=N->npre[t]+N->npost[t]-cur.tpend[t]; bool ok;
            if(k<N->npre[t]) ok=push_p(&n,N->pre[t][k],MSG(0,0));            /* withdrawal */
            else             ok=push_p(&n,N->post[t][k-N->npre[t]],MSG(1,0)); /* deposit    */
            n.tpend[t]--;
            if(ok){ any=true; succ(&n,i,WHYCODE(W_TSEND,t,k)); } else R.blocked++;
            continue;
        }
        if(!cur.tqn[t]) continue;
        any=true;
        state_t n=cur; edg_event_t ev=dec(pop_t(&n,t)); bool fires;
        if(!naive()){
            edg_pn_trans_state_t ts={ .st={ .d=cur.td[t], .have_out=cur.tho[t], .last_out=cur.tlo[t], .anomaly=false } };
            bool an=false; fires=edg_pn_trans_step(&ts,(uint8_t)N->npre[t],ev,&an);
            n.td[t]=(int8_t)ts.st.d; n.tho[t]=ts.st.have_out; n.tlo[t]=ts.st.last_out;
            if(an){ R.anomalies++; if(R.first_anomaly<0){ bool nw; R.first_anomaly=intern(&n,i,WHYCODE(W_TRECV,t,0),&nw); R.steps++; } continue; }
        } else {
            /* naive: the transition IS an AND gate; fire on its rising edge */
            edg_state_t st={ .d=cur.td[t], .have_out=cur.tho[t], .last_out=cur.tlo[t], .anomaly=false };
            edg_event_t in=ev; in.value=!in.value; edg_event_t y;
            bool emit=edg_gate_step(EDG_LEVEL,0u,&st,in,&y);
            n.td[t]=(int8_t)st.d; n.tho[t]=st.have_out; n.tlo[t]=st.last_out;
            fires=(emit && y.value);
        }
        if(fires){
            R.firings++;
            long mi=rs_find(cur.mabs);
            if(!enabled(cur.mabs,t)){
                R.unsound++;
                if(R.first_unsound<0){ bool nw; R.first_unsound=intern(&n,i,WHYCODE(W_TFIRE,t,0),&nw); R.steps++; }
                continue;                                   /* do not go on from an illegal firing */
            }
            if(mi>=0) RG_edge[mi*MAXT+t]=2;
            fire(n.mabs,t);
            n.tpend[t]=(uint8_t)(N->npre[t]+N->npost[t]);
            succ(&n,i,WHYCODE(W_TFIRE,t,0));
        } else {
            succ(&n,i,WHYCODE(W_TRECV,t,0));
        }
    }

    /* coverage of reachable markings */
    long mi=rs_find(cur.mabs); if(mi>=0) RS_seen[mi]=1;

    if(!any){
        bool blocked=false;
        for(int p=0;p<N->np;p++) if(cur.ppend[p]||cur.itrue[p]||cur.ifalse[p]) blocked=true;
        for(int t=0;t<N->nt;t++) if(cur.tpend[t]) blocked=true;
        if(blocked) return;                                 /* queue-full deadlock, counted in R.blocked */
        R.quiescent++;
        bool dead=true; for(int t=0;t<N->nt;t++) if(enabled(cur.mabs,t)) dead=false;
        bool same=true; for(int p=0;p<N->np;p++) if(cur.pd[p]!=cur.mabs[p]) same=false;
        if(!same) R.mismatch++;
        if(!dead){ R.stalls++; if(R.first_stall<0) R.first_stall=(long)i; }
    }
}

static void print_marking(const int8_t *m){
    printf("{"); bool f=true;
    for(int p=0;p<N->np;p++) if(m[p]){ printf("%s%s", f?"":" ", N->pname[p]); if(m[p]!=1) printf("x%d",m[p]); f=false; }
    printf("}");
}
static void print_trace(long idx){
    uint32_t chain[4096]; int n=0;
    for(uint32_t k=(uint32_t)idx; k!=UINT32_MAX && n<4096; k=PARENT[k]) chain[n++]=k;
    printf("      trace (%d steps; token messages and firings only):\n",n-1);
    for(int j=n-2;j>=0;j--){
        uint32_t w=WHY[chain[j]]; int kind=w>>16, obj=(w>>8)&0xff, arg=w&0xff;
        switch(kind){
        case W_INIT:  printf("        init   %s %s\n", N->pname[obj], arg?"+1":"(empty)"); break;
        case W_TFIRE: printf("        FIRE   %-8s  abstract marking before: ", N->tname[obj]); { state_t ps=state_at(PARENT[chain[j]]); print_marking(ps.mabs); } printf("\n"); break;
        case W_TSEND:
            if(arg<N->npre[obj]) printf("        %-8s -> %s : withdraw\n", N->tname[obj], N->pname[N->pre[obj][arg]]);
            else                 printf("        %-8s -> %s : deposit\n",  N->tname[obj], N->pname[N->post[obj][arg-N->npre[obj]]]);
            break;
        case W_PRECV: printf("        %-8s processes a message (M=%d)\n", N->pname[obj], state_at(chain[j]).pd[obj]); break;
        case W_PSEND: printf("        %-8s -> its consumer(s): %s\n", N->pname[obj], naive()?"level":"offer"); break;
        case W_TRECV: printf("        %-8s processes a message (d=%d)\n", N->tname[obj], state_at(chain[j]).td[obj]); break;
        }
    }
}

static void run(const net_t *net, design_t d, bool verbose){
    N=net; D=d;
    for(int p=0;p<N->np;p++){ ncons[p]=0; for(int t=0;t<N->nt;t++) for(int k=0;k<N->npre[t];k++) if(N->pre[t][k]==p) cons[p][ncons[p]++]=t; }
    build_reference();

    L=packed_len();
    capS=1<<16; nS=0; SB=malloc(capS*L); PARENT=malloc(capS*sizeof *PARENT); WHY=malloc(capS*sizeof *WHY);
    capH=1<<18; HT=malloc(capH*sizeof *HT); memset(HT,0xff,capH*sizeof *HT);
    memset(&R,0,sizeof R); R.first_unsound=R.first_anomaly=R.first_stall=-1;

    state_t s0; memset(&s0,0,sizeof s0);
    for(int p=0;p<N->np;p++){
        s0.poff[p]=-1; s0.mabs[p]=(int8_t)N->m0[p];
        s0.itrue[p]=(uint8_t)N->m0[p];
        if(D==NAIVE_PRESET){ s0.pho[p]=1; s0.plo[p]=1; }           /* preset: empty, already reported */
        else                 s0.ifalse[p]=(N->m0[p]==0);           /* (false, init) for empty places */
    }
    for(int t=0;t<N->nt;t++){
        if(D==NAIVE_EAGER){ s0.td[t]=0; s0.tho[t]=0; s0.tlo[t]=0; }  /* plain EDG_STATE_INIT gate */
        else { s0.td[t]=(int8_t)N->npre[t]; s0.tho[t]=1; s0.tlo[t]=0; }
    }
    if(PRELOAD){
        /* Start with every initialization message already in its queue, which is what
         * the samples and the FreeRTOS harness do (edg_pn_mark() before the scheduler
         * starts). This removes the interleavings of the start-up phase only. */
        for(int p=0;p<N->np;p++){
            while(s0.itrue[p]){ s0.itrue[p]--; push_p(&s0,p,MSG(1, D!=NAIVE_PRESET)); }
            if(s0.ifalse[p]){ s0.ifalse[p]=0; push_p(&s0,p,MSG(0,1)); }
        }
    }
    bool nw; intern(&s0,UINT32_MAX,0,&nw);
    bool capped=false;
    for(size_t i=0;i<nS;i++){ if(nS>MAX_STATES){ capped=true; break; } expand((uint32_t)i); }

    size_t cov=0, edges=0, ecov=0;
    for(size_t i=0;i<nRS;i++){ cov+=RS_seen[i]; for(int t=0;t<N->nt;t++){ if(RG_edge[i*MAXT+t]) edges++; if(RG_edge[i*MAXT+t]==2) ecov++; } }

    printf("%-15s %-13s states=%-8zu steps=%-9zu firings=%-8zu unsound=%-6zu anomalies=%-5zu "
           "quiescent=%-4zu stalls=%-4zu markings=%zu/%zu edges=%zu/%zu maxq=%d%s%s\n",
           N->name, DESIGN_NAME[D], nS, R.steps, R.firings, R.unsound, R.anomalies,
           R.quiescent, R.stalls, cov, nRS, ecov, edges, R.maxq,
           R.mismatch?"  !!descriptor/marking mismatch":"", R.blocked?"  !!queue full":"");
    if(capped) printf("    !! state limit (%zu) reached: exploration INCOMPLETE\n",(size_t)MAX_STATES);
    if(verbose){
        if(R.first_unsound>=0){ printf("    first illegal firing:\n"); print_trace(R.first_unsound); }
        if(R.first_anomaly>=0){ printf("    first place anomaly (withdrawal without token/offer):\n"); print_trace(R.first_anomaly); }
        if(R.first_stall>=0){   printf("    first stall, abstract marking "); { state_t fs=state_at((size_t)R.first_stall); print_marking(fs.mabs); }
                                printf(" (not dead in the net):\n"); print_trace(R.first_stall); }
    }
    free(SB); free(PARENT); free(WHY); free(HT); free(RS); free(RS_seen); free(RG_edge);
}

/* usage: pn_explore [-v] [--max=N] [--preload] [net-name [design-name]] */
int main(int argc, char **argv){
    bool verbose=false; const char *only_net=NULL, *only_design=NULL;
    for(int a=1;a<argc;a++){
        if(!strcmp(argv[a],"-v")) verbose=true;
        else if(!strncmp(argv[a],"--max=",6)) MAX_STATES=(size_t)atoll(argv[a]+6);
        else if(!strcmp(argv[a],"--preload")) PRELOAD=true;
        else if(!only_net) only_net=argv[a];
        else only_design=argv[a];
    }
    setvbuf(stdout,NULL,_IOLBF,0);
    printf("EDG-PN exhaustive interleaving exploration (queue depth %d%s)\n\n", QCAP,
           PRELOAD ? ", initial marking preloaded" : "");
    for(int i=0;i<N_NETS;i++){
        if(only_net && strcmp(only_net,NETS[i].name)) continue;
        printf("# %s  [%s]  |P|=%d |T|=%d\n", NETS[i].name, NETS[i].klass, NETS[i].np, NETS[i].nt);
        for(int d=CREDIT_RR; d<=NAIVE_EAGER; d++){
            if(only_design && strcmp(only_design,DESIGN_NAME[d])) continue;
            run(&NETS[i], (design_t)d, verbose);
        }
        printf("\n");
    }
    return 0;
}
