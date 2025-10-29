#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>

#define PORT 8000
#define UPLOAD_DIR "uploads"

typedef struct {
    int client_socket;
    time_t start_time;
    off_t total_bytes;
    time_t last_check_time;
} client_info;

int recv_file_metadata(int client_socket, char* filename, unsigned long long *file_size) {
    // Recv filename len
    uint16_t filename_len;
    ssize_t len = recv(client_socket, &filename_len, sizeof(filename_len), 0);
    if (len <= 0) {
        printf("Error receiving filename length\n");
        close(client_socket);
        return -1;
    }
    filename_len = filename_len;

    // Recv filename
    len = recv(client_socket, filename, filename_len, 0);
    if (len <= 0) {
        printf("Error receiving filename\n");
        close(client_socket);
        return -1;
    }
    filename[filename_len] = '\0';
    printf("Recieved file name: %s \n", filename);

    // Recv file size
    len = recv(client_socket, file_size, 8, 0);
    if (len <= 0) {
        printf("Error receiving file size\n");
        close(client_socket);
        return -1;
    }
    printf("File size to recieve: %llu bytes\n", *file_size);

    return 0;
}

FILE* create_file(char* filename) {
    mkdir(UPLOAD_DIR, 0755);
    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", UPLOAD_DIR, filename);
    return fopen(file_path, "wb");
}

void print_speed(client_info *client, unsigned long long current_total) {
    time_t now = time(NULL);
    double elapsed_since_start = difftime(now, client->start_time);
    double elapsed_since_last = difftime(now, client->last_check_time);

    if (elapsed_since_start <= 0 || elapsed_since_last <= 0) {
        return;
    }

    double avg_speed = (double)current_total / elapsed_since_start;
    double current_speed = (double)(current_total - client->total_bytes) / elapsed_since_last;

    printf("Client [%d]: Avg speed: %.2f KB/s, Current speed: %.2f KB/s\n",
           client->client_socket, avg_speed / 1024, current_speed / 1024);
}

void *handle_client(void *arg) {
    client_info *client = (client_info *)arg;
    int client_socket = client->client_socket;
    char filename[4097];
    unsigned long long file_size = 0;

    int err = recv_file_metadata(client_socket, filename, &file_size);
    if (err != 0) {
        printf("Failed receiving file metadata (%d)\n", err);
        return NULL;
    }

    FILE *file = create_file(filename);
    if (file == NULL) {
        perror("Error creating file");
        close(client_socket);
        return NULL;
    }

    // Recieving
    char buffer[4096];
    unsigned long long total_received = 0;
    time_t last_report_time = client->start_time;
    time_t start_time = time(NULL);

    printf("Trying to recv %llu bytes\n", file_size);
    
    while (total_received < file_size) {
        long len = recv(client_socket, buffer, sizeof(buffer), 0);
        if (len <= 0) {
            break;
        }

        total_received += len;
        fwrite(buffer, 1, len, file);

        time_t now = time(NULL);
        if (difftime(now, last_report_time) >= 0.005f) {
            print_speed(client, total_received);
            last_report_time = now;
            client->total_bytes = total_received;
            client->last_check_time = now;
        }
    }

    // Final stats
    time_t final_time = time(NULL);

    long actual_size = ftell(file);
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        close(client_socket);
        printf("Closed client socket [%d]: Error at file size check\n", client_socket);
        return NULL;
    }

    printf("Received file with size: %ld bytes\n", actual_size);
    fclose(file);

    if (difftime(final_time, last_report_time) > 0) {
        print_speed(client, total_received); 
    }

    if (actual_size != -1 && actual_size != file_size) {
        printf("File size mismatch: %ld != %llu\n", actual_size, file_size);
    }

    // Response to client
    char response[100];
    snprintf(response, sizeof(response), "File received: %s (%llu bytes)", filename, file_size);
    send(client_socket, response, strlen(response), 0);

    close(client_socket);
    printf("Closed client socket [%d]\n", client_socket);
    return NULL;
}

int start_sever_socket() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);  // IPv4 TCP
    if (server_socket < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind error");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listen error");
        exit(EXIT_FAILURE);
    }

    return server_socket;
}

int accept_client(int server_socket) {
    int client_socket = accept(server_socket, NULL, NULL);
    if (client_socket < 0) {
        perror("Accept error");
        return -1;
    }
    printf("\nSuccessfully accepted new connection [%d]\n", client_socket);

    client_info *client = malloc(sizeof(client_info));
    if (!client) {
        close(client_socket);
        return -1;
    }

    client->client_socket = client_socket;
    client->start_time = time(NULL);
    client->total_bytes = 0;
    client->last_check_time = client->start_time;

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, handle_client, client);
    pthread_detach(thread_id);

    return client_socket;
}

int main() {
    int server_socket = start_sever_socket();
    printf("Server listening on port %d\n", PORT);

    while (1) {
        int client_socket = accept_client(server_socket);
    }

    close(server_socket);
    return 0;
}
