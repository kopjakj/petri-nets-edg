/* Run the real gate handlers on independent POSIX mailbox workers. */
#include "edg_pn_choice.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

enum { P, Q, R, DoneA, DoneB, DoneC, Places };
enum { A, B, C, Transitions };
enum { Pairs = 32, Boxes = 3 * EDG_CHOICE_MAX, Capacity = 256 };
#define BIT EDG_CHOICE_BIT

typedef struct {
    pthread_cond_t ready;
    pthread_t thread;
    edg_choice_msg_t items[Capacity];
    unsigned head, size;
    bool active;
} inbox_t;

static edg_choice_net_t net;
static inbox_t inbox[Boxes];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t drained = PTHREAD_COND_INITIALIZER;
static unsigned unfinished;
static bool stop;

static void require(bool ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "%s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void post(void *ctx, edg_choice_msg_t msg)
{
    (void)ctx;
    int box = edg_choice_mailbox(&net, msg);
    require(box >= 0 && inbox[box].active, "invalid mailbox");
    inbox_t *q = &inbox[box];
    pthread_mutex_lock(&mutex);
    require(q->size < Capacity, "mailbox storage exhausted");
    q->items[(q->head + q->size) % Capacity] = msg;
    ++q->size;
    ++unfinished;
    pthread_cond_signal(&q->ready);
    pthread_mutex_unlock(&mutex);
}

static void *worker(void *arg)
{
    inbox_t *q = arg;
    pthread_mutex_lock(&mutex);
    for (;;) {
        while (!q->size && !stop) pthread_cond_wait(&q->ready, &mutex);
        if (stop) break;
        edg_choice_msg_t m = q->items[q->head];
        q->head = (q->head + 1) % Capacity;
        --q->size;
        pthread_mutex_unlock(&mutex);
        /* Token, AND and group updates run outside the transport mutex. */
        require(edg_choice_step(&net, m, post, NULL), "protocol error");
        pthread_mutex_lock(&mutex);
        if (--unfinished == 0) pthread_cond_signal(&drained);
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main(void)
{
    const edg_choice_arcs_t arcs[] = {
        {BIT(P)|BIT(Q), BIT(DoneA)}, {BIT(P)|BIT(Q), BIT(DoneB)},
        {BIT(R), BIT(DoneC)}
    };
    require(edg_choice_init(&net, Places, arcs, Transitions), "invalid wiring");
    for (unsigned p = 0; p < Places; ++p) inbox[p].active = true;
    for (unsigned t = 0; t < Transitions; ++t) {
        inbox[EDG_CHOICE_MAX + t].active = true;
        inbox[2 * EDG_CHOICE_MAX + net.group_of_transition[t]].active = true;
    }
    for (unsigned i = 0; i < Boxes; ++i) {
        if (!inbox[i].active) continue;
        require(pthread_cond_init(&inbox[i].ready, NULL) == 0, "condition init failed");
        require(pthread_create(&inbox[i].thread, NULL, worker, &inbox[i]) == 0,
                "worker creation failed");
    }
    for (unsigned p = 0; p < Places; ++p)
        post(NULL, (edg_choice_msg_t){EDG_CHOICE_INIT, p});
    for (unsigned i = 0; i < Pairs; ++i)
        for (unsigned p = P; p <= R; ++p)
            post(NULL, (edg_choice_msg_t){EDG_CHOICE_DEPOSIT, p});

    pthread_mutex_lock(&mutex);
    while (unfinished) pthread_cond_wait(&drained, &mutex);
    stop = true;
    for (unsigned i = 0; i < Boxes; ++i)
        if (inbox[i].active) pthread_cond_signal(&inbox[i].ready);
    pthread_mutex_unlock(&mutex);
    for (unsigned i = 0; i < Boxes; ++i) {
        if (!inbox[i].active) continue;
        require(pthread_join(inbox[i].thread, NULL) == 0, "worker join failed");
        pthread_cond_destroy(&inbox[i].ready);
    }
    int a = net.place[DoneA].st.d, b = net.place[DoneB].st.d, c = net.place[DoneC].st.d;
    require(a + b == Pairs && c == Pairs && !net.place[P].st.d &&
            !net.place[Q].st.d && !net.place[R].st.d, "incorrect final marking");
    printf("Concurrent workers: A=%d B=%d (shared pairs=%d), C=%d (independent)\n",
           a, b, Pairs, c);
    return EXIT_SUCCESS;
}
