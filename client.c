#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>

#include "truck.h"
#include "frame.h"

#pragma comment(lib, "Ws2_32.lib")
typedef enum{
    e_kill = 0U, 
    e_active =1U }
state;

#define DO_NOTHING \
    {              \
        ;          \
    }

struct Truck truck;
volatile state client_state = e_active;
pthread_mutex_t truck_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned int g_followerCount = 0;

bool matchSpeed(struct Truck *t, int targetSpeed)
{
    int step = 5;

    pthread_mutex_lock(&truck_mutex);

    if (t->currentSpeed < targetSpeed) {
        t->currentSpeed += step;
        if (t->currentSpeed > targetSpeed)
            t->currentSpeed = targetSpeed;
    } else if (t->currentSpeed > targetSpeed) {
        t->currentSpeed -= step;
        if (t->currentSpeed < targetSpeed)
            t->currentSpeed = targetSpeed;
    }

    printf("\n Truck %d: Speed = %d\n", t->id, t->currentSpeed);

    pthread_mutex_unlock(&truck_mutex);
    return (t->currentSpeed == targetSpeed);
}

bool matchDistance(struct Truck *t, int targetDistance)
{
    int distanceStep = 10;

    pthread_mutex_lock(&truck_mutex);

    if (t->currentDistance < targetDistance) {
        t->currentDistance += distanceStep;
        if (t->currentDistance > targetDistance)
            t->currentDistance = targetDistance;
    } else if (t->currentDistance > targetDistance) {
        t->currentDistance -= distanceStep;
        if (t->currentDistance < targetDistance)
            t->currentDistance = targetDistance;
    }

    printf("\n Truck %d: Distance = %d\n", t->id, t->currentDistance);

    pthread_mutex_unlock(&truck_mutex);
    return (t->currentDistance == targetDistance);
}

void reportIntrusion(struct Truck *t)
{
    int speed = 20;
    int step  = 5;

    pthread_mutex_lock(&truck_mutex);

    if (t->currentSpeed > speed) {
        t->currentSpeed -= step;
        if (t->currentSpeed < speed){
            t->currentSpeed = speed;
        }
        printf("Truck %d: Intrusion detected! Slowing down to %d\n",
               t->id, t->currentSpeed);
    }
    else {
        printf("Truck %d: Intrusion active. Speed stabilized at %d\n",
               t->id, t->currentSpeed);
    }

    pthread_mutex_unlock(&truck_mutex);
}

void *TXthread(void* socketTXCopy){
    //(1) start point
    // construct a message()
    // SendMessage()
    // check Error in message()
    SOCKET *SocketTX = (SOCKET *)socketTXCopy;

    int temp_case      = SPEED;
    int targetSpeed    = 80;
    int targetDistance = 100;

    while(client_state == e_active){
        char *msg = NULL;

        matchSpeed(&truck, targetSpeed);
        matchDistance(&truck, targetDistance);

        switch (temp_case){
        case INTRUSION:
            reportIntrusion(&truck);
            msg = constructMessage(
                truck.id,               // truck_id
                e_write,                // rw = write
                truck.currentSpeed,     // reporting intrusion
                truck.currentDistance,  // reporting intrusion
                INTRUSION               // eventType
            );
            temp_case = SPEED;
            break;

        case SPEED:
            msg = constructMessage(
                truck.id,              // truck_id
                e_write,                     // rw = write
                truck.currentSpeed,
                truck.currentDistance,
                SPEED              // eventType
            );
            temp_case = DISTANCE;
            break;

        case DISTANCE:
            msg = constructMessage(
                truck.id,              // truck_id
                e_write,               // rw = write
                truck.currentSpeed,
                truck.currentDistance,
                DISTANCE              // eventType
            );
            temp_case = SPEED;
            break;

        case EMERGENCY_BRAKE:
            msg = constructMessage(
                truck.id,
                e_write,
                0,
                truck.currentDistance,
                EMERGENCY_BRAKE
            );
            break;

        case LEAVE_PLATOON:
            printf("\n Some client left");
            break;
        default:
            temp_case = SPEED;
            printf("\n TX - default case");
            continue;
            // //TODO: CHECK THIS PART
            // printf("\n TX - Undefined case");
            // closesocket(*SocketTX);
            // free(msg);
            // return NULL;
        }

        if (msg == NULL) {
            printf("Failed to construct frame message\n");
            closesocket(*SocketTX);
            return NULL;
        }

        int len = (int)strlen(msg);
        int bytesSent = send(*SocketTX, msg, len, 0);

        if (bytesSent == SOCKET_ERROR)
        {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
            {
                // no data yet, continue loop
                // do_nothing
            }
            else
            {
                printf("\n TX-Server disconnected\n");
                free(msg);
                client_state = e_kill;
                closesocket(*SocketTX);
                return (NULL);
            }
        }
        else if (bytesSent == 0)
        {
            printf("\n TX-Server disconnected\n");
            free(msg);
            client_state = e_kill;
            closesocket(*SocketTX);
            WSACleanup();
        }

        free(msg);
        sleep(5); // Sleep briefly to avoid busy-waiting
    }
    
    printf("TX thread exiting\n");
    return NULL;
}

void *RXthread(void* socketRXCopy){
    // (4) Applying part
    SOCKET* socketRX = (SOCKET*) socketRXCopy;
    char RXBuffer[1024];
    DataFrame *receivedFrame;

    while(client_state == e_active){
        //(1) Receive message
        int bytesReceived = recv(*socketRX, RXBuffer, sizeof(RXBuffer) - 1, 0);

        //(2) parse message / check error in message
        if (bytesReceived > 0)
        {
            RXBuffer[bytesReceived] = '\0';
            receivedFrame = parseMessage(RXBuffer);

            if (receivedFrame == NULL) {
                printf("Invalid frame received\n");
                continue;
            }
        }
        if (bytesReceived == SOCKET_ERROR)
        {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
            {
                // no data yet, continue loop
                continue;
            }
            else
            {
                printf("\n RX-Server disconnected\n");
                client_state = e_kill;
                closesocket(*socketRX);
                return NULL;
            }
            // real error
        }
        else if (bytesReceived == 0)
        {
            printf("\n RX-Server disconnected\n");
            client_state = e_kill;
            closesocket(*socketRX);
            return NULL;
        }
        //(3) execute state_machine

        switch(receivedFrame->eventType){
            case CLIENT_ID:
                truck.id = receivedFrame->truck_id;
                truck.position = receivedFrame->value;
                printf("Received Truck ID = %u, Position = %u", truck.id, truck.position);
                break;
            case SPEED:
                matchSpeed(&truck, receivedFrame->value);
                break;
            case DISTANCE:
                matchDistance(&truck, receivedFrame->value);
                break;
            case INTRUSION:
                printf("\n Intrusion acknowledged");
                reportIntrusion(&truck);
                break;
            case LANE_CHANGE:
                printf("\n Leader is changing lane");
                break;
            case LEADER_LEFT:
                printf("\n Leader is leaving");
                break;
            case JOIN_PLATOON:
                g_followerCount = receivedFrame->value; // assign no of clients to new clients position
                printf("\n New client %d has joined at Position %d", receivedFrame->param, receivedFrame->value);
                break;
            case LEAVE_PLATOON:
                g_followerCount--; //reduce no of clients
                printf("\n Client %d left platoon", receivedFrame->param);
                printf("\n Client %d-New position = %d ",truck.id,receivedFrame->value );
            default:
                printf("\n Undefined state");
        }
    }
    return NULL;
}
int main(int argc, char *argv[])
{

    client_state = e_active;
    WSADATA wsaData;
    SOCKET clientSocket;
    struct sockaddr_in serverAddr;
    int port = 8080;

    // Check command line arguments
    if (argc != 2)
    {
        printf("Usage: %s <velocity>\n", argv[0]);
        return 1;
    }

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("Failed to initialize Winsock.\n");
        return 1;
    }

    // Create socket
    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET){
        printf("Failed to create socket.\n");
        WSACleanup();
        return 1;
    }

    // Setup server address structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    int result = inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (result <= 0)
    {
        printf("Address not supported.\n");
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    // Connect to server
    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        printf("Connection to server failed.\n", WSAGetLastError());
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    truck.id = 0;
    truck.currentSpeed = atoi(argv[1]); // Only for initialisation
    truck.currentDistance = 0;  // TODO: get currentDistance from Server.
    
    // create a seperate thread for TX & RX similar to Embedded systems
    // we do it seperately because, if leader election has to be integrated - then a code should have both follower & leader part inside it.
    
    // Deciding leader while starting - based on who starts first instance, the leader has to be decided.
    // whoever joins next would be a follwoer & assigned an ID.
    // Deciding leader in runtime - if current elader exits, it nominates the next immediate truck.
    // that one truck alone, will close connection & start a socket, all others can join to that network again.


    pthread_t threadList[2];  
    
    pthread_create(&threadList[1], NULL,RXthread, &clientSocket);
    pthread_detach(threadList[1]);

    pthread_create(&threadList[0], NULL,TXthread, &clientSocket);
    pthread_detach(threadList[0]);

    while (client_state == e_active)
    { 
        // Kill logic
        sleep(5);        
    }
    //kill exits loop & destroys sockets to end program.
    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
