#ifndef NOTIFICATION_EVENT_H
#define NOTIFICATION_EVENT_H

#include <string.h>

#define PREVIEW_NOTIFICATION_TEXT_MAX 32
#define PREVIEW_NOTIFICATION_TTL_MAX 60
#define PREVIEW_NOTIFICATION_FADE_MS 1500UL

typedef struct {
    char text[PREVIEW_NOTIFICATION_TEXT_MAX + 1];
    unsigned long started_ms;
    unsigned long duration_ms;
} PreviewNotification;

static int
preview_parse_notification(const char *packet, size_t packet_length,
                           char *text, size_t text_size, int *ttl)
{
    size_t position;
    size_t message_length;
    int seconds = 0;
    int digits = 0;

    if (packet_length < 9 || memcmp(packet, "OT1|N|", 6) != 0)
        return 0;
    position = 6;
    while (position < packet_length &&
           packet[position] >= '0' && packet[position] <= '9') {
        if (digits >= 2)
            return 0;
        seconds = seconds * 10 + packet[position] - '0';
        ++digits;
        ++position;
    }
    if (digits == 0 || position >= packet_length ||
        packet[position] != '|' || seconds < 1 ||
        seconds > PREVIEW_NOTIFICATION_TTL_MAX)
        return 0;
    ++position;
    message_length = packet_length - position;
    if (message_length == 0 ||
        message_length > PREVIEW_NOTIFICATION_TEXT_MAX ||
        text_size <= message_length)
        return 0;
    while (position < packet_length) {
        unsigned char character = (unsigned char)packet[position];

        if (character < 32 || character > 126)
            return 0;
        ++position;
    }
    memcpy(text, packet + packet_length - message_length, message_length);
    text[message_length] = '\0';
    *ttl = seconds;
    return 1;
}

static void
preview_notification_set(PreviewNotification *notification,
                         const char *text, int ttl,
                         unsigned long now_ms)
{
    size_t length = strlen(text);

    if (length > PREVIEW_NOTIFICATION_TEXT_MAX)
        length = PREVIEW_NOTIFICATION_TEXT_MAX;
    memcpy(notification->text, text, length);
    notification->text[length] = '\0';
    notification->started_ms = now_ms;
    notification->duration_ms = (unsigned long)ttl * 1000UL;
}

static unsigned int
preview_notification_alpha(const PreviewNotification *notification,
                           unsigned long now_ms)
{
    unsigned long elapsed;
    unsigned long fade_ms;
    unsigned long fade_start;

    if (notification->duration_ms == 0)
        return 0;
    elapsed = now_ms - notification->started_ms;
    if (elapsed >= notification->duration_ms)
        return 0;
    fade_ms = notification->duration_ms < PREVIEW_NOTIFICATION_FADE_MS ?
        notification->duration_ms : PREVIEW_NOTIFICATION_FADE_MS;
    fade_start = notification->duration_ms - fade_ms;
    if (elapsed <= fade_start)
        return 255;
    return (unsigned int)(((notification->duration_ms - elapsed) * 255UL +
                           fade_ms - 1UL) / fade_ms);
}

static unsigned int
preview_notification_weather_alpha(unsigned int notification_alpha)
{
    return notification_alpha == 0U ? 255U : 0U;
}

static int
preview_notification_expired(const PreviewNotification *notification,
                             unsigned long now_ms)
{
    return notification->duration_ms != 0 &&
        now_ms - notification->started_ms >= notification->duration_ms;
}

static void
preview_notification_clear(PreviewNotification *notification)
{
    notification->text[0] = '\0';
    notification->started_ms = 0;
    notification->duration_ms = 0;
}

#endif
