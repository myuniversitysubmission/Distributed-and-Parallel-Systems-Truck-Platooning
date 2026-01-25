#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unistd.h>
#include <stdbool.h>
#include "truck.h"
#include "frame.h"

#pragma comment(lib, "Ws2_32.lib")


bool matchSpeed(struct Truck *t, int targetSpeed)
{
    t->currentSpeed = targetSpeed;
    printf("Truck %d: Speed matched to %d\n", t->id, t->currentSpeed);

    char *msg = constructMessage(t->id, 1, t->currentSpeed, t->currentPosition, TELEMETRY);

    if (msg == NULL) {
        printf("matchSpeed: constructMessage failed\n");
        return false;
    } else {
        printf("matchSpeed: constructed message: %s", msg);
    }

    free(msg);

    return true;
}

bool matchDistance(struct Truck *t, int targetDistance)
{
    
    t->currentPosition = targetDistance;
    printf("Truck %d: position matched to %d\n",t->id, t->currentPosition);
           
    char *msg = constructMessage(t->id, 1, t->currentSpeed, t->currentPosition, TELEMETRY);
    if (msg == NULL) {
        printf("matchDistance: constructMessage failed\n");
        return false;
    } else {
        printf("matchDistance: constructed message: %s", msg);
    }

    free(msg);
    return true;
}


void reportIntrusion(struct Truck *t)
{
    
    t->currentSpeed = 20;
    printf("Truck %d: Intrusion detected! Speed reduced to %d\n",t->id, t->currentSpeed);
    
    char *msg = constructMessage(t->id, 1, t->currentSpeed, t->currentPosition, INTRUSION);
    if(msg == NULL) {
        printf("reportIntrusion: constructMessage failed\n");
    } else {
        printf("reportIntrusion: constructed message: %s", msg);
    }

    free(msg);
}

int main(int argc, char *argv[]){

    WSADATA wsaData;
    SOCKET clientSocket;
    struct sockaddr_in serverAddr;
    int port = 8080;

    // Check command line arguments
    if (argc != 2) {
        printf("Usage: %s <velocity>\n", argv[0]);
        return 1;
    }

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Failed to initialize Winsock.\n");
        return 1;
    }

    // Create socket
    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        printf("Failed to create socket.\n");
        WSACleanup();
        return 1;
    }

    // Setup server address structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    int result = inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (result <= 0) {
        printf("Address not supported.\n");
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    // Connect to server
    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Connection to server failed.\n", WSAGetLastError());
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }    


    struct Truck truck;
    truck.id = 1;                          
    truck.currentSpeed = atoi(argv[1]);    
    truck.currentPosition = 0;             

    while(1){
        char *msg = constructMessage(
            truck.id,              // truck_id
            1,                     // rw = write
            truck.currentSpeed,    // velocity
            truck.currentPosition, // distance
            TELEMETRY              // eventType
        );

        if (msg == NULL) {
            printf("Failed to construct frame message\n");
            break;
        }

        int len = (int)strlen(msg);

        int bytesSent = send(clientSocket, msg, len, 0);

        free(msg);

        if (bytesSent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                continue;
            } else {
                printf("\n Server disconnected or send error (%d)\n", err);
                closesocket(clientSocket);
                WSACleanup();
                return -1;
            }
        } else if (bytesSent == 0) {
            printf("\n Server disconnected\n");
            closesocket(clientSocket);
            WSACleanup();
            return -1;
        } else {
            printf("Sent %d bytes: %d;%d;time;event;v;d ...\n", bytesSent, truck.id, truck.currentSpeed);
        }

        sleep(5);
    }

    closesocket(clientSocket);
    WSACleanup();
}