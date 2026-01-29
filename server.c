#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
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
#define PORT        8080
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
            df->param         = leftId; // left id

            if (g_clients[i]->client_position > leftPosition) {
                printf("\nclient[%d], pos_existing = %d",
                       g_clients[i]->client_id, g_clients[i]->client_position);
                g_clients[i]->client_position = g_clients[i]->client_position - 1;
                printf("\nclient[%d], pos_new = %d",
                       g_clients[i]->client_id, g_clients[i]->client_position);
            }
            df->value = g_clients[i]->client_position; // new position
            printf("\n i= %d, client_id=%d, leftID+1= %d, new_pos =%d, ",
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
        printf("Accept failed.\n");
        closesocket(*tempClient);
        return NULL;
    }
    // Set the client socket to non-blocking mode
    if (ioctlsocket(*tempClient, FIONBIO, &modeConnection) < 0) {
        perror("fcntl failed");
        closesocket(*tempClient);
        exit(EXIT_FAILURE);
    }

    printf("Client connected.\n");
    memset(RXBuffer, 0, sizeof(RXBuffer));

    while (1) {
        int bytesReceived = recv(*tempClient, RXBuffer, sizeof(RXBuffer) - 1, 0);

        if (bytesReceived > 0) {
            printf("\n Printing from %u : %s", tempArgument->client_id, RXBuffer);

            receivedFrame = parseMessage(RXBuffer);
            if (!receivedFrame) {
                continue;
            }

            /* --- EMERGENCY_BRAKE HANDLING --- */
            if (receivedFrame->eventType == EMERGENCY_BRAKE) {
                printf("\n EMERGENCY_BRAKE received from client %u\n",
                       tempArgument->client_id);

                urgentBrakeAll();   // tüm client'lere emergency brake yayınla

                free(receivedFrame);
                receivedFrame = NULL;
                continue;
            }

            /* --- INTRUSION HANDLING --- */
            if (receivedFrame->eventType == INTRUSION) {
                int reportedSpeed = receivedFrame->param;   // client currentSpeed
                int reportedDist  = receivedFrame->value;   // client currentDistance

                // Basit politika: hızı düşür, mesafeyi artır
                int safeSpeed = reportedSpeed - 20;
                if (safeSpeed < 20) {
                    safeSpeed = 20;  // minimum hız
                }
                int safeDist = reportedDist + 50;  // mesafeyi aç

                // Çok kritik mesafe ise emergency de tetikleyebilirsin
                if (reportedDist < 5) {
                    printf("\n CRITICAL intrusion from client %u, triggering EMERGENCY BRAKE\n",
                           tempArgument->client_id);
                    urgentBrakeAll();
                }

                // 1) SPEED komutu
                DataFrame *speedCmd = (DataFrame *)malloc(sizeof(DataFrame));
                if (speedCmd) {
                    speedCmd->truck_id      = tempArgument->client_id;
                    speedCmd->eventType     = SPEED;
                    speedCmd->readWriteFlag = e_write;
                    speedCmd->param         = 0;
                    speedCmd->value         = safeSpeed;
                    TxQueue_push(&tempArgument->txQueue, speedCmd);
                }

                // 2) DISTANCE komutu
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
                continue;
            }

            /* --- SPEED / DISTANCE: sadece telemetri, geri yollama --- */
            if (receivedFrame->eventType == SPEED || receivedFrame->eventType == DISTANCE) {
                // Telemetry only, don't echo back
                free(receivedFrame);
                receivedFrame = NULL;
                continue;
            }

            /* --- Diğer event'ler normal şekilde TX kuyruğuna --- */
            TxQueue_push(&tempArgument->txQueue, receivedFrame);
            receivedFrame = NULL;
        }

        if (bytesReceived == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                // no data yet, continue loop
                continue;
            } else {
                int id = tempArgument->client_id;

                printf("\n Client %d disconnected\n", id);
                id--; // making id from normal terms to arrays
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

            printf("\n Client %d disconnected\n", id);

            id--; // making id from normal terms to arrays
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

        char *frame;
        if (msg.eventType == SHUTDOWN) {
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
            printf("\n Client ID - %d Undefined RX case", ci->client_id);
            frame = NULL;
            break;
        }

        if (frame) {
            send(s, frame, (int)strlen(frame), 0);
            free(frame);
        }
    }
    printf("Client RX thread destroyed");
    return NULL;
}

void urgentBrakeAll(void)
{
    printf("Urgent brake applied to ALL clients!\n");

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
        df->value         = 0;   // extra info istersen doldur

        TxQueue_push(&g_clients[i]->txQueue, df);
    }
}


int main(void)
{
#ifndef USE_LINUX
    // winsocket initialisation
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Failed to initialize Winsock.\n");
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
        printf("Failed to create socket.\n");
#ifndef USE_LINUX
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }

    // 2. Binding socket to an address
    if (bind(server_fd, (struct sockaddr *)&server_addr, addr_len) == SOCKET_ERROR) {
        printf("Bind failed.\n");
        closesocket(server_fd);
#ifndef USE_LINUX
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }

    // Set the server socket to non-blocking mode
    if (ioctlsocket(server_fd, FIONBIO, &mode) < 0) {
        perror("fcntl failed");
        closesocket(server_fd);
        exit(EXIT_FAILURE);
    }

    // 3. Listen for incoming connections
    if (listen(server_fd, 40) == SOCKET_ERROR) {
        printf("Listen failed.\n");
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    printf("Server is listening on port %d...\n", PORT);

    while (1) {
        newSocket = accept(server_fd, (struct sockaddr *)&clientAddr, &addr_len);
        if (newSocket == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                DO_NOTHING // no new connection yet, continue to main loop
            } else {
                printf("accept failed: %d\n", err);
                break;
            }
        } else {
            int id = assignFreeID();
            if (id == -1) {
                printf("Max clients reached\n");
                closesocket(newSocket);
                continue;
            }

            clientInfo *newClient  = (clientInfo *)malloc(sizeof(clientInfo));
            newClient->socClient   = (SOCKET *)malloc(sizeof(SOCKET));
            *(newClient->socClient) = newSocket;
            g_clients[id]           = newClient;   // for broadcasting
            newClient->client_id    = ++id;
            printf("\n truck id= %d", id);
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

            printf("New connection, socket fd is %d\n", newSocket);
            usleep(1000); // Sleep briefly to avoid busy-waiting

            pthread_t rxThread, txThread;

            pthread_create(&txThread, NULL, ServerTxHandler, (void *)newClient);
            pthread_detach(txThread);

            pthread_create(&rxThread, NULL, ServerRxHandler, (void *)newClient);
            pthread_detach(rxThread);
        }
    }

    // Clean up
    closesocket(server_fd);
    WSACleanup();

    return 0;
}
