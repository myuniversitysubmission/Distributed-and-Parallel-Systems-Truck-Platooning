#ifndef TRUCK_H
#define TRUCK_H

#include "pthread.h"

// Define Truck structure
struct Truck {
    unsigned int id : 3;
    unsigned int position : 3;
    int currentSpeed;
    int currentDistance;
    pthread_mutex_t truck_mutex;
};

#endif