#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define RELAY_HOST "192.168.101.114"
#define RELAY_PORT 18744
#define CONTROL_HOST "127.0.0.1"
#define CONTROL_PORT 18743
#define GPS_PIPE "/var/run/gpspipe"
#define GPS_CONTROL "/dev/gps"
#define GPS_FALLBACK_CONFIG \
    "/mnt/sdcard/opentom/etc/weather-fallback-location.cfg"
#define GPS_FALLBACK_DELAY_SECONDS 60
#define REQUEST_INTERVAL_SECONDS (60 * 60)
#define HTTP_BUFFER_SIZE 8192
#define WEATHER_FETCH_FAILED 0
#define WEATHER_FETCH_SUCCEEDED 1
#define WEATHER_FETCH_RATE_LIMITED 2

typedef struct {
    double latitude;
    double longitude;
} GpsFix;

typedef struct {
    GpsFix fix;
    int enabled;
} FallbackLocation;

typedef struct {
    int condition;
    int temperature;
    int alert;
    int precipitation;
    int noteworthy;
} WeatherSummary;

static int
connect_to(const char *host, int port, int timeout_seconds)
{
    struct sockaddr_in address;
    struct timeval timeout;
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_aton(host, &address.sin_addr) == 0 ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int
send_all(int fd, const char *buffer, size_t length)
{
    size_t sent_total = 0;

    while (sent_total < length) {
        ssize_t sent = send(fd, buffer + sent_total, length - sent_total, 0);
        if (sent < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (sent == 0)
            return -1;
        sent_total += (size_t)sent;
    }
    return 0;
}

static int
parse_coordinate(const char *value, char hemisphere, double *coordinate)
{
    char *end;
    double raw;
    double degrees;
    double minutes;

    if (value == NULL || value[0] == '\0')
        return 0;
    raw = strtod(value, &end);
    if (end == value || *end != '\0')
        return 0;
    if (!(raw >= 0.0) || raw > 18000.0)
        return 0;
    degrees = (int)(raw / 100.0);
    minutes = raw - degrees * 100.0;
    if (minutes < 0.0 || minutes >= 60.0)
        return 0;
    *coordinate = degrees + minutes / 60.0;
    if (hemisphere == 'S' || hemisphere == 'W')
        *coordinate = -*coordinate;
    else if (hemisphere != 'N' && hemisphere != 'E')
        return 0;
    return 1;
}

static int
parse_rmc(char *line, GpsFix *fix)
{
    char *fields[16];
    char *checksum;
    char *cursor;
    int field_count = 0;
    unsigned char calculated = 0;
    unsigned int supplied;
    int i;

    if (strncmp(line, "$GPRMC,", 7) != 0 &&
        strncmp(line, "$GNRMC,", 7) != 0)
        return 0;
    checksum = strchr(line, '*');
    if (checksum == NULL || strlen(checksum + 1) < 2 ||
        !isxdigit((unsigned char)checksum[1]) ||
        !isxdigit((unsigned char)checksum[2]) ||
        (checksum[3] != '\0' && checksum[3] != '\r') ||
        sscanf(checksum + 1, "%2x", &supplied) != 1)
        return 0;
    for (cursor = line + 1; cursor < checksum; ++cursor)
        calculated ^= (unsigned char)*cursor;
    if (calculated != (unsigned char)supplied)
        return 0;
    *checksum = '\0';
    fields[field_count++] = line;
    for (cursor = line; *cursor != '\0' && field_count < 16; ++cursor) {
        if (*cursor == ',') {
            *cursor = '\0';
            fields[field_count++] = cursor + 1;
        }
    }
    if (field_count < 10 || strcmp(fields[2], "A") != 0)
        return 0;
    for (i = 0; i < field_count; ++i) {
        if (fields[i] == NULL)
            return 0;
    }
    if (!parse_coordinate(fields[3], fields[4][0], &fix->latitude) ||
        !parse_coordinate(fields[5], fields[6][0], &fix->longitude))
        return 0;
    return fix->latitude >= -90.0 && fix->latitude <= 90.0 &&
        fix->longitude >= -180.0 && fix->longitude <= 180.0;
}

static int
load_fallback_location(FallbackLocation *location)
{
    FILE *file;
    char line[96];
    char extra;
    int valid = 0;

    location->enabled = 0;
    file = fopen(GPS_FALLBACK_CONFIG, "r");
    if (file == NULL)
        return 0;
    if (fgets(line, sizeof(line), file) != NULL &&
        sscanf(line, "%lf %lf %c", &location->fix.latitude,
               &location->fix.longitude, &extra) == 2 &&
        location->fix.latitude >= -90.0 &&
        location->fix.latitude <= 90.0 &&
        location->fix.longitude >= -180.0 &&
        location->fix.longitude <= 180.0)
        valid = 1;
    if (fclose(file) != 0)
        valid = 0;
    location->enabled = valid;
    return valid;
}

static int
gps_fallback_due(time_t now, time_t started, time_t last_valid_fix)
{
    time_t reference = last_valid_fix != 0 ? last_valid_fix : started;

    return now >= reference &&
        now - reference >= GPS_FALLBACK_DELAY_SECONDS;
}

static void
enable_raw_gps(void)
{
    static const char command[] = "EnableRawGPSOutput\n";
    int fd = open(GPS_CONTROL, O_WRONLY);
    ssize_t written;

    if (fd < 0) {
        fprintf(stderr, "weather-sync: GPS control unavailable\n");
        return;
    }
    written = write(fd, command, sizeof(command) - 1);
    if (written < 0)
        fprintf(stderr, "weather-sync: could not enable GPS output\n");
    close(fd);
}

static int
fetch_weather(const GpsFix *fix, WeatherSummary *summary)
{
    char request[512];
    char payload[160];
    char response[HTTP_BUFFER_SIZE];
    char *body;
    char *header_end;
    int fd;
    int request_length;
    size_t received = 0;
    int parsed;

    fd = connect_to(RELAY_HOST, RELAY_PORT, 4);
    if (fd < 0) {
        fprintf(stderr, "weather-sync: host relay unavailable\n");
        return 0;
    }
    parsed = snprintf(
        payload, sizeof(payload),
        "{\"latitude\":%.6f,\"longitude\":%.6f,"
        "\"location_sharing_enabled\":true,\"language_code\":\"en\"}",
        fix->latitude, fix->longitude);
    if (parsed < 0 || parsed >= (int)sizeof(payload)) {
        close(fd);
        return 0;
    }
    request_length = snprintf(
        request, sizeof(request),
        "POST /v1/weather/display HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: %lu\r\n\r\n"
        "%s",
        RELAY_HOST,
        (unsigned long)strlen(payload),
        payload);
    if (request_length < 0 || request_length >= (int)sizeof(request) ||
        send_all(fd, request, (size_t)request_length) != 0) {
        close(fd);
        return 0;
    }
    while (received < sizeof(response) - 1) {
        ssize_t count = recv(fd, response + received,
                             sizeof(response) - received - 1, 0);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return 0;
        }
        if (count == 0)
            break;
        received += (size_t)count;
    }
    close(fd);
    response[received] = '\0';
    header_end = strstr(response, "\r\n\r\n");
    if (header_end == NULL) {
        fprintf(stderr, "weather-sync: invalid host relay response\n");
        return WEATHER_FETCH_FAILED;
    }
    if (strncmp(response, "HTTP/1.1 429 ", 13) == 0) {
        fprintf(stderr, "weather-sync: refresh limited; waiting one hour\n");
        return WEATHER_FETCH_RATE_LIMITED;
    }
    if (strncmp(response, "HTTP/1.1 200 ", 13) != 0) {
        fprintf(stderr, "weather-sync: host relay returned an error\n");
        return WEATHER_FETCH_FAILED;
    }
    body = header_end + 4;
    parsed = sscanf(body, "TMW1 %d %d %d %d %d",
                    &summary->condition, &summary->temperature,
                    &summary->alert, &summary->precipitation,
                    &summary->noteworthy);
    if (parsed != 5 ||
        summary->condition < 0 || summary->condition > 6 ||
        summary->temperature < -100 || summary->temperature > 100 ||
        summary->alert < 0 || summary->alert > 5 ||
        summary->precipitation < 0 || summary->precipitation > 100 ||
        (summary->noteworthy != 0 && summary->noteworthy != 1)) {
        fprintf(stderr, "weather-sync: invalid weather summary\n");
        return WEATHER_FETCH_FAILED;
    }
    return WEATHER_FETCH_SUCCEEDED;
}

static int
set_device_weather(const WeatherSummary *summary)
{
    char command[96];
    char response[64];
    int fd;
    int length;
    ssize_t received;

    fd = connect_to(CONTROL_HOST, CONTROL_PORT, 2);
    if (fd < 0)
        return 0;
    length = snprintf(command, sizeof(command),
                      "SET_WEATHER %d %d %d %d %d\n",
                      summary->condition, summary->temperature,
                      summary->alert, summary->precipitation,
                      summary->noteworthy);
    if (length < 0 || length >= (int)sizeof(command) ||
        send_all(fd, command, (size_t)length) != 0) {
        close(fd);
        return 0;
    }
    received = recv(fd, response, sizeof(response) - 1, 0);
    close(fd);
    if (received <= 0)
        return 0;
    response[received] = '\0';
    return strncmp(response, "OK WEATHER\n", 11) == 0;
}

static void
refresh_weather(const GpsFix *fix, const char *source, time_t *next_request)
{
    WeatherSummary summary;
    int fetch_result = fetch_weather(fix, &summary);

    if (fetch_result == WEATHER_FETCH_RATE_LIMITED) {
        fprintf(stderr,
                "weather-sync: %s refresh limited; waiting one hour\n",
                source);
    } else if (fetch_result == WEATHER_FETCH_SUCCEEDED &&
               set_device_weather(&summary)) {
        fprintf(stderr, "weather-sync: %s weather updated\n", source);
    } else {
        if (fetch_result == WEATHER_FETCH_SUCCEEDED)
            fprintf(stderr, "weather-sync: device update failed\n");
        *next_request = time(NULL) + REQUEST_INTERVAL_SECONDS;
        return;
    }
    *next_request = time(NULL) + REQUEST_INTERVAL_SECONDS;
}

static void
maybe_request_fallback(time_t now, time_t started, time_t last_valid_fix,
                       const FallbackLocation *fallback,
                       int *fallback_requested, time_t *next_request)
{
    if (!fallback->enabled || *fallback_requested ||
        now < *next_request ||
        !gps_fallback_due(now, started, last_valid_fix))
        return;
    refresh_weather(&fallback->fix, "fallback", next_request);
    *fallback_requested = 1;
}

static void
poll_gps(const FallbackLocation *fallback)
{
    char line[160];
    size_t used = 0;
    time_t started = time(NULL);
    time_t last_valid_fix = 0;
    time_t next_request = 0;
    int fallback_requested = 0;
    int gps_fd = -1;

    for (;;) {
        fd_set read_set;
        struct timeval wait;
        int ready;
        char chunk[64];
        ssize_t count;

        if (gps_fd < 0)
            gps_fd = open(GPS_PIPE, O_RDONLY | O_NONBLOCK);
        if (gps_fd < 0) {
            maybe_request_fallback(time(NULL), started, last_valid_fix,
                                   fallback, &fallback_requested,
                                   &next_request);
            sleep(5);
            continue;
        }
        FD_ZERO(&read_set);
        FD_SET(gps_fd, &read_set);
        wait.tv_sec = 2;
        wait.tv_usec = 0;
        ready = select(gps_fd + 1, &read_set, NULL, NULL, &wait);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            close(gps_fd);
            gps_fd = -1;
            maybe_request_fallback(time(NULL), started, last_valid_fix,
                                   fallback, &fallback_requested,
                                   &next_request);
            continue;
        }
        if (ready == 0) {
            maybe_request_fallback(time(NULL), started, last_valid_fix,
                                   fallback, &fallback_requested,
                                   &next_request);
            continue;
        }
        count = read(gps_fd, chunk, sizeof(chunk));
        if (count == 0) {
            close(gps_fd);
            gps_fd = -1;
            sleep(2);
            maybe_request_fallback(time(NULL), started, last_valid_fix,
                                   fallback, &fallback_requested,
                                   &next_request);
            continue;
        }
        if (count <= 0) {
            if (count < 0 && errno != EAGAIN && errno != EINTR) {
                close(gps_fd);
                gps_fd = -1;
            }
            maybe_request_fallback(time(NULL), started, last_valid_fix,
                                   fallback, &fallback_requested,
                                   &next_request);
            continue;
        }
        while (count > 0) {
            char *newline;
            size_t take;

            newline = memchr(chunk, '\n', (size_t)count);
            take = newline == NULL ? (size_t)count :
                (size_t)(newline - chunk);
            if (take >= sizeof(line) - used - 1) {
                used = 0;
            } else {
                memcpy(line + used, chunk, take);
                used += take;
                if (newline != NULL) {
                    GpsFix fix;
                    int valid;

                    line[used] = '\0';
                    valid = parse_rmc(line, &fix);
                    used = 0;
                    if (valid) {
                        time_t now = time(NULL);

                        last_valid_fix = now;
                        fallback_requested = 0;
                        if (now >= next_request)
                            refresh_weather(&fix, "GPS", &next_request);
                    }
                }
            }
            if (newline == NULL)
                break;
            count -= (ssize_t)(take + 1);
            memmove(chunk, newline + 1, (size_t)count);
        }
        maybe_request_fallback(time(NULL), started, last_valid_fix,
                               fallback, &fallback_requested, &next_request);
    }
}

int
main(void)
{
    FallbackLocation fallback;
    int fd;

    signal(SIGPIPE, SIG_IGN);
    if (!load_fallback_location(&fallback))
        fprintf(stderr, "weather-sync: fallback location unavailable\n");
    else
        fprintf(stderr, "weather-sync: Tzaneen fallback enabled\n");
    fd = connect_to(CONTROL_HOST, CONTROL_PORT, 2);
    if (fd >= 0) {
        send_all(fd, "CLEAR_WEATHER\n", 14);
        close(fd);
    }
    enable_raw_gps();
    poll_gps(&fallback);
    return 0;
}
