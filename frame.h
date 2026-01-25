// frame.h
#ifndef FRAME_H
#define FRAME_H

#include <time.h>

typedef enum {
    INTRUSION = 0,
    TELEMETRY = 1,
    EMERGENCY_BRAKE = 2,
    JOIN_PLATOON = 3,
    LEAVE_PLATOON = 4,
    LANE_CHANGE = 5
} EventType;

typedef struct {
    int      truck_id;
    time_t   time;
    int      readWriteFlag;   // 0 = read, 1 = write
    int      velocity;
    int      distance;
    EventType eventType;
} DataFrame;

char *constructMessage(int truck_id,
                       int rw,
                       int velocity,
                       int distance,
                       EventType eventType);

DataFrame *parseMessage(const char *message);

#endif
