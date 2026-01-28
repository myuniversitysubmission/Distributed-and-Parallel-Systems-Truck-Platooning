#include <pthread.h>
#include <stdbool.h>
#include "frame.h"
#include "queue.h"

void TxQueue_init(TxQueue *q) {
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->notEmpty, NULL);
}

bool TxQueue_push(TxQueue *q, DataFrame *m) {
    pthread_mutex_lock(&q->lock);
    if (q->count == TX_QUEUE_SIZE) {
        // queue full – drop or handle as needed
        pthread_mutex_unlock(&q->lock);
        return false;
    }
    q->buf[q->tail] = *m;
    q->tail = (q->tail + 1) % TX_QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->notEmpty);
    pthread_mutex_unlock(&q->lock);
    return true;
}

bool TxQueue_pop(TxQueue *q, DataFrame *out) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0) {
        pthread_cond_wait(&q->notEmpty, &q->lock);
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % TX_QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return true;
}