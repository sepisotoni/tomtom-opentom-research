#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef TOMTOM_CONTROL_BIND_ADDR
#define TOMTOM_CONTROL_BIND_ADDR "192.168.101.115"
#endif
#ifndef TOMTOM_CONTROL_PORT
#define TOMTOM_CONTROL_PORT 18743
#endif
#ifndef TOMTOM_CONTROL_ALLOWED_NETWORK
#define TOMTOM_CONTROL_ALLOWED_NETWORK "192.168.101.0"
#endif
#ifndef TOMTOM_CONTROL_ALLOWED_MASK
#define TOMTOM_CONTROL_ALLOWED_MASK "255.255.255.0"
#endif
#ifndef TOMTOM_CONTROL_FACE_FILE
#define TOMTOM_CONTROL_FACE_FILE \
    "/mnt/sdcard/opentom/preview-gallery/current_face"
#endif

#define FACE_COUNT 9
#define REQUEST_MAX 64

static volatile sig_atomic_t stopping;

static void
stop_server(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static int
send_response(int client, const char *message)
{
    size_t remaining = strlen(message);
    const char *cursor = message;

    while (remaining > 0) {
        ssize_t sent = send(client, cursor, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        cursor += sent;
        remaining -= (size_t)sent;
    }
    return 0;
}

static int
save_face(int face_id)
{
    char temporary_path[sizeof(TOMTOM_CONTROL_FACE_FILE) + 16];
    FILE *file;
    int failed;

    if (snprintf(temporary_path, sizeof(temporary_path), "%s.control-tmp",
                 TOMTOM_CONTROL_FACE_FILE) >= (int)sizeof(temporary_path))
        return -1;
    file = fopen(temporary_path, "w");
    if (file == NULL)
        return -1;
    failed = fprintf(file, "%d\n", face_id) < 0;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0)
        failed = 1;
    if (fclose(file) != 0)
        failed = 1;
    if (failed) {
        unlink(temporary_path);
        return -1;
    }
    if (rename(temporary_path, TOMTOM_CONTROL_FACE_FILE) != 0) {
        unlink(temporary_path);
        return -1;
    }
    return 0;
}

static int
read_face(void)
{
    FILE *file = fopen(TOMTOM_CONTROL_FACE_FILE, "r");
    char line[REQUEST_MAX];
    char *end;
    long face_id;

    if (file == NULL)
        return -1;
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return -1;
    }
    fclose(file);
    face_id = strtol(line, &end, 10);
    while (*end == '\r' || *end == '\n' || *end == ' ' || *end == '\t')
        ++end;
    if (end == line || *end != '\0' || face_id < 0 || face_id >= FACE_COUNT)
        return -1;
    return (int)face_id;
}

static void
handle_client(int client, const struct sockaddr_in *peer)
{
    char request[REQUEST_MAX];
    char response[REQUEST_MAX];
    size_t used = 0;
    unsigned long address = ntohl(peer->sin_addr.s_addr);
    unsigned long usb_network =
        ntohl(inet_addr(TOMTOM_CONTROL_ALLOWED_NETWORK));
    unsigned long mask = ntohl(inet_addr(TOMTOM_CONTROL_ALLOWED_MASK));
    struct timeval receive_timeout;

    receive_timeout.tv_sec = 3;
    receive_timeout.tv_usec = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
               sizeof(receive_timeout));

    if ((address & mask) != usb_network) {
        send_response(client, "ERR USB_ONLY\n");
        return;
    }

    while (used < sizeof(request) - 1) {
        char byte;
        ssize_t received = recv(client, &byte, 1, 0);
        if (received < 0) {
            if (errno == EINTR && !stopping)
                continue;
            return;
        }
        if (received == 0)
            return;
        if (byte == '\n')
            break;
        if (byte != '\r')
            request[used++] = byte;
    }
    if (used == sizeof(request) - 1) {
        send_response(client, "ERR REQUEST_TOO_LONG\n");
        return;
    }
    request[used] = '\0';

    if (strncmp(request, "SET_FACE ", 9) == 0) {
        char *end;
        long face_id = strtol(request + 9, &end, 10);
        while (*end == ' ' || *end == '\t')
            ++end;
        if (end == request + 9 || *end != '\0' ||
            face_id < 0 || face_id >= FACE_COUNT) {
            send_response(client, "ERR INVALID_FACE\n");
            return;
        }
        if (save_face((int)face_id) != 0) {
            send_response(client, "ERR SAVE_FAILED\n");
            return;
        }
        snprintf(response, sizeof(response), "OK FACE %ld\n", face_id);
        send_response(client, response);
        return;
    }
    if (strcmp(request, "STATUS") == 0) {
        int face_id = read_face();
        if (face_id < 0) {
            send_response(client, "ERR NO_FACE\n");
            return;
        }
        snprintf(response, sizeof(response), "OK FACE %d\n", face_id);
        send_response(client, response);
        return;
    }
    if (strcmp(request, "PING") == 0) {
        send_response(client, "OK TOMTOM_CONTROL 1\n");
        return;
    }
    send_response(client, "ERR UNKNOWN_COMMAND\n");
}

int
main(void)
{
    struct sockaddr_in address;
    int server;
    int enabled = 1;

    signal(SIGTERM, stop_server);
    signal(SIGINT, stop_server);
    signal(SIGPIPE, SIG_IGN);

    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("tomtom-control: socket");
        return 1;
    }
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(TOMTOM_CONTROL_PORT);
    address.sin_addr.s_addr = inet_addr(TOMTOM_CONTROL_BIND_ADDR);
    if (address.sin_addr.s_addr == INADDR_NONE ||
        bind(server, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("tomtom-control: bind");
        close(server);
        return 1;
    }
    if (listen(server, 2) != 0) {
        perror("tomtom-control: listen");
        close(server);
        return 1;
    }
    while (!stopping) {
        struct sockaddr_in peer;
        socklen_t peer_size = sizeof(peer);
        fd_set read_set;
        struct timeval wait;
        int ready;
        int client;

        FD_ZERO(&read_set);
        FD_SET(server, &read_set);
        wait.tv_sec = 1;
        wait.tv_usec = 0;
        ready = select(server + 1, &read_set, NULL, NULL, &wait);
        if (ready == 0)
            continue;
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("tomtom-control: select");
            break;
        }
        client = accept(server, (struct sockaddr *)&peer, &peer_size);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            perror("tomtom-control: accept");
            break;
        }
        handle_client(client, &peer);
        close(client);
    }
    close(server);
    return 0;
}
