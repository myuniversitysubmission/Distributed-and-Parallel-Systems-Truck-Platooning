// frame.h
#ifndef FRAME_H
#define FRAME_H

#include <time.h>

typedef enum {
    INTRUSION = 0,
    SPEED = 1,
    DISTANCE = 2,
    EMERGENCY_BRAKE = 3,
    JOIN_PLATOON = 4,
    LEAVE_PLATOON = 5,
    LANE_CHANGE = 6,
    LEADER_LEFT = 7
} EventType;

typedef enum
{
    e_read = 0,
    e_write = 1,
    e_undef = 2
} e_rw;

typedef struct {
    int truck_id;
    time_t time;
    int readWriteFlag; // 0 = read, 1 = write
    int param;
    int value;
    EventType eventType;
} DataFrame;


char *constructMessage(int truck_id,
                       e_rw rw,
                       int param,
                       int value,
                       EventType eventType);

DataFrame *parseMessage(const char *message);

#endif
