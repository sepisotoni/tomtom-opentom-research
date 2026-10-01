#include "tomtom-display-protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef TOMTOM_DISPLAY_BIND_ADDR
#define TOMTOM_DISPLAY_BIND_ADDR "192.168.101.115"
#endif
#ifndef TOMTOM_DISPLAY_PORT
#define TOMTOM_DISPLAY_PORT 18745
#endif
#ifndef TOMTOM_DISPLAY_ALLOWED_NETWORK
#define TOMTOM_DISPLAY_ALLOWED_NETWORK "192.168.101.0"
#endif
#ifndef TOMTOM_DISPLAY_ALLOWED_MASK
#define TOMTOM_DISPLAY_ALLOWED_MASK "255.255.255.0"
#endif
#ifndef TOMTOM_DISPLAY_FRAMEBUFFER
#define TOMTOM_DISPLAY_FRAMEBUFFER "/dev/fb0"
#endif

#define FRAME_INTERVAL_MIN_MS 100
#define CLIENT_TIMEOUT_SECONDS 2

static volatile sig_atomic_t stopping;
static int framebuffer_fd = -1;
static unsigned char *framebuffer_memory;
static struct fb_fix_screeninfo framebuffer_fixed;
static struct fb_var_screeninfo framebuffer_variable;
static unsigned char frame_pixels[TOMTOM_DISPLAY_FRAME_BYTES];

static void
stop_receiver(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static unsigned long
current_milliseconds(void)
{
    struct timeval now;

    if (gettimeofday(&now, NULL) != 0)
        return 0;
    return (unsigned long)now.tv_sec * 1000UL +
        (unsigned long)now.tv_usec / 1000UL;
}

static int
framebuffer_open(void)
{
    size_t required_size;

    framebuffer_fd = open(TOMTOM_DISPLAY_FRAMEBUFFER, O_RDWR);
    if (framebuffer_fd < 0) {
        perror("display-receiver: open framebuffer");
        return -1;
    }
    if (ioctl(framebuffer_fd, FBIOGET_FSCREENINFO, &framebuffer_fixed) < 0 ||
        ioctl(framebuffer_fd, FBIOGET_VSCREENINFO,
              &framebuffer_variable) < 0) {
        perror("display-receiver: framebuffer info");
        return -1;
    }
    required_size = (size_t)framebuffer_fixed.line_length *
        TOMTOM_DISPLAY_HEIGHT;
    if (framebuffer_variable.xres != TOMTOM_DISPLAY_WIDTH ||
        framebuffer_variable.yres != TOMTOM_DISPLAY_HEIGHT ||
        framebuffer_variable.xres_virtual < TOMTOM_DISPLAY_WIDTH ||
        framebuffer_variable.yres_virtual < TOMTOM_DISPLAY_HEIGHT ||
        framebuffer_variable.xoffset != 0 ||
        framebuffer_variable.yoffset != 0 ||
        framebuffer_variable.bits_per_pixel != 16 ||
        framebuffer_fixed.type != FB_TYPE_PACKED_PIXELS ||
        framebuffer_fixed.visual != FB_VISUAL_TRUECOLOR ||
        framebuffer_variable.red.offset != 11 ||
        framebuffer_variable.red.length != 5 ||
        framebuffer_variable.red.msb_right != 0 ||
        framebuffer_variable.green.offset != 5 ||
        framebuffer_variable.green.length != 6 ||
        framebuffer_variable.green.msb_right != 0 ||
        framebuffer_variable.blue.offset != 0 ||
        framebuffer_variable.blue.length != 5 ||
        framebuffer_variable.blue.msb_right != 0 ||
        framebuffer_fixed.line_length < TOMTOM_DISPLAY_WIDTH *
            TOMTOM_DISPLAY_BYTES_PER_PIXEL ||
        framebuffer_fixed.smem_len < required_size) {
        fprintf(stderr, "display-receiver: unsupported framebuffer mode\n");
        return -1;
    }
    framebuffer_memory = (unsigned char *)mmap(
        NULL, framebuffer_fixed.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED,
        framebuffer_fd, 0);
    if (framebuffer_memory == MAP_FAILED) {
        framebuffer_memory = NULL;
        perror("display-receiver: mmap framebuffer");
        return -1;
    }
    return 0;
}

static void
framebuffer_close(void)
{
    if (framebuffer_memory != NULL) {
        munmap(framebuffer_memory, framebuffer_fixed.smem_len);
        framebuffer_memory = NULL;
    }
    if (framebuffer_fd >= 0) {
        close(framebuffer_fd);
        framebuffer_fd = -1;
    }
}

static int
receive_exact(int socket_fd, unsigned char *destination, size_t length)
{
    size_t received_total = 0;
    unsigned long deadline =
        current_milliseconds() + CLIENT_TIMEOUT_SECONDS * 1000UL;

    while (received_total < length && !stopping) {
        struct timeval wait;
        fd_set read_set;
        unsigned long now = current_milliseconds();
        unsigned long remaining;
        int ready;
        ssize_t received;

        if (now >= deadline)
            return -1;
        remaining = deadline - now;
        wait.tv_sec = (long)(remaining / 1000UL);
        wait.tv_usec = (long)(remaining % 1000UL) * 1000L;
        FD_ZERO(&read_set);
        FD_SET(socket_fd, &read_set);
        ready = select(socket_fd + 1, &read_set, NULL, NULL, &wait);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ready == 0)
            return -1;
        received = recv(socket_fd, destination + received_total,
                        length - received_total, 0);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (received == 0)
            return -1;
        received_total += (size_t)received;
    }
    return received_total == length ? 0 : -1;
}

static int
send_ack(int socket_fd, unsigned long sequence, unsigned int status)
{
    unsigned char acknowledgement[TOMTOM_DISPLAY_ACK_SIZE];
    size_t sent_total = 0;

    tomtom_display_make_ack(acknowledgement, sequence, status);
    while (sent_total < sizeof(acknowledgement)) {
        ssize_t sent = send(socket_fd, acknowledgement + sent_total,
                            sizeof(acknowledgement) - sent_total, 0);
        if (sent < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        sent_total += (size_t)sent;
    }
    return 0;
}

static int
present_frame(const unsigned char *pixels)
{
    unsigned int row;
    size_t source_row_size =
        TOMTOM_DISPLAY_WIDTH * TOMTOM_DISPLAY_BYTES_PER_PIXEL;

    for (row = 0; row < TOMTOM_DISPLAY_HEIGHT; ++row)
        memcpy(framebuffer_memory +
                   (size_t)row * framebuffer_fixed.line_length,
               pixels + (size_t)row * source_row_size, source_row_size);
    return 0;
}

static void
handle_client(int client, const struct sockaddr_in *peer)
{
    struct timeval timeout;
    unsigned long allowed_network =
        ntohl(inet_addr(TOMTOM_DISPLAY_ALLOWED_NETWORK));
    unsigned long allowed_mask =
        ntohl(inet_addr(TOMTOM_DISPLAY_ALLOWED_MASK));
    unsigned long last_frame_ms = 0;

    timeout.tv_sec = CLIENT_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) < 0 ||
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout)) < 0) {
        perror("display-receiver: socket timeout");
        return;
    }
    if ((ntohl(peer->sin_addr.s_addr) & allowed_mask) != allowed_network) {
        fprintf(stderr, "display-receiver: rejected non-USB peer\n");
        return;
    }

    while (!stopping) {
        unsigned char header[TOMTOM_DISPLAY_HEADER_SIZE];
        struct tomtom_display_frame frame;
        unsigned long now;

        if (receive_exact(client, header, sizeof(header)) != 0)
            return;
        if (!tomtom_display_parse_header(header, sizeof(header), &frame)) {
            fprintf(stderr, "display-receiver: rejected invalid frame header\n");
            return;
        }
        if (receive_exact(client, frame_pixels, sizeof(frame_pixels)) != 0)
            return;
        now = current_milliseconds();
        if (last_frame_ms != 0 &&
            now - last_frame_ms < FRAME_INTERVAL_MIN_MS) {
            send_ack(client, frame.sequence, TOMTOM_DISPLAY_ACK_RATE_LIMIT);
            return;
        }
        if (present_frame(frame_pixels) != 0) {
            send_ack(client, frame.sequence, TOMTOM_DISPLAY_ACK_FRAMEBUFFER);
            return;
        }
        last_frame_ms = now;
        if (send_ack(client, frame.sequence, TOMTOM_DISPLAY_ACK_OK) != 0)
            return;
    }
}

int
main(void)
{
    struct sockaddr_in address;
    int server;
    int enabled = 1;

    signal(SIGTERM, stop_receiver);
    signal(SIGINT, stop_receiver);
    signal(SIGPIPE, SIG_IGN);
    if (framebuffer_open() != 0) {
        framebuffer_close();
        return 1;
    }
    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("display-receiver: socket");
        framebuffer_close();
        return 1;
    }
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(TOMTOM_DISPLAY_PORT);
    address.sin_addr.s_addr = inet_addr(TOMTOM_DISPLAY_BIND_ADDR);
    if (address.sin_addr.s_addr == INADDR_NONE ||
        bind(server, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("display-receiver: bind");
        close(server);
        framebuffer_close();
        return 1;
    }
    if (listen(server, 1) != 0) {
        perror("display-receiver: listen");
        close(server);
        framebuffer_close();
        return 1;
    }
    fprintf(stderr, "DISPLAY_RECEIVER=%dx%d RGB565 port=%d\n",
            TOMTOM_DISPLAY_WIDTH, TOMTOM_DISPLAY_HEIGHT,
            TOMTOM_DISPLAY_PORT);
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
            perror("display-receiver: select");
            break;
        }
        client = accept(server, (struct sockaddr *)&peer, &peer_size);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            perror("display-receiver: accept");
            break;
        }
        handle_client(client, &peer);
        close(client);
    }
    close(server);
    framebuffer_close();
    return 0;
}
