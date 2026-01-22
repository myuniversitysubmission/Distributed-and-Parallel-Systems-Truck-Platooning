#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include<unistd.h>

#pragma comment(lib, "Ws2_32.lib")

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

    while(1){ // Send velocity data
    const char *velocity = argv[1];
    int len = strlen(velocity);
    int bytesSent = send(clientSocket, velocity, len, 0);
    if (bytesSent == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            // no data yet, continue loop
            continue;
        }
        else{
            printf("\n Server disconnected\n");
            closesocket(clientSocket);
            return -1;
        }
    }
    else if (bytesSent == 0)
    {
        printf("\n Server disconnected\n");
        closesocket(clientSocket);
        WSACleanup();
    }
    else {
        printf("Sent %d bytes: %s\n", bytesSent, velocity);
    }
    
    sleep(5); // Sleep briefly to avoid busy-waiting
    }

    closesocket(clientSocket);
    WSACleanup();
}