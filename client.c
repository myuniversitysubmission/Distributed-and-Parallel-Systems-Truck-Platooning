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
state client_state = e_active;

bool matchSpeed(struct Truck *t, int targetSpeed)
{
    t->currentSpeed = targetSpeed;
    printf("\n Truck %d: Speed matched to %d\n", t->id, t->currentSpeed);
    return true;
}

bool matchDistance(struct Truck *t, int targetDistance)
{

    t->currentDistance = targetDistance;
    printf("Truck %d: Distance matched to %d\n", t->id, t->currentDistance);

    return true;
}

void reportIntrusion(struct Truck *t)
{

    t->currentSpeed = 20;
    printf("Truck %d: Intrusion detected! Speed reduced to %d\n", t->id, t->currentSpeed);

    // char *msg = constructMessage(t->id, 1, t->currentSpeed, t->currentDistance, INTRUSION);
    // if (msg == NULL)
    // {
    //     printf("reportIntrusion: constructMessage failed\n");
    // }
    // else
    // {
    //     printf("reportIntrusion: constructed message: %s", msg);
    // }
}

void *TXthread(void* socketTXCopy){
    // construct a message()
    // SendMessage()
    // check Error in message()
    int temp_case = INTRUSION;
    while(client_state == e_active){
        SOCKET* SocketTX = (SOCKET*) socketTXCopy;
        char *msg = NULL;
        switch (temp_case)
        {
        case INTRUSION:
            msg = constructMessage(
                truck.id,               // truck_id
                e_read,                 // rw = write
                0,                   // reporting intrusion
                0,                   // reporting intrusion
                INTRUSION               // eventType
            );
            temp_case = SPEED;
            break;

        case SPEED:
            msg = constructMessage(
                truck.id,              // truck_id
                e_read,                     // rw = write
                0,    // reading speed
                0, // reading speed
                SPEED              // eventType
            );
            temp_case = DISTANCE;
            break;

        case DISTANCE:
            msg = constructMessage(
                truck.id,              // truck_id
                e_read,               // rw = write
                0,    // reading distance
                0, // reading distance
                DISTANCE              // eventType
            );
            temp_case = -1;
            break;

        case CLIENT_LEFT:
            printf("\n Some client left");
            DO_NOTHING
            break;
        default:
            printf("\n TX - Undefined case");
            closesocket(*SocketTX);
            free(msg);
            return NULL;
        }

        if (msg == NULL)
        {
            printf("Failed to construct frame message\n");
            closesocket(*SocketTX);
            return (NULL);
        }

        int len = (int)strlen(msg);
        int bytesSent = send(*SocketTX, msg, len, 0);
        free(msg);

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
                closesocket(*SocketTX);
                return (NULL);
            }
        }
        else if (bytesSent == 0)
        {
            printf("\n TX-Server disconnected\n");
            closesocket(*SocketTX);
            WSACleanup();
        }
        else
        {
            printf("\n Sent %d bytes: %d;%d;time;event;v;d ...\n", bytesSent, truck.id, truck.currentSpeed);
        }

        sleep(5); // Sleep briefly to avoid busy-waiting
    }
}

void *RXthread(void* socketRXCopy){
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
                break;
            case LANE_CHANGE:
                printf("\n Leader is changing lane");
                break;
            case LEADER_LEFT:
                printf("\n Leader is leaving");
                break;
            case CLIENT_LEFT:
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
    if (clientSocket == INVALID_SOCKET)
    {
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

}