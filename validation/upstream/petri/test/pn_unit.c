/*==============================================================================
 *  pn_unit.c  --  unit tests of the two pure cores of the EDG-PN layer
 *                 (edg_pn_place_step, edg_pn_trans_step).
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

static const edg_event_t DEP  = { true,  false };   /* deposit            */
static const edg_event_t WDR  = { false, false };   /* withdrawal         */
static const edg_event_t TOK0 = { true,  true  };   /* initial token      */
static const edg_event_t NOTK = { false, true  };   /* "no initial token" */

int main(void)
{
    edg_pn_place_out_t o;

    /* --- place: the descriptor is the marking; the level is an OR gate ----- */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        edg_pn_place_step(&p, 1, NOTK, &o);                 /* empty at start   */
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
    /* --- place: initial tokens are initialization messages ------------------ */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        edg_pn_place_step(&p, 1, TOK0, &o);
        CHECK(p.st.d == 1 && o.level_changed && o.level.value && o.level.is_init && o.offer);
        edg_pn_place_step(&p, 1, TOK0, &o);
        CHECK(p.st.d == 2 && !o.level_changed && !o.offer);
    }
    /* --- place: a choice place rotates its single offer --------------------- */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        int seen[3] = {0, 0, 0};
        edg_pn_place_step(&p, 3, TOK0, &o); CHECK(o.offer); seen[o.offer_to]++;
        for (int i = 0; i < 5; i++) {
            edg_pn_place_step(&p, 3, DEP, &o); CHECK(!o.offer);       /* one offer only */
            edg_pn_place_step(&p, 3, WDR, &o); CHECK(o.offer && !o.anomaly);
            seen[o.offer_to]++;
        }
        CHECK(seen[0] == 2 && seen[1] == 2 && seen[2] == 2);
    }
    /* --- place: a withdrawal that answers no offer is an anomaly ------------ */
    {
        edg_pn_place_state_t p = EDG_PN_PLACE_INIT;
        edg_pn_place_step(&p, 0, TOK0, &o);                 /* sink place: never offers */
        CHECK(!o.offer && p.st.d == 1);
        edg_pn_place_step(&p, 0, WDR, &o);
        CHECK(o.anomaly);
    }
    /* --- transition: the descriptor is the distance from enabledness ------- */
    for (uint8_t n = 1; n <= 4; n++) {
        edg_pn_trans_state_t t = edg_pn_trans_init(n);
        bool an = false;
        CHECK(t.st.d == n);
        for (int round = 0; round < 3; round++) {
            for (uint8_t k = 1; k < n; k++) {
                CHECK(!edg_pn_trans_step(&t, n, DEP, &an) && !an);
                CHECK(t.st.d == n - k);
            }
            CHECK(edg_pn_trans_step(&t, n, DEP, &an) && !an);   /* n-th offer fires */
            CHECK(t.st.d == n);                                 /* offers consumed  */
        }
    }

    printf("pn_unit: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
