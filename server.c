#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
// What we want is to have clients reach server with client ID
// server will then post messages from them.
// If client kills itself server should recognise that.

// we cannot run in both windows & linux with same libraries. So using a ehader to differentiate
// #ifdef USE_LINUX

//     #include <arpa/inet.h>
//     #include <sys/socket.h>

// #else
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#include <unistd.h>
#include <fcntl.h>

// #endif

// Function prototypes & definitions

struct ip_addr
{
    unsigned long S_addr;
};

typedef struct s_clientInfo
{
    SOCKET *socClient;
    int client_id;
} clientInfo;

// Macros
#define PORT 8080
#define NON_BLOCKING 1
#define BLOCKING 0

// Globals
u_long mode = NON_BLOCKING;
u_long modeConnection = BLOCKING;
int id = 0;
void *clientHandler(void *client)
{
    char buffer[1024];
    clientInfo *tempArgument = (clientInfo *)client;
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
    memset(buffer, 0, sizeof(buffer));
    while (1)
    {
        int bytesReceived = recv(*tempClient, buffer, sizeof(buffer) - 1, 0);
        // printf(" socket fd is  %d : %s",tempArgument->client_id,  buffer);

        if (bytesReceived > 0)
        {
            printf("\n Printing from %d : %s", tempArgument->client_id, buffer);
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
                // no new connection yet
                continue;
            }
            printf("a 7 ccept failed: %d\n", err);
            break;
        }
        else
        {
            // SOCKET *client = malloc(sizeof(SOCKET));
            // *client = newSocket;

            clientInfo *newClient = malloc(sizeof(clientInfo));
            newClient->socClient = malloc(sizeof(SOCKET));
            *(newClient->socClient) = newSocket;
            newClient->client_id = id++;
            printf("New connection, socket fd is %d\n", newSocket);
            usleep(1000); // Sleep briefly to avoid busy-waiting

            pthread_t t;
            pthread_create(&t, NULL, clientHandler, (void *)(newClient));
            pthread_detach(t);
        }
    }

    // Clean up
    closesocket(server_fd);
    WSACleanup();

    return 0;
}
