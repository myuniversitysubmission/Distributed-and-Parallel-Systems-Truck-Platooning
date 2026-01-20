#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// we cannot run in both windows & linux with same libraries. So using a ehader to differentiate
// #ifdef USE_LINUX

//     #include <arpa/inet.h>
//     #include <sys/socket.h>

// #else
    #include <winsock2.h>
    #include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")


//#endif

// Function prototypes & definitions

struct ip_addr{
    unsigned long S_addr;
};

// Macros
#define PORT 8080

int main(){

    #ifndef USE_LINUX
        // winsocket initialisation
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            //fprintf(stderr, "WSAStartup failed with error: %d\n", GET_LAST_ERROR());
            printf("Failed to initialize Winsock.\n");
            return 1;
        }
    #endif
    
    // server_fd is the so called channel, where data flows
    SOCKET server_fd, client_socket1;

    struct sockaddr_in server_addr, clientAddr;
    server_addr.sin_addr.s_addr  = INADDR_ANY; // Listen to all interfaces
    server_addr.sin_family = AF_INET; // IP_v4
    server_addr.sin_port = htons(PORT);
    int addr_len = sizeof(server_addr);
    char buffer[1024];

    // 1. Socket creation
    if((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET){
        
        printf("Failed to create socket.\n", WSAGetLastError());
        //throwError("Error occured in socket creation");
        // Close WSA & exit out of program
        #ifndef USE_LINUX
            WSACleanup();
        #endif
        exit(EXIT_FAILURE);    
    } 

    // 2. Binding socekt to a address
    
    if ((bind(server_fd, (struct sockaddr *)&server_addr, addr_len) == SOCKET_ERROR)){
        
        printf("Bind failed.\n", WSAGetLastError());
        // throwError("Bind error");
        closesocket(server_fd);
        // Close WSA & exit out of program
        #ifndef USE_LINUX
            WSACleanup();
        #endif
        exit(EXIT_FAILURE);
    }

    /***********************************************************/
    
    // 3. Listen for incoming connections
    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed.\n", WSAGetLastError());
         closesocket(server_fd);
         WSACleanup();
         return 1;
    }

    printf("Server is listening on port %d...\n", PORT);

    // 4. Accept a client connection
    client_socket1 = accept(server_fd, (struct sockaddr *)&clientAddr, &addr_len);
    if (client_socket1 == INVALID_SOCKET) {
        printf("Accept failed.\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    
    printf("Client connected.\n");

    // Receive data from client
    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        int bytesReceived = recv(client_socket1, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived == SOCKET_ERROR)
        {
            printf("Receive error %d\n", WSAGetLastError());
            // break;
        }

        if (bytesReceived == 0)
        {
            printf("Client disconnected\n");
            // break;
        }

        // Print received data
        printf("Velocity received: %s\n", buffer);

        
    }

    // Clean up
    closesocket(client_socket1);
    closesocket(server_fd);
    WSACleanup();

    return 0;

}




