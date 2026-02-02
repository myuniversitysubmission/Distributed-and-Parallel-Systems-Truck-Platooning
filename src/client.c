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
#include "logical_clock.h"  

#pragma comment(lib, "Ws2_32.lib")

typedef enum {
    e_kill   = 0U,
    e_active = 1U
} state;

#define DO_NOTHING \
    {              \
        ;          \
    }

struct Truck truck;
volatile state client_state = e_active;
//pthread_mutex__t truck_mutex = //pthread_mutex__INITIALIZER;

// Logical clock for client
static MatrixClock g_lc;
static int g_lc_initialized = 0;

// Global hedefler (server'dan gelen komutlarla değişecek)
int g_targetSpeed    = 80;
int g_targetDistance = 100;

bool matchSpeed(struct Truck *t, int targetSpeed)
{
    int step = 5;

    //pthread_mutex__lock(&truck_mutex);

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

    //pthread_mutex__unlock(&truck_mutex);
    return (t->currentSpeed == targetSpeed);
}

bool matchDistance(struct Truck *t, int targetDistance)
{
    int distanceStep = 10;

    //pthread_mutex__lock(&truck_mutex);

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

    //pthread_mutex__unlock(&truck_mutex);
    return (t->currentDistance == targetDistance);
}

void reportIntrusion(struct Truck *t)
{
    int speed = 20;
    int step  = 5;

    //pthread_mutex__lock(&truck_mutex);

    if (t->currentSpeed > speed) {
        t->currentSpeed -= step;
        if (t->currentSpeed < speed) {
            t->currentSpeed = speed;
        }
        printf("Truck %d: Intrusion detected! Slowing down to %d\n",
               t->id, t->currentSpeed);
    } else {
        printf("Truck %d: Intrusion active. Speed stabilized at %d\n",
               t->id, t->currentSpeed);
    }

    //pthread_mutex__unlock(&truck_mutex);
}

void *TXthread(void *socketTXCopy)
{
    SOCKET *SocketTX = (SOCKET *)socketTXCopy;

    int  temp_case                 = SPEED;
    bool intrusionReported         = false;
    bool platoonStable             = false;
    bool intrusionScenarioInjected = false;  // intrusion testi için "fizik" simulasyonu
    bool emergencyReported         = false;  // emergency sadece bir kez raporlansın

    while (client_state == e_active) {
        char *msg = NULL;

        // 1) Hedef hız/mesafeye doğru yürüt
        bool speedOk    = matchSpeed(&truck, g_targetSpeed);
        bool distanceOk = matchDistance(&truck, g_targetDistance);

        // 2) Platoon'un stabilize olduğu an
        if (!platoonStable && speedOk && distanceOk) {
            platoonStable = true;
            printf("\n Truck %d: Platoon STABLE at speed=%d, distance=%d\n",
                   truck.id, truck.currentSpeed, truck.currentDistance);
        }

        // 3) "Gerçek dünya" intrusion simulasyonu:
        //    Platoon stable olduktan sonra, bir kere mesafeyi aniden düşür (araya araç girmiş gibi)
        if (platoonStable && !intrusionScenarioInjected) {
            //pthread_mutex__lock(&truck_mutex);
            truck.currentDistance = 10;   // intrusion tetiklenebilecek kadar yakın
            //pthread_mutex__unlock(&truck_mutex);

            printf("\n Truck %d: SIMULATED PHYSICAL INTRUSION -> distance DROPPED to %d\n",
                   truck.id, truck.currentDistance);

            intrusionScenarioInjected = true;
        }

        // 4) EMERGENCY tetikleyici (daha kritik, önce kontrol et)
        if (platoonStable && !emergencyReported && truck.currentDistance < 20) {
            temp_case = EMERGENCY_BRAKE;
        }
        // 5) Normal intrusion tetikleyici
        else if (platoonStable && !intrusionReported && truck.currentDistance < 30) {
            temp_case = INTRUSION;
        }

        // 6) Frame seçimi ve gönderimi
        switch (temp_case) {
        case INTRUSION:
            // Lokal refleks: server cevabı gelene kadar hızını biraz düşür
            reportIntrusion(&truck);

            msg = constructMessage(
                truck.id,               // truck_id
                e_write,                // rw = write
                truck.currentSpeed,     // param: currentSpeed (server için telemetri)
                truck.currentDistance,  // value: currentDistance
                INTRUSION               // eventType
            );
            intrusionReported = true;   // intrusion sadece bir kez raporlanacak
            temp_case = SPEED;          // sonraki loop'ta normal akışa dön
            break;

        case SPEED:
            msg = constructMessage(
                truck.id,               // truck_id
                e_write,                // rw = write
                truck.currentSpeed,
                truck.currentDistance,
                SPEED                   // eventType
            );
            //printf("\nis this working");
            temp_case = DISTANCE;
            break;

        case DISTANCE:
            msg = constructMessage(
                truck.id,               // truck_id
                e_write,                // rw = write
                truck.currentSpeed,
                truck.currentDistance,
                DISTANCE                // eventType
            );
            temp_case = SPEED;
            break;

        case EMERGENCY_BRAKE:
            // Lokal olarak hedef hızı 0'a çek
            //pthread_mutex__lock(&truck_mutex);
            g_targetSpeed = 0;
            //pthread_mutex__unlock(&truck_mutex);

            printf("\n Truck %d: LOCAL EMERGENCY TRIGGERED, reporting to server\n",
                   truck.id);

            msg = constructMessage(
                truck.id,
                e_write,
                0,                      // param: speed=0 (bilgi amaçlı)
                truck.currentDistance,  // value: o anki mesafe
                EMERGENCY_BRAKE
            );
            emergencyReported = true;   // bir kere raporla yeter
            temp_case = SPEED;          // akış SPEED/DISTANCE ile devam eder (hedef hız 0)
            break;

        case CLIENT_LEFT:
            printf("\n Some client left");
            // şu an özel frame göndermiyoruz
            break;

        default:
            temp_case = SPEED;
            printf("\n TX - default case");
            continue;
        }

        char *full = NULL;
        // printf("\n initiated? =%d",g_lc_initialized );
        if (g_lc_initialized) {
            //pthread_mutex__lock(&g_lc.mutex);
            lc_inc_local(&g_lc); // local send event
            char *mat = lc_serialize_matrix(&g_lc);
            if (mat) {
                int newlen = (int)strlen(msg) + (int)strlen(mat) + 4;
                full = (char *)malloc(newlen);
                if (full) {
                    snprintf(full, newlen, "%s|%s", msg, mat);
                }
                free(mat);
            }
            // printf("\n print 1(TX after messageConstruction), %d=init status", g_lc_initialized);
            lc_print(&g_lc);
            //pthread_mutex__unlock(&g_lc.mutex);
        }

        // Choose buffer to send (full matrix appended if available)
        char *sendBuf = (full != NULL) ? full : msg;
        int len = (int)strlen(sendBuf);
        int bytesSent = send(*SocketTX, sendBuf, len, 0);

        //socket error handling
        if (bytesSent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                // no data yet, continue loop
            } else {
                printf("\n TX-Server disconnected\n");
                free(msg);
                client_state = e_kill;
                closesocket(*SocketTX);
                return NULL;
            }
        } else if (bytesSent == 0) {
            printf("\n TX-Server disconnected\n");
            free(msg);
            client_state = e_kill;
            closesocket(*SocketTX);
            WSACleanup();
        }

        free(msg);
        if (full) free(full);

        sleep(5); // Sleep briefly to avoid busy-waiting
    }

    printf("TX thread exiting\n");
    return NULL;
}

void client_apply_emergency_brake(void)
{
    //pthread_mutex__lock(&truck_mutex);
    g_targetSpeed      = 0;
    truck.currentSpeed = 0;
    //pthread_mutex__unlock(&truck_mutex);
}

void *RXthread(void *socketRXCopy)
{
    SOCKET *socketRX = (SOCKET *)socketRXCopy;
    char RXBuffer[1024];
    DataFrame *receivedFrame;

    while (client_state == e_active) {
        //(1) Receive message
        int bytesReceived = recv(*socketRX, RXBuffer, sizeof(RXBuffer) - 1, 0);

        //(2) parse message / check error in message
        if (bytesReceived > 0) {
            printf("\n[RX] From server : %s", RXBuffer);
            RXBuffer[bytesReceived] = '\0';
            receivedFrame = parseMessage(RXBuffer);

            if (receivedFrame == NULL) {
                printf("Invalid frame received\n");
                continue;
            }
        }
        if (bytesReceived == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                // no data yet, continue loop
                continue;
            } else {
                printf("\n RX-Server disconnected\n");
                client_state = e_kill;
                closesocket(*socketRX);
                return NULL;
            }
        } else if (bytesReceived == 0) {
            printf("\n RX-Server disconnected\n");
            client_state = e_kill;
            closesocket(*socketRX);
            return NULL;
        }

        //(3) execute state_machine
        switch (receivedFrame->eventType) {
        case CLIENT_ID:
            truck.id = receivedFrame->truck_id;
            truck.position = receivedFrame->value;
            printf("Received Truck ID = %u, Position = %u", truck.id, truck.position);

                        // initialize logical clock once we know our id
            lc_init(&g_lc, LOGICAL_MAX_NODES, truck.id);
            g_lc_initialized = 1;

            // also parse server matrix if present in raw buffer
            {
                char *p = strstr(RXBuffer, "MATRIX:");
                if (p) {
                    p += strlen("MATRIX:");
                    //pthread_mutex__lock(&g_lc.mutex);
                    lc_merge_matrix_from_str(&g_lc, p); // server id = 0 inside matrix
                    lc_inc_local(&g_lc); // receive event
                    // printf("\n print 2(rx,recvd client id), %d=init status", g_lc_initialized);
                    lc_print(&g_lc);     // print matrix after receiving initial ID
                    //pthread_mutex__unlock(&g_lc.mutex);

                }
            }
            break;

        case SPEED:
            g_targetSpeed = receivedFrame->value;
            printf("\n Truck %d: New target SPEED from server = %d\n",
                   truck.id, g_targetSpeed);
            break;

        case DISTANCE:
            g_targetDistance = receivedFrame->value;
            printf("\n Truck %d: New target DISTANCE from server = %d\n",
                   truck.id, g_targetDistance);
            break;

        case INTRUSION:
            printf("\n Intrusion acknowledged");
            break;

        case EMERGENCY_BRAKE:
            printf("\n Truck %d: EMERGENCY BRAKE! Speed forced to 0\n", truck.id);
            break;

        case LANE_CHANGE:
            printf("\n Leader is changing lane");
            break;

        case LEADER_LEFT:
            printf("\n Leader is leaving");
            break;

        case CLIENT_LEFT:
            printf("\n Client %d left platoon", receivedFrame->param);
            printf("\n Client %d-New position = %d ", truck.id, receivedFrame->value);
            if (g_lc_initialized) {
                // receivedFrame->param contains leftId (same ID space as matrix)
                //pthread_mutex__lock(&g_lc.mutex);
                lc_reset_node(&g_lc, receivedFrame->param);
                //printf("\n print 3(some client left), %d=init status", g_lc_initialized);
                lc_print(&g_lc);
                //pthread_mutex__unlock(&g_lc.mutex);
            }
            break;

        default:
            printf("\n Undefined state");
        }
        
        if (g_lc_initialized) {
            char *p = strstr(RXBuffer, "MATRIX:");
            if (p) {
                p += strlen("MATRIX:");
                //pthread_mutex__lock(&g_lc.mutex);
                lc_merge_matrix_from_str(&g_lc, p);
                lc_inc_local(&g_lc); // receive event
                //printf("\n print 4(RX@ end), %d=init status", g_lc_initialized);
                lc_print(&g_lc);     // print matrix after receive
                //pthread_mutex__unlock(&g_lc.mutex);
            }
        }
    }
    return NULL;
}

#if defined(BUILD_CLIENT)
int main(int argc, char *argv[])
{
    client_state = e_active;
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

    truck.id             = 0;
    truck.currentSpeed   = atoi(argv[1]); // Only for initialisation
    truck.currentDistance = 0;            // TODO: get currentDistance from Server.

    pthread_t threadList[2];

    pthread_create(&threadList[1], NULL, RXthread, &clientSocket);
    pthread_detach(threadList[1]);

    pthread_create(&threadList[0], NULL, TXthread, &clientSocket);
    pthread_detach(threadList[0]);

    while (client_state == e_active) {
        // Kill logic
        sleep(5);
    }

    //kill exits loop & destroys sockets to end program.
    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
#endif

