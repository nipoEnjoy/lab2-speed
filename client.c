#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    // Server setup
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection error");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // Getting file
    char filename[256];
    printf("Enter filename: ");
    scanf("%s", filename);

    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("File open error");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // File size
    fseek(file, 0, SEEK_END);
    uint64_t file_size = ftell(file);
    fclose(file);

    // Sending filename - 2 bytes LE
    uint16_t filename_len = strlen(filename);
    send(client_socket, &filename_len, sizeof(filename_len), 0);
    send(client_socket, filename, filename_len, 0);

    // Sending file size - 8 bytes LE
    printf("File size: %lu bytes\n Sending...\n", file_size);
    send(client_socket, (void *) &file_size, 8, 0);

    // Sending file
    FILE *file_content = fopen(filename, "rb");
    if (!file_content) {
        perror("File open error");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    char buffer[4096];
    int bytes;
    while ((bytes = fread(buffer, 1, 4096, file_content)) > 0) {
        send(client_socket, buffer, bytes, 0);
    }
    printf("Sent file to server\n");
    fclose(file_content);

    // Server response
    char response[256];
    recv(client_socket, response, sizeof(response), 0);
    printf("Server response: \"%s\"\n", response);

    close(client_socket);
    return 0;
}
