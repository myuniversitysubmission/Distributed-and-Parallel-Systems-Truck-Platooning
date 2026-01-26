// frame.c
#include "frame.h"
#include <stdio.h>
#include <stdlib.h>

char *constructMessage(int truck_id,
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

    time_t now = time(NULL);

    // FORMAT:
    // id;rw;time;eventType;velocity;distance\n
    //  1  2    3     4        5        6
    int bytes = snprintf(
        buffer,
        BUF_SIZE,
        "%d;%d;%lld;%d;%d;%d\n",
        truck_id,       // 1
        (int)rw,        // 2
        (long long)now, // 3
        param,         // 4
        value,         // 5
        (int)eventType  // 6
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

    long long time_ll = 0;
    int eventInt = 0;
    int tempReadWrite = e_undef;

    // id;rw;time;param;value,eventType\n
    //  1  2    3     4   5        6
    int scanned = sscanf(
        message,
        "%d;%d;%lld;%d;%d;%d",
        &df->truck_id,  // 1
        &tempReadWrite, // 2
        &time_ll,       // 3
        &df->param,     // 4
        &df->value,     // 5
        &eventInt       // 6
    );

    if (scanned != 6)
    {
        free(df);
        return NULL;
    }

    df->time = (time_t)time_ll;
    df->eventType = (EventType)eventInt;
    df->readWriteFlag = (e_rw)tempReadWrite;

    return df;  // caller free()
}
