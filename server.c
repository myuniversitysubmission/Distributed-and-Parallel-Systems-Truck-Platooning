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
// What we want is to have clients reach server with client ID
// server will then post messages from them.
// If client kills itself server should recognise that.

#include "queue.h"

#pragma comment(lib, "Ws2_32.lib")

struct ip_addr
{
    unsigned long S_addr;
};

typedef struct s_clientInfo
{
    SOCKET *socClient;
    unsigned int client_id : 3;
    unsigned int client_position : 3;
    TxQueue txQueue;
} clientInfo;

// Macros
#define PORT 8080
#define NON_BLOCKING 1
#define BLOCKING 0
#define MAX_CLIENTS 7U
#define SHUTDOWN 999U

#define DO_NOTHING \
    {              \
        ;          \
    }

// Globals
u_long mode = NON_BLOCKING;
u_long modeConnection = BLOCKING;
unsigned int g_followerCount = 0;
clientInfo *g_clients[MAX_CLIENTS] = {0};

// To find out free spots in the platoon
int assignFreeID() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] == NULL) {
            g_followerCount++;
            return (i);
        }
    }
    return -1;
}

void broadcastToOne(clientInfo *ci, EventType eventType, unsigned int truckId, unsigned int param,
                    unsigned int value)
{
    DataFrame *df = malloc(sizeof(DataFrame));
    df->truck_id = truckId;
    df->eventType = eventType;
    df->readWriteFlag = e_write;
    df->param = param;
    df->value = value;

    TxQueue_push(&ci->txQueue, df);
}

void broadcastAll(EventType eventType, unsigned int param,
                  unsigned int value)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] != NULL) {
            broadcastToOne(g_clients[i], eventType, g_clients[i]->client_id, param, value);
        }
    }
}

void broadcastClientLeft(EventType eventType, unsigned int leftId, unsigned int leftPosition)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clientInfo *ci = g_clients[i];
        if (ci == NULL || ci->client_id == leftId)
            continue;

        // Update position
        if (ci->client_position > leftPosition)
            ci->client_position--;

        // Send customized message to THIS client
        broadcastToOne(ci,
                    eventType,
                    ci->client_id,          // Current client
                    leftId,                 // who left
                    ci->client_position);   // THIS client's new position
    }
}

// To inform all other clients if one client left the platoon. Used for clock matrix
/*void broadcast_join_left(unsigned int leftId, unsigned int leftPosition) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] != NULL && g_clients[i]->client_id != (leftId)) {

            if(g_clients[i]->client_position > leftPosition){
                printf("\nclient[%d], pos_existing = %d",g_clients[i]->client_id, g_clients[i]->client_position);
                g_clients[i]->client_position = g_clients[i]->client_position - 1;
                printf("\nclient[%d], pos_new = %d",g_clients[i]->client_id, g_clients[i]->client_position);
            }
            // (event, left_client_id, client[i]_new_position)
            // DataFrame *df = malloc(sizeof(DataFrame));
            // df->truck_id = g_clients[i]->client_id;
            // df->eventType = LEAVE_PLATOON;
            // df->readWriteFlag = e_write;
            // df->param = leftId; // leftid

            // df->value = g_clients[i]->client_position; //new position
            // printf("\n i= %d, client_id=%d, leftID+1= %d, new_pos =%d, ",i,g_clients[i]->client_id, leftId+1, g_clients[i]->client_position);

            // TxQueue_push(&g_clients[i]->txQueue, df);
        }
    }
}*/

void *ServerRxHandler(void *clientInfoRxCopy)
{
    // (2) Check point
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
            printf("\n Printing from %u : %s", tempArgument->client_id, RXBuffer);
            //TODO: printf("\n Printing from %d : %s", tempArgument->client_id + 1, RXBuffer);
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
                int id = tempArgument->client_id;

                printf("\n Client %d disconnected\n", id);
                id--; // making id from normal terms to arrays
                g_clients[id] = NULL; // remove from global list
                g_followerCount--;    // reduce active count
                receivedFrame->eventType = SHUTDOWN;
                TxQueue_push(&tempArgument->txQueue, receivedFrame);
                broadcastClientLeft(LEAVE_PLATOON, tempArgument->client_id, tempArgument->client_position); // broadcast to others
                closesocket(*tempClient);
                free(tempArgument->socClient);
                free(tempArgument);
                return NULL;
            }
            // real error
        }
        else if (bytesReceived == 0)
        {
            int id = tempArgument->client_id;
            
            printf("\n Client %d disconnected\n", id);

            id--;// making id from normal terms to arrays
            g_clients[id] = NULL; // remove from global list
            g_followerCount--;    // reduce active count
            receivedFrame->eventType = SHUTDOWN;
            TxQueue_push(&tempArgument->txQueue, receivedFrame);
            broadcastClientLeft(LEAVE_PLATOON, tempArgument->client_id, tempArgument->client_position); // broadcast to others
            closesocket(*tempClient);
            free(tempArgument->socClient);
            free(tempArgument);
            return NULL;
        }
    }
}

void *ServerTxHandler(void *clientInfoTxCopy)
{
    // (3) Decision making
    clientInfo *ci = (clientInfo *)clientInfoTxCopy;
    SOCKET s = *(ci->socClient);
    DataFrame msg;

    while (1)
    {
        TxQueue_pop(&ci->txQueue, &msg);

        char *frame;
        if (msg.eventType == SHUTDOWN) {
            break;   // exit TX thread
        }
        switch (msg.eventType)
        {
        case INTRUSION:
        // processing 
            frame = constructMessage(
                ci->client_id, 
                e_write,       
                msg.param,             
                msg.value,            
                msg.eventType      
            );
            break;
        case SPEED:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.param,             
                msg.value,  
                msg.eventType          // eventType
            );
            break;

        case DISTANCE:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.param,             
                msg.value,  
                msg.eventType       // eventType
            );
            break;
        case JOIN_PLATOON:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.param,     // id of the truck that left
                msg.value,     // new position
                msg.eventType    // 
            );
            break;
        case LEAVE_PLATOON:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                msg.param,     // id of the truck that left
                msg.value,     // new position
                msg.eventType    // 
            );
            break;
        case EMERGENCY_BRAKE:
            frame = constructMessage(
                ci->client_id, // truck_id
                e_write,       // rw = write
                0,             // speed = 0
                msg.value,           // reading distance
                msg.eventType // eventType
            );
            break;
        default:
            printf("\n Client ID - %d Undefined RX case", ci->client_id);
            break;
        }

        if (frame)
        {
            send(s, frame, strlen(frame), 0);
            free(frame);
        }
    }
    printf("Client RX thread destroyed");
    return NULL;
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
                                     leader->currentDistance, EMERGENCY_BRAKE);

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
    leader->currentDistance = distance;

    char *msg = constructMessage(
        leader->id,
        e_write,
        leader->currentSpeed,
        leader->currentDistance,
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
        // 4. Accept a client connection
        // This will block the main thread here.
        // printf(" socket fd is %d\n", newSocket);
        newSocket = accept(server_fd, (struct sockaddr *)&clientAddr, &addr_len);
        // printf(" socket fd is %d\n", newSocket);
        if (newSocket == INVALID_SOCKET)
        {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
            {
                DO_NOTHING // no new connection yet, continue to main thread
            }
            else
            {
                printf("accept failed: %d\n", err);
                break;
            }
        }
        else
        {
            int id = assignFreeID();
            if (id == -1)
            {
                printf("Max clients reached\n");
                closesocket(newSocket);
                continue;
            }

            // SOCKET *client = malloc(sizeof(SOCKET));
            // *client = newSocket;
            clientInfo *newClient = malloc(sizeof(clientInfo));
            newClient->socClient = malloc(sizeof(SOCKET));
            *(newClient->socClient) = newSocket;
            g_clients[id] = newClient;   // for broadcasting
            newClient->client_id = ++id;
            printf("\n truck id= %d", id);
            newClient->client_position = g_followerCount;
            

            TxQueue_init(&newClient->txQueue);
            // Publish client ID to client
            char *clientIdFrame = constructMessage(newClient->client_id, e_write, newClient->client_id, newClient->client_position, CLIENT_ID);
            send(newSocket, clientIdFrame, strlen(clientIdFrame), 0);
            free(clientIdFrame);

            broadcastAll(JOIN_PLATOON, newClient->client_id, g_followerCount); // broadcast to others

            printf("New connection, socket fd is %d\n", newSocket);
            usleep(1000); // Sleep briefly to avoid busy-waiting

            pthread_t rxThread, txThread;

            pthread_create(&txThread, NULL, ServerTxHandler, (void *)newClient);
            pthread_detach(txThread);

            pthread_create(&rxThread, NULL, ServerRxHandler, (void *)newClient);
            pthread_detach(rxThread);
        }
        // main thread follows this (if any)
    }

    // Clean up
    closesocket(server_fd);
    WSACleanup();

    return 0;
}
