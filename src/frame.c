// frame.c
#include "frame.h"
#include <stdio.h>
#include <stdlib.h>

char *constructMessage(unsigned int truck_id,
                       e_rw rw,
                       int param,
                       int value,
                       EventType eventType)
{
    const int BUF_SIZE = 256;
    char *buffer = (char *)malloc(BUF_SIZE);
    if (!buffer) {
        return NULL;
    }

    int bytes = snprintf(
        buffer,
        BUF_SIZE,
        "%d %d %d %d %d\n",
        truck_id,       // 1
        (int)rw,        // 2
        param,          // 3
        value,       // 4
        (int)eventType  // 5
    );

    if (bytes < 0 || bytes >= BUF_SIZE) {
        free(buffer);
        return NULL;
    }

    return buffer;  // caller free()
}

DataFrame *parseMessage(const char *message)
{
    if (!message) {
        return NULL;
    }

    DataFrame *df = (DataFrame *)malloc(sizeof(DataFrame));
    if (!df) {
        return NULL;
    }

    int eventInt = 0;
    int tempReadWrite = e_undef;

    int scanned = sscanf(
        message,
        "%d %d %d %d %d",
        &df->truck_id,  // 1
        &tempReadWrite, // 2
        &df->param,     // 3
        &df->value,  // 4
        &eventInt       // 5
    );

    if (scanned != 5)
    {
        free(df);
        return NULL;
    }

    df->eventType = (EventType)eventInt;
    df->readWriteFlag = (e_rw)tempReadWrite;

    return df;  // caller free()
}
