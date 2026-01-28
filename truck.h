#ifndef TRUCK_H
#define TRUCK_H

// Define Truck structure
struct Truck {
    unsigned int id : 3;
    unsigned int position : 3;
    int currentSpeed;
    int currentDistance;
};

#endif