#include <assert.h>
#include <string.h>

#include "notification_event.h"

int
main(void)
{
    PreviewNotification notification;
    char text[PREVIEW_NOTIFICATION_TEXT_MAX + 1];
    const char *packet = "OT1|N|15|Test message received";
    int ttl = 0;

    memset(&notification, 0, sizeof(notification));
    assert(preview_parse_notification(packet, strlen(packet), text,
                                      sizeof(text), &ttl));
    assert(ttl == 15);
    assert(strcmp(text, "Test message received") == 0);
    preview_notification_set(&notification, text, ttl, 1000UL);
    assert(preview_notification_alpha(&notification, 1000UL) == 255U);
    assert(preview_notification_alpha(&notification, 14499UL) == 255U);
    assert(preview_notification_alpha(&notification, 14500UL) == 255U);
    assert(preview_notification_alpha(&notification, 15250UL) >= 126U);
    assert(preview_notification_alpha(&notification, 15995UL) > 0U);
    assert(preview_notification_alpha(&notification, 16000UL) == 0U);
    assert(preview_notification_weather_alpha(255U) == 0U);
    assert(preview_notification_weather_alpha(128U) == 0U);
    assert(preview_notification_weather_alpha(1U) == 0U);
    assert(preview_notification_weather_alpha(0U) == 255U);
    assert(!preview_notification_expired(&notification, 15999UL));
    assert(preview_notification_expired(&notification, 16000UL));
    preview_notification_clear(&notification);
    assert(preview_notification_alpha(&notification, 20000UL) == 0U);
    assert(!preview_notification_expired(&notification, 20000UL));
    assert(!preview_parse_notification("OT1|N|0|bad", 11, text,
                                       sizeof(text), &ttl));
    assert(!preview_parse_notification("OT1|N|61|bad", 12, text,
                                       sizeof(text), &ttl));
    assert(!preview_parse_notification("OT1|N|15|line\nbreak", 18, text,
                                       sizeof(text), &ttl));
    assert(!preview_parse_notification(
        "OT1|N|15|123456789012345678901234567890123", 43, text,
        sizeof(text), &ttl));
    return 0;
}
