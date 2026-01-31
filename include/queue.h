#ifndef TX_QUEUE_H
#define TX_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include "frame.h"

#define TX_QUEUE_SIZE 32

typedef struct {
    DataFrame buf[TX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t  notEmpty;
} TxQueue;


void TxQueue_init(TxQueue *q);
bool TxQueue_push(TxQueue *q, DataFrame *m);
bool TxQueue_pop(TxQueue *q, DataFrame *out);

#endif 
