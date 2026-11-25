/*==============================================================================
 *  pn_unit.c  --  unit tests of the pure cores of the EDG-PN layer
 *                 (edg_pn_place_step, edg_pn_trans_step) and of
 *                 edg_pn_auto_release.
 *============================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include "edg_pn.h"

/* link stubs: edg.c's task/gpio symbols are unused here */
QueueHandle_t xQueueCreate(UBaseType_t l,UBaseType_t i){(void)l;(void)i;return (void*)1;}
BaseType_t xQueueSendToBack(QueueHandle_t q,const void*it,TickType_t t){(void)q;(void)it;(void)t;return 1;}
BaseType_t xQueueReceive(QueueHandle_t q,void*b,TickType_t t){(void)q;(void)b;(void)t;return 1;}
TickType_t xTaskGetTickCount(void){return 0;}
void vTaskDelay(TickType_t i){(void)i;}
void gpio_init(uint32_t g){(void)g;} void gpio_set_dir(uint32_t g,int d){(void)g;(void)d;}
void gpio_pull_up(uint32_t g){(void)g;} void gpio_pull_down(uint32_t g){(void)g;}
int gpio_get(uint32_t g){(void)g;return 0;} void gpio_put(uint32_t g,int v){(void)g;(void)v;}
void stdio_init_all(void){} void sleep_ms(uint32_t m){(void)m;}

static int checks, fails;
#define CHECK(c) do { checks++; if(!(c)){ fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while(0)

static const edg_event_t DEP  = { true,  false };   /* deposit / kept offer   */
static const edg_event_t WDR  = { false, false };   /* withdrawal             */
static const edg_event_t INI  = { false, true  };   /* init, later: release   */
static const edg_event_t REQ  = { true,  true  };   /* request / release offer */

int main(void)
{
    edg_pn_place_out_t o;

    /* --- place: the descriptor is the token count; the level is an OR gate -- */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        edg_pn_place_step(&p, 1, INI, &o);                  /* initialization   */
        CHECK(o.kind == EDG_PN_IN_INIT && p.ready && !o.anomaly);
        CHECK(p.st.d == 0 && o.level_changed && !o.level.value && o.level.is_init && !o.offer);
        edg_pn_place_step(&p, 1, DEP, &o);                  /* 0 -> 1           */
        CHECK(p.st.d == 1 && o.level_changed && o.level.value && !o.level.is_init);
        CHECK(o.offer && o.offer_to == 0 && p.offered == 0);
        edg_pn_place_step(&p, 1, DEP, &o);                  /* 1 -> 2: silent   */
        CHECK(p.st.d == 2 && !o.level_changed && !o.offer);
        edg_pn_place_step(&p, 1, WDR, &o);                  /* 2 -> 1: re-offer */
        CHECK(p.st.d == 1 && !o.level_changed && o.offer && !o.anomaly);
        edg_pn_place_step(&p, 1, WDR, &o);                  /* 1 -> 0           */
        CHECK(p.st.d == 0 && o.level_changed && !o.level.value && !o.offer && p.offered == -1);
        edg_pn_place_step(&p, 1, WDR, &o);                  /* nothing to take  */
        CHECK(o.anomaly && p.st.d == 0);
    }
    /* --- place: the initial marking is an initialization plus deposits ------ */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        edg_pn_place_step(&p, 1, INI, &o);
        CHECK(o.level_changed && o.level.is_init && !o.level.value && !o.offer);
        edg_pn_place_step(&p, 1, DEP, &o);
        CHECK(p.st.d == 1 && o.level_changed && o.level.value && o.offer);
        edg_pn_place_step(&p, 1, DEP, &o);
        CHECK(p.st.d == 2 && !o.level_changed && !o.offer);
    }
    /* --- place: a choice place rotates its single offer --------------------- */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        int seen[3] = {0, 0, 0};
        edg_pn_place_step(&p, 3, INI, &o);
        edg_pn_place_step(&p, 3, DEP, &o); CHECK(o.offer); seen[o.offer_to]++;
        for (int i = 0; i < 5; i++) {
            edg_pn_place_step(&p, 3, DEP, &o); CHECK(!o.offer);       /* one offer only */
            edg_pn_place_step(&p, 3, WDR, &o); CHECK(o.offer && !o.anomaly);
            seen[o.offer_to]++;
        }
        CHECK(seen[0] == 2 && seen[1] == 2 && seen[2] == 2);
    }
    /* --- place: release passes the token on; a full round of releases stops
     *     the offers until a request arrives -------------------------------- */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        edg_pn_place_step(&p, 2, INI, &o);
        edg_pn_place_step(&p, 2, DEP, &o);
        CHECK(o.offer && o.offer_to == 0);
        edg_pn_place_step(&p, 2, INI, &o);                  /* consumer 0 releases */
        CHECK(o.kind == EDG_PN_IN_RELEASE && !o.anomaly && p.st.d == 1);
        CHECK(o.offer && o.offer_to == 1 && !o.level_changed);
        edg_pn_place_step(&p, 2, INI, &o);                  /* consumer 1 releases */
        CHECK(o.kind == EDG_PN_IN_RELEASE && !o.offer && p.offered == -1 && p.released == 3);
        edg_pn_place_step(&p, 2, DEP, &o);                  /* a deposit alone does not restart */
        CHECK(!o.offer && p.st.d == 2);
        edg_pn_place_step(&p, 2, REQ, &o);                  /* a request does    */
        CHECK(o.kind == EDG_PN_IN_REQUEST && o.offer && o.offer_to == 0 && p.st.d == 2);
        edg_pn_place_step(&p, 2, REQ, &o);                  /* request while offering: no second offer */
        CHECK(!o.offer && p.released == 0);
        edg_pn_place_step(&p, 2, WDR, &o);                  /* consumer 0 fired  */
        CHECK(!o.anomaly && p.st.d == 1 && o.offer && o.offer_to == 1);
    }
    /* --- place: a consumer that released is skipped until the next reset ---- */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        edg_pn_place_step(&p, 3, INI, &o);
        edg_pn_place_step(&p, 3, DEP, &o);   CHECK(o.offer && o.offer_to == 0);
        edg_pn_place_step(&p, 3, INI, &o);   CHECK(o.offer && o.offer_to == 1);  /* 0 released */
        edg_pn_place_step(&p, 3, REQ, &o);   CHECK(!o.offer && p.released == 0); /* request clears */
        edg_pn_place_step(&p, 3, INI, &o);   CHECK(o.offer && o.offer_to == 2);  /* 1 released */
        edg_pn_place_step(&p, 3, INI, &o);   CHECK(o.offer && o.offer_to == 0);  /* 2 released */
        edg_pn_place_step(&p, 3, INI, &o);   CHECK(!o.offer && p.released == 7); /* all released */
        edg_pn_place_step(&p, 3, WDR, &o);   CHECK(o.anomaly);                   /* no offer open */
    }
    /* --- place: release or withdrawal that answers no offer is an anomaly --- */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        edg_pn_place_step(&p, 2, INI, &o);
        edg_pn_place_step(&p, 2, INI, &o);                  /* release, no offer */
        CHECK(o.kind == EDG_PN_IN_RELEASE && o.anomaly && p.st.d == 0);
        edg_pn_place_step(&p, 0, DEP, &o);                  /* sink place: never offers */
        CHECK(!o.offer && p.st.d == 1);
        edg_pn_place_step(&p, 0, WDR, &o);
        CHECK(o.anomaly);
    }
    /* --- transition without release input: the descriptor is the distance
     *     from enabledness ----------------------------------------------------- */
    for (uint8_t n = 1; n <= 4; n++) {
        edg_pn_trans_state_t t = edg_pn_trans_init(n);
        bool an = false;
        CHECK(t.st.d == n);
        for (int round = 0; round < 3; round++) {
            for (uint8_t k = 1; k < n; k++) {
                CHECK(edg_pn_trans_step(&t, n, false, DEP, &an) == EDG_PN_WAIT && !an);
                CHECK(t.st.d == n - k);
            }
            CHECK(edg_pn_trans_step(&t, n, false, DEP, &an) == EDG_PN_FIRE && !an);
            CHECK(t.st.d == n);                                 /* offers consumed  */
        }
        CHECK(edg_pn_trans_step(&t, n, false, REQ, &an) == EDG_PN_WAIT && an);
    }
    /* --- transition with a release input ------------------------------------- */
    for (uint8_t n = 0; n <= 3; n++) {
        edg_pn_trans_state_t t = edg_pn_trans_init(n);
        bool an = false;
        for (int round = 0; round < 3; round++) {
            if (n > 0) {
                CHECK(edg_pn_trans_step(&t, n, true, REQ, &an) == EDG_PN_RELEASE && !an);
                CHECK(t.st.d == n);                             /* nothing kept     */
                for (uint8_t k = 1; k < n; k++) {
                    CHECK(edg_pn_trans_step(&t, n, true, DEP, &an) == EDG_PN_WAIT && !an);
                }
                CHECK(edg_pn_trans_step(&t, n, true, DEP, &an) == EDG_PN_REQUEST && !an);
                CHECK(t.st.d == 0);
            }
            CHECK(edg_pn_trans_step(&t, n, true, REQ, &an) == EDG_PN_FIRE && !an);
            CHECK(t.st.d == n);
        }
    }
    /* --- automatic configuration of release inputs -------------------------- */
    {
        /* sensor choice: part -> toA (with okA), part -> toB (with okB) */
        static int q[16];
        #define Q(i) ((QueueHandle_t)&q[i])
        edg_pn_place_t P[3] = {
            { .name = "part", .in = Q(0), .consumer = { Q(10), Q(11) }, .n_consumers = 2 },
            { .name = "okA",  .in = Q(1), .consumer = { Q(10) },        .n_consumers = 1 },
            { .name = "okB",  .in = Q(2), .consumer = { Q(11) },        .n_consumers = 1 },
        };
        edg_pn_transition_t T[2] = {
            { .name = "toA", .in = Q(10), .pre = { Q(1), Q(0) }, .n_pre = 2 },
            { .name = "toB", .in = Q(11), .pre = { Q(0), Q(2) }, .n_pre = 2 },
        };
        CHECK(edg_pn_auto_release(P, 3, T, 2) == 0);
        CHECK(T[0].release_input == 2 && T[1].release_input == 1);
        CHECK(P[0].release_mask == 3 && P[1].release_mask == 0 && P[2].release_mask == 0);

        /* two philosophers: each transition has two shared inputs */
        edg_pn_place_t F[2] = {
            { .name = "f0", .in = Q(3), .consumer = { Q(12), Q(13) }, .n_consumers = 2 },
            { .name = "f1", .in = Q(4), .consumer = { Q(12), Q(13) }, .n_consumers = 2 },
        };
        edg_pn_transition_t E[2] = {
            { .name = "take0", .in = Q(12), .pre = { Q(3), Q(4) }, .n_pre = 2 },
            { .name = "take1", .in = Q(13), .pre = { Q(4), Q(3) }, .n_pre = 2 },
        };
        CHECK(edg_pn_auto_release(F, 2, E, 2) == 2);
        CHECK(E[0].release_input == 0 && E[1].release_input == 0 && F[0].release_mask == 0);
    }

    printf("pn_unit: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
