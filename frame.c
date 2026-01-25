#include "frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *constructMessage(int truck_id, int rw,int velocity,int distance,EventType eventType){

    char *buffer = (char *)malloc(256);
    DataFrame df;

    df.truck_id     = truck_id;
    df.readWriteFlag= rw;
    df.time         = time(NULL);
    df.velocity     = velocity;
    df.distance     = distance;
    df.eventType    = eventType;

    int bytes = snprintf(
        buffer, sizeof(buffer),
        "%d;%d;%ld;%d;%d;%d\n",
        df.truck_id,
        df.readWriteFlag,
        df.time,
        df.eventType,
        df.velocity,
        df.distance
    );

    return buffer;

}

DataFrame *parseMessage(const char *message){
    
    DataFrame *df = (DataFrame *)malloc(sizeof(DataFrame));
    sscanf(message, "%d;%d;%ld;%d;%d;%d",
        &df->truck_id,
        &df->readWriteFlag,
        &df->time,
        &df->velocity,
        &df->distance,
        (int*)&df->eventType    
    );
    return df;
}