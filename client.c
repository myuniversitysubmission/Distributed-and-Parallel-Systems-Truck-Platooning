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

typedef enum {
    e_kill   = 0U,
    e_active = 1U
} state;

struct Truck truck;
volatile state client_state = e_active;

pthread_mutex_t truck_mutex = PTHREAD_MUTEX_INITIALIZER;


bool matchSpeed(struct Truck *t, int targetSpeed) {
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

    printf("Truck %d: Speed = %d\n", t->id, t->currentSpeed);

    pthread_mutex_unlock(&truck_mutex);
    return (t->currentSpeed == targetSpeed);
}

bool matchDistance(struct Truck *t, int targetDistance) {
    int distanceStep = 10;

    pthread_mutex_lock(&truck_mutex);

    if (t->currentPosition < targetDistance) {
        t->currentPosition += distanceStep;
        if (t->currentPosition > targetDistance)
            t->currentPosition = targetDistance;
    } else if (t->currentPosition > targetDistance) {
        t->currentPosition -= distanceStep;
        if (t->currentPosition < targetDistance)
            t->currentPosition = targetDistance;
    }

    printf("Truck %d: Position = %d\n", t->id, t->currentPosition);

    pthread_mutex_unlock(&truck_mutex);
    return (t->currentPosition == targetDistance);
}

void reportIntrusion(struct Truck *t) {
    int speed = 20;
    int step  = 5;

    pthread_mutex_lock(&truck_mutex);

    if (t->currentSpeed > speed) {
        t->currentSpeed -= step;
        if (t->currentSpeed < speed)
            t->currentSpeed = speed;

        printf("Truck %d: Intrusion detected! Slowing down to %d\n",
               t->id, t->currentSpeed);
    } else {
        printf("Truck %d: Intrusion active. Speed stabilized at %d\n",
               t->id, t->currentSpeed);
    }

    pthread_mutex_unlock(&truck_mutex);
}


void *TXthread(void *socketTXCopy) {
    SOCKET *SocketTX = (SOCKET *)socketTXCopy;

    int temp_case      = SPEED;
    int targetSpeed    = 80;
    int targetDistance = 100;

    while (client_state == e_active) {
        char *msg = NULL;

        matchSpeed(&truck, targetSpeed);
        matchDistance(&truck, targetDistance);

        switch (temp_case) {
        case INTRUSION:
            reportIntrusion(&truck);
            msg = constructMessage(
                truck.id,
                e_write,
                truck.currentSpeed,
                truck.currentPosition,
                INTRUSION
            );
            temp_case = SPEED;
            break;

        case SPEED:
            msg = constructMessage(
                truck.id,
                e_write,
                truck.currentSpeed,
                truck.currentPosition,
                SPEED
            );
            temp_case = DISTANCE;
            break;

        case DISTANCE:
            msg = constructMessage(
                truck.id,
                e_write,
                truck.currentSpeed,
                truck.currentPosition,
                DISTANCE
            );
            temp_case = SPEED;
            break;

        case EMERGENCY_BRAKE:
            msg = constructMessage(
                truck.id,
                e_write,
                0,
                truck.currentPosition,
                EMERGENCY_BRAKE
            );
            break;

        default:
            temp_case = SPEED;
            continue;
        }

        if (msg == NULL) {
            printf("Failed to construct frame message\n");
            closesocket(*SocketTX);
            return NULL;
        }

        int len = (int)strlen(msg);
        int bytesSent = send(*SocketTX, msg, len, 0);

        if (bytesSent == SOCKET_ERROR) {
            printf("Send error, closing TX thread\n");
            free(msg);
            client_state = e_kill;
            break;
        }

        free(msg);
        Sleep(500);
    }

    printf("TX thread exiting\n");
    return NULL;
}

/* ---------------- RX Thread ---------------- */

void *RXthread(void *socketRXCopy) {
    SOCKET *socketRX = (SOCKET *)socketRXCopy;
    char RXBuffer[1024];
    DataFrame *receivedFrame;

    while (client_state == e_active) {
        int bytesReceived =
            recv(*socketRX, RXBuffer, sizeof(RXBuffer) - 1, 0);

        if (bytesReceived > 0) {
            RXBuffer[bytesReceived] = '\0';
            receivedFrame = parseMessage(RXBuffer);

            if (receivedFrame == NULL) {
                printf("Invalid frame received\n");
                continue;
            }
        }

        if (bytesReceived == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                continue;
            else {
                printf("\nServer disconnected\n");
                closesocket(*socketRX);
                return NULL;
            }
        } else if (bytesReceived == 0) {
            printf("\nServer disconnected\n");
            closesocket(*socketRX);
            return NULL;
        }

        switch (receivedFrame->eventType) {
        case SPEED:
            matchSpeed(&truck, receivedFrame->speed);
            break;
        case DISTANCE:
            matchDistance(&truck, receivedFrame->distance);
            break;
        case INTRUSION:
            printf("\nIntrusion acknowledged");
            reportIntrusion(&truck);
            break;
        case LANE_CHANGE:
            printf("\nLeader is changing lane");
            break;
        case LEADER_LEFT:
            printf("\nLeader is leaving");
            break;
        default:
            printf("\nUndefined state");
        }
    }
    return NULL;
}


int main(int argc, char *argv[]) {
    WSADATA wsaData;
    SOCKET clientSocket;
    struct sockaddr_in serverAddr;
    int port = 8080;

    if (argc != 3) {
        printf("Usage: %s <truck_id> <velocity>\n", argv[0]);
        return 1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Failed to initialize Winsock.\n");
        return 1;
    }

    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        printf("Failed to create socket.\n");
        WSACleanup();
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(clientSocket,
                (struct sockaddr *)&serverAddr,
                sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Connection to server failed.\n");
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    truck.id              = atoi(argv[1]);
    truck.currentSpeed    = atoi(argv[2]);
    truck.currentPosition = 0;

    pthread_t threadList[2];

    pthread_create(&threadList[0], NULL, TXthread, &clientSocket);
    pthread_detach(threadList[0]);

    pthread_create(&threadList[1], NULL, RXthread, &clientSocket);
    pthread_detach(threadList[1]);

    while (client_state == e_active) {
        scanf("%d", &client_state);
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
