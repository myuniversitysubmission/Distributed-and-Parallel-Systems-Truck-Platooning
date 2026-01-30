#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <windows.h>    // for QueryPerformanceCounter
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unistd.h>
#include <fcntl.h>

#include "truck.h"
#include "frame.h"
#include "queue.h"

#pragma comment(lib, "Ws2_32.lib")

// Forward declaration
void urgentBrakeAll(void);

struct ip_addr
{
    unsigned long S_addr;
};

typedef struct s_clientInfo
{
    SOCKET *socClient;
    unsigned int client_id      : 3;
    unsigned int client_position: 3;
    TxQueue txQueue;
} clientInfo;

// Macros
#define PORT         8080
#define NON_BLOCKING 1
#define BLOCKING     0
#define MAX_CLIENTS  7U
#define SHUTDOWN     999U

#define DO_NOTHING \
    {              \
        ;          \
    }

// Globals
u_long mode           = NON_BLOCKING;
u_long modeConnection = BLOCKING;
unsigned int g_followerCount = 0;
clientInfo *g_clients[MAX_CLIENTS] = {0};

// High-resolution timer globals (for WCET measurement)
LARGE_INTEGER g_perfFreq;
double g_max_rx_ms    = 0.0;
double g_max_tx_ms    = 0.0;
double g_max_emerg_ms = 0.0;

// Helper: current time in milliseconds (high resolution)
static double now_ms(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)g_perfFreq.QuadPart;
}

// To find out free spots in the platoon
int assignFreeID(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] == NULL) {
            g_followerCount++;
            return i;
        }
    }
    return -1;
}

// To inform all other clients if one client left the platoon. Used for clock matrix
void broadcastClientLeft(unsigned int leftId, unsigned int leftPosition)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] != NULL && g_clients[i]->client_id != leftId) {
            DataFrame *df = (DataFrame *)malloc(sizeof(DataFrame));
            if (!df) continue;

            df->truck_id      = g_clients[i]->client_id;
            df->eventType     = CLIENT_LEFT;
            df->readWriteFlag = e_write;
            df->param         = leftId; // ID of the truck that left

            if (g_clients[i]->client_position > leftPosition) {
                printf("\n[INFO] Client[%d], old position = %d",
                       g_clients[i]->client_id, g_clients[i]->client_position);
                g_clients[i]->client_position = g_clients[i]->client_position - 1;
                printf("\n[INFO] Client[%d], new position = %d",
                       g_clients[i]->client_id, g_clients[i]->client_position);
            }
            df->value = g_clients[i]->client_position; // new position
            printf("\n[INFO] i = %d, client_id = %d, leftID+1 = %d, new_pos = %d",
                   i, g_clients[i]->client_id, leftId + 1, g_clients[i]->client_position);

            TxQueue_push(&g_clients[i]->txQueue, df);
        }
    }
}

void *ServerRxHandler(void *clientInfoRxCopy)
{
    char RXBuffer[1024];
    DataFrame *receivedFrame = NULL;

    clientInfo *tempArgument = (clientInfo *)clientInfoRxCopy;
    SOCKET *tempClient       = tempArgument->socClient;
    if (*tempClient == INVALID_SOCKET) {
        printf("[ERROR] Accept failed.\n");
        closesocket(*tempClient);
        return NULL;
    }
    // Set the client socket to non-blocking mode
    if (ioctlsocket(*tempClient, FIONBIO, &modeConnection) < 0) {
        perror("[ERROR] ioctlsocket failed");
        closesocket(*tempClient);
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Client connected (ID = %u).\n", tempArgument->client_id);
    memset(RXBuffer, 0, sizeof(RXBuffer));

    while (1) {
        int bytesReceived = recv(*tempClient, RXBuffer, sizeof(RXBuffer) - 1, 0);

        if (bytesReceived > 0) {
            double start_ms = now_ms();   // start RX job timing

            printf("\n[RX] From client %u : %s", tempArgument->client_id, RXBuffer);

            receivedFrame = parseMessage(RXBuffer);
            if (!receivedFrame) {
                double end_ms = now_ms();
                double elapsed_ms = end_ms - start_ms;
                if (elapsed_ms > g_max_rx_ms) {
                    g_max_rx_ms = elapsed_ms;
                }
                printf("[WCET DEBUG] RX job (parse fail) took %.6f ms (max so far = %.6f ms)\n",
                       elapsed_ms, g_max_rx_ms);
                continue;
            }

            /* --- EMERGENCY_BRAKE HANDLING --- */
            if (receivedFrame->eventType == EMERGENCY_BRAKE) {
                printf("\n[INFO] EMERGENCY_BRAKE received from client %u\n",
                       tempArgument->client_id);

                urgentBrakeAll();   // broadcast emergency brake to all clients

                free(receivedFrame);
                receivedFrame = NULL;

                double end_ms = now_ms();
                double elapsed_ms = end_ms - start_ms;
                if (elapsed_ms > g_max_rx_ms) {
                    g_max_rx_ms = elapsed_ms;
                }
                printf("[WCET DEBUG] RX job (EMERGENCY_BRAKE) took %.6f ms (max so far = %.6f ms)\n",
                       elapsed_ms, g_max_rx_ms);
                continue;
            }

            /* --- INTRUSION HANDLING --- */
            if (receivedFrame->eventType == INTRUSION) {
                int reportedSpeed = receivedFrame->param;   // client currentSpeed
                int reportedDist  = receivedFrame->value;   // client currentDistance

                // Simple policy: decrease speed, increase distance
                int safeSpeed = reportedSpeed - 20;
                if (safeSpeed < 20) {
                    safeSpeed = 20;  // minimum speed
                }
                int safeDist = reportedDist + 50;  // increase distance

                // Critical distance => also trigger emergency brake
                if (reportedDist < 5) {
                    printf("\n[WARN] CRITICAL intrusion from client %u, triggering EMERGENCY BRAKE\n",
                           tempArgument->client_id);
                    urgentBrakeAll();
                }

                // 1) SPEED command
                DataFrame *speedCmd = (DataFrame *)malloc(sizeof(DataFrame));
                if (speedCmd) {
                    speedCmd->truck_id      = tempArgument->client_id;
                    speedCmd->eventType     = SPEED;
                    speedCmd->readWriteFlag = e_write;
                    speedCmd->param         = 0;
                    speedCmd->value         = safeSpeed;
                    TxQueue_push(&tempArgument->txQueue, speedCmd);
                }

                // 2) DISTANCE command
                DataFrame *distCmd = (DataFrame *)malloc(sizeof(DataFrame));
                if (distCmd) {
                    distCmd->truck_id      = tempArgument->client_id;
                    distCmd->eventType     = DISTANCE;
                    distCmd->readWriteFlag = e_write;
                    distCmd->param         = 0;
                    distCmd->value         = safeDist;
                    TxQueue_push(&tempArgument->txQueue, distCmd);
                }

                free(receivedFrame);
                receivedFrame = NULL;

                double end_ms = now_ms();
                double elapsed_ms = end_ms - start_ms;
                if (elapsed_ms > g_max_rx_ms) {
                    g_max_rx_ms = elapsed_ms;
                }
                printf("[WCET DEBUG] RX job (INTRUSION) took %.6f ms (max so far = %.6f ms)\n",
                       elapsed_ms, g_max_rx_ms);
                continue;
            }

            /* --- SPEED / DISTANCE: telemetry only, do not echo back --- */
            if (receivedFrame->eventType == SPEED || receivedFrame->eventType == DISTANCE) {
                free(receivedFrame);
                receivedFrame = NULL;

                double end_ms = now_ms();
                double elapsed_ms = end_ms - start_ms;
                if (elapsed_ms > g_max_rx_ms) {
                    g_max_rx_ms = elapsed_ms;
                }
                printf("[WCET DEBUG] RX job (telemetry) took %.6f ms (max so far = %.6f ms)\n",
                       elapsed_ms, g_max_rx_ms);
                continue;
            }

            /* --- Other events: push to TX queue --- */
            TxQueue_push(&tempArgument->txQueue, receivedFrame);
            receivedFrame = NULL;

            double end_ms = now_ms();
            double elapsed_ms = end_ms - start_ms;
            if (elapsed_ms > g_max_rx_ms) {
                g_max_rx_ms = elapsed_ms;
            }
            printf("[WCET DEBUG] RX job (other) took %.6f ms (max so far = %.6f ms)\n",
                   elapsed_ms, g_max_rx_ms);
        }

        if (bytesReceived == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                // no data yet, continue loop
                continue;
            } else {
                int id = tempArgument->client_id;

                printf("\n[INFO] Client %d disconnected (SOCKET_ERROR)\n", id);
                id--; // convert to array index
                g_clients[id] = NULL; // remove from global list
                g_followerCount--;    // reduce active count

                if (receivedFrame == NULL) {
                    receivedFrame = (DataFrame *)malloc(sizeof(DataFrame));
                    if (!receivedFrame) {
                        closesocket(*tempClient);
                        free(tempArgument->socClient);
                        free(tempArgument);
                        return NULL;
                    }
                }
                receivedFrame->eventType = SHUTDOWN;
                TxQueue_push(&tempArgument->txQueue, receivedFrame);

                broadcastClientLeft(tempArgument->client_id, tempArgument->client_position);
                closesocket(*tempClient);
                free(tempArgument->socClient);
                free(tempArgument);
                return NULL;
            }
        } else if (bytesReceived == 0) {
            int id = tempArgument->client_id;

            printf("\n[INFO] Client %d disconnected (graceful)\n", id);

            id--; // convert to array index
            g_clients[id] = NULL; // remove from global list
            g_followerCount--;    // reduce active count

            if (receivedFrame == NULL) {
                receivedFrame = (DataFrame *)malloc(sizeof(DataFrame));
                if (!receivedFrame) {
                    closesocket(*tempClient);
                    free(tempArgument->socClient);
                    free(tempArgument);
                    return NULL;
                }
            }
            receivedFrame->eventType = SHUTDOWN;
            TxQueue_push(&tempArgument->txQueue, receivedFrame);

            broadcastClientLeft(tempArgument->client_id, tempArgument->client_position);
            closesocket(*tempClient);
            free(tempArgument->socClient);
            free(tempArgument);
            return NULL;
        }
    }
}

void *ServerTxHandler(void *clientInfoTxCopy)
{
    clientInfo *ci = (clientInfo *)clientInfoTxCopy;
    SOCKET s       = *(ci->socClient);
    DataFrame msg;

    while (1) {
        TxQueue_pop(&ci->txQueue, &msg);

        double start_ms = now_ms();  // start TX job timing

        char *frame;
        if (msg.eventType == SHUTDOWN) {
            double end_ms = now_ms();
            double elapsed_ms = end_ms - start_ms;
            if (elapsed_ms > g_max_tx_ms) {
                g_max_tx_ms = elapsed_ms;
            }
            printf("[WCET DEBUG] TX job (SHUTDOWN) took %.6f ms (max so far = %.6f ms)\n",
                   elapsed_ms, g_max_tx_ms);
            break;   // exit TX thread
        }

        switch (msg.eventType) {
        case INTRUSION:
            frame = constructMessage(
                ci->client_id,
                e_write,
                msg.param,
                msg.value,
                INTRUSION
            );
            break;

        case SPEED:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.param,
                msg.value,
                SPEED          // eventType
            );
            break;

        case DISTANCE:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.param,
                msg.value,
                DISTANCE       // eventType
            );
            break;

        case CLIENT_LEFT:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.param,     // id of the truck that left
                msg.value,     // new position
                CLIENT_LEFT
            );
            break;

        case EMERGENCY_BRAKE:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                0,             // speed = 0
                msg.value,     // distance or extra info
                EMERGENCY_BRAKE
            );
            break;

        default:
            printf("\n[WARN] Client ID %d: undefined TX eventType %d",
                   ci->client_id, msg.eventType);
            frame = NULL;
            break;
        }

        if (frame) {
            send(s, frame, (int)strlen(frame), 0);
            free(frame);
        }

        double end_ms = now_ms();
        double elapsed_ms = end_ms - start_ms;
        if (elapsed_ms > g_max_tx_ms) {
            g_max_tx_ms = elapsed_ms;
        }

        printf("[WCET DEBUG] TX job took %.6f ms (max so far = %.6f ms)\n",
               elapsed_ms, g_max_tx_ms);
    }
    printf("[INFO] TX thread for client %d terminated. Estimated WCET per message = %.6f ms\n",
           ci->client_id, g_max_tx_ms);
    printf("Client RX thread destroyed");
    return NULL;
}

void urgentBrakeAll(void)
{
    double start_ms = now_ms();   // start emergency task timing

    printf("[WARN] Urgent brake is being applied to ALL clients!\n");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] == NULL) {
            continue;
        }

        DataFrame *df = (DataFrame *)malloc(sizeof(DataFrame));
        if (!df) {
            continue;
        }

        df->truck_id      = g_clients[i]->client_id;
        df->eventType     = EMERGENCY_BRAKE;
        df->readWriteFlag = e_write;
        df->param         = 0;   // speed = 0
        df->value         = 0;   // extra info if needed

        TxQueue_push(&g_clients[i]->txQueue, df);
    }

    double end_ms = now_ms();
    double elapsed_ms = end_ms - start_ms;
    if (elapsed_ms > g_max_emerg_ms) {
        g_max_emerg_ms = elapsed_ms;
    }

    printf("[WCET DEBUG] urgentBrakeAll took %.6f ms (max so far = %.6f ms)\n",
           elapsed_ms, g_max_emerg_ms);
}


int main(void)
{
#ifndef USE_LINUX
    // high-resolution timer initialization
    if (!QueryPerformanceFrequency(&g_perfFreq)) {
        printf("[ERROR] High-resolution performance counter not supported.\n");
        return 1;
    }

    // winsocket initialisation
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("[ERROR] Failed to initialize Winsock.\n");
        return 1;
    }
#endif

    SOCKET server_fd, newSocket;
    struct sockaddr_in server_addr, clientAddr;

    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen to all interfaces
    server_addr.sin_family      = AF_INET;    // IPv4
    server_addr.sin_port        = htons(PORT);
    int addr_len                = sizeof(server_addr);

    // 1. Socket creation
    if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
        printf("[ERROR] Failed to create socket.\n");
#ifndef USE_LINUX
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }

    // 2. Binding socket to an address
    if (bind(server_fd, (struct sockaddr *)&server_addr, addr_len) == SOCKET_ERROR) {
        printf("[ERROR] Bind failed.\n");
        closesocket(server_fd);
#ifndef USE_LINUX
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }

    // Set the server socket to non-blocking mode
    if (ioctlsocket(server_fd, FIONBIO, &mode) < 0) {
        perror("[ERROR] ioctlsocket failed");
        closesocket(server_fd);
        exit(EXIT_FAILURE);
    }

    // 3. Listen for incoming connections
    if (listen(server_fd, 40) == SOCKET_ERROR) {
        printf("[ERROR] Listen failed.\n");
        closesocket(server_fd);
#ifndef USE_LINUX
        WSACleanup();
#endif
        return 1;
    }

    printf("[INFO] Server is listening on port %d...\n", PORT);

    while (1) {
        newSocket = accept(server_fd, (struct sockaddr *)&clientAddr, &addr_len);
        if (newSocket == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                DO_NOTHING // no new connection yet, continue to main loop
            } else {
                printf("[ERROR] accept failed: %d\n", err);
                break;
            }
        } else {
            int id = assignFreeID();
            if (id == -1) {
                printf("[WARN] Max clients reached, rejecting new connection\n");
                closesocket(newSocket);
                continue;
            }

            clientInfo *newClient   = (clientInfo *)malloc(sizeof(clientInfo));
            newClient->socClient    = (SOCKET *)malloc(sizeof(SOCKET));
            *(newClient->socClient) = newSocket;
            g_clients[id]           = newClient;   // for broadcasting
            newClient->client_id    = ++id;
            printf("\n[INFO] New truck connected, assigned ID = %d", id);
            newClient->client_position = g_followerCount;

            TxQueue_init(&newClient->txQueue);

            // Publish client ID to client
            char *clientIdFrame = constructMessage(
                newClient->client_id,
                e_write,
                newClient->client_id,
                newClient->client_position,
                CLIENT_ID
            );
            send(newSocket, clientIdFrame, (int)strlen(clientIdFrame), 0);
            free(clientIdFrame);

            printf("\n[INFO] New connection, socket fd is %d\n", newSocket);
            usleep(1000); // Sleep briefly to avoid busy-waiting

            pthread_t rxThread, txThread;

            pthread_create(&txThread, NULL, ServerTxHandler, (void *)newClient);
            pthread_detach(txThread);

            pthread_create(&rxThread, NULL, ServerRxHandler, (void *)newClient);
            pthread_detach(rxThread);
        }
    }

    // Print final WCET estimates before shutdown
    printf("\n[RESULT] Estimated WCETs (based on max measured values):\n");
    printf("         RX handler per frame      : %.6f ms\n", g_max_rx_ms);
    printf("         TX handler per message    : %.6f ms\n", g_max_tx_ms);
    printf("         urgentBrakeAll (broadcast): %.6f ms\n", g_max_emerg_ms);

    // Clean up
    closesocket(server_fd);
#ifndef USE_LINUX
    WSACleanup();
#endif

    return 0;
}
