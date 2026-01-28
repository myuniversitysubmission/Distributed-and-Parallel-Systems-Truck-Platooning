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


struct ip_addr
{
    unsigned long S_addr;
};

typedef struct s_clientInfo
{
    SOCKET *socClient;
    int client_id;
    TxQueue txQueue;
} clientInfo;

// Macros
#define PORT 8080
#define NON_BLOCKING 1
#define BLOCKING 0

// Globals
u_long mode = NON_BLOCKING;
u_long modeConnection = BLOCKING;
int id = 0;

void *ServerRxHandler(void *clientInfoRxCopy)
{
    char RXBuffer[1024];
    DataFrame *receivedFrame;

    clientInfo *tempArgument = (clientInfo *)clientInfoRxCopy;
    SOCKET *tempClient = tempArgument->socClient;
    if (*tempClient == INVALID_SOCKET)
    {
        printf("Accept failed.\n", WSAGetLastError());
        closesocket(*tempClient);
        return NULL;
    }
    // Set the server socket to non-blocking mode
    if (ioctlsocket(*tempClient, FIONBIO, &modeConnection) < 0)
    {
        perror("fcntl failed");
        closesocket(*tempClient);
        exit(EXIT_FAILURE);
    }

    printf("Client connected.\n");
    memset(RXBuffer, 0, sizeof(RXBuffer));
    while (1)
    {
        int bytesReceived = recv(*tempClient, RXBuffer, sizeof(RXBuffer) - 1, 0);
        // printf(" socket fd is  %d : %s",tempArgument->client_id,  RXBuffer);

        if (bytesReceived > 0)
        {
            printf("\n Printing from %d : %s", tempArgument->client_id + 1, RXBuffer);
            receivedFrame = parseMessage(RXBuffer);
            TxQueue_push(&tempArgument->txQueue, receivedFrame);
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
                printf("\n Client disconnected\n");
                closesocket(*tempClient);
                free(tempArgument->socClient);
                free(tempArgument);
                return NULL;
            }
            // real error
        }
        else if (bytesReceived == 0)
        {
            printf("\n Client disconnected\n");
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
    SOCKET s = *(ci->socClient);
    DataFrame msg;

    while (1)
    {
        TxQueue_pop(&ci->txQueue, &msg);

        char *frame;

        switch (msg.eventType)
        {
        case INTRUSION:
            frame = constructMessage(
                ci->client_id, 
                e_write,       
                msg.speed,             
                msg.distance,            
                INTRUSION      
            );
            break;
        case SPEED:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.speed,         // reading speed
                msg.distance,           // reading distance
                SPEED          // eventType
            );
            break;

        case DISTANCE:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.speed,      // reading distance
                msg.distance,           // reading distance
                DISTANCE       // eventType
            );
            break;
        case EMERGENCY_BRAKE:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                0,             // speed = 0
                msg.distance,           // reading distance
                EMERGENCY_BRAKE // eventType
            );
            break;
        default:
            break;
        }

        if (frame)
        {
            send(s, frame, strlen(frame), 0);
            free(frame);
        }
    }
}

void urgentBrakeAll(struct Truck *leader, SOCKET *clientSockets)
{
    leader->currentSpeed = 0;
    printf("Urgent brake applied to ALL! Leader speed = 0\n");
    int count = sizeof(clientSockets);

    for (int i = 0; i < count; i++)
    {
        SOCKET s = clientSockets[i];
        if (s == INVALID_SOCKET)
        {
            continue;
        }

        char *msg = constructMessage(leader->id, e_write, leader->currentSpeed,
                                     leader->currentPosition, EMERGENCY_BRAKE);

        if (msg == NULL)
        {
            printf("urgentBrakeAll: constructMessage failed for client %d\n", i);
            continue;
        }
        else
        {
            send(s, msg, strlen(msg), 0);
        }

        free(msg);
    }
}

int telematicComm(struct Truck *leader, SOCKET clientSocket, int speed, int distance)
{

    leader->currentSpeed = speed;
    leader->currentPosition = distance;

    char *msg = constructMessage(
        leader->id,
        e_write,
        leader->currentSpeed,
        leader->currentPosition,
        SPEED);

    return 0;
}

int main()
{
#ifndef USE_LINUX
    // winsocket initialisation
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        // fprintf(stderr, "WSAStartup failed with error: %d\n", GET_LAST_ERROR());
        printf("Failed to initialize Winsock.\n");
        return 1;
    }
#endif

    // server_fd is the so called channel, where data flows
    SOCKET server_fd, newSocket;

    struct sockaddr_in server_addr, clientAddr;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen to all interfaces
    server_addr.sin_family = AF_INET;         // IP_v4
    server_addr.sin_port = htons(PORT);
    int addr_len = sizeof(server_addr);

    // 1. Socket creation
    if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
    {

        printf("Failed to create socket.\n", WSAGetLastError());
// throwError("Error occured in socket creation");
//  Close WSA & exit out of program
#ifndef USE_LINUX
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }

    // 2. Binding socekt to a address

    if ((bind(server_fd, (struct sockaddr *)&server_addr, addr_len) == SOCKET_ERROR))
    {

        printf("Bind failed.\n", WSAGetLastError());
        // throwError("Bind error");
        closesocket(server_fd);
// Close WSA & exit out of program
#ifndef USE_LINUX
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }

    // Set the server socket to non-blocking mode
    if (ioctlsocket(server_fd, FIONBIO, &mode) < 0)
    {
        perror("fcntl failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /***********************************************************/

    // 3. Listen for incoming connections. A maximum of 50 ports
    if (listen(server_fd, 40) == SOCKET_ERROR)
    {
        printf("Listen failed.\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    printf("Server is listening on port %d...\n", PORT);

    // Receive data from client
    while (1)
    {
        newSocket = accept(server_fd, (struct sockaddr *)&clientAddr, &addr_len);
        if (newSocket == INVALID_SOCKET)
        {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
            {
                // no new connection yet
                continue;
            }
            printf("a 7 ccept failed: %d\n", err);
            break;
        }
        else
        {
            
            clientInfo *newClient = malloc(sizeof(clientInfo));
            newClient->socClient = malloc(sizeof(SOCKET));
            *(newClient->socClient) = newSocket;
            newClient->client_id = id++;
            TxQueue_init(&newClient->txQueue);

            printf("New connection, socket fd is %d\n", newSocket);
            usleep(1000); // Sleep briefly to avoid busy-waiting

            pthread_t rxThread, txThread;

            pthread_create(&rxThread, NULL, ServerRxHandler, (void *)newClient);
            pthread_detach(rxThread);

            pthread_create(&txThread, NULL, ServerTxHandler, (void *)newClient);
            pthread_detach(txThread);
        }
    }

    // Clean up
    closesocket(server_fd);
    WSACleanup();

    return 0;
}
