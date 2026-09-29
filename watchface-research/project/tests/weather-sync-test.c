#define main weather_sync_daemon_main
#include "../src/opentom_skel/bin/weather-sync.c"
#undef main

#include <assert.h>
#include <math.h>

static void
append_checksum(char *sentence, size_t capacity)
{
    size_t length = strlen(sentence);
    unsigned char checksum = 0;
    size_t i;

    for (i = 1; i < length; ++i)
        checksum ^= (unsigned char)sentence[i];
    assert(length + 4 < capacity);
    snprintf(sentence + length, capacity - length, "*%02X", checksum);
}

static int
parse_sentence(const char *source, GpsFix *fix)
{
    char sentence[192];

    assert(strlen(source) < sizeof(sentence) - 4);
    strcpy(sentence, source);
    append_checksum(sentence, sizeof(sentence));
    return parse_rmc(sentence, fix);
}

int
main(void)
{
    GpsFix fix;
    time_t started = 1000;
    WeatherSummary summary;
    char bad_checksum[] =
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B";

    assert(parse_sentence(
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W",
        &fix));
    assert(fabs(fix.latitude - 48.1173) < 0.00001);
    assert(fabs(fix.longitude - 11.5166667) < 0.00001);
    assert(!parse_sentence(
        "$GPRMC,123519,V,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W",
        &fix));
    assert(!parse_sentence(
        "$GPRMC,123519,A,9999.038,N,01131.000,E,022.4,084.4,230394,003.1,W",
        &fix));
    assert(!parse_sentence(
        "$GPRMC,123519,A,NaN,N,01131.000,E,022.4,084.4,230394,003.1,W",
        &fix));
    assert(!parse_rmc(bad_checksum, &fix));
    assert(parse_weather_summary(
        "TMW2 4 14 3 75 1 18 11 rain\n", &summary));
    assert(summary.temperature == 14);
    assert(summary.high == 18);
    assert(summary.low == 11);
    assert(strcmp(summary.icon_key, "rain") == 0);
    assert(!parse_weather_summary(
        "TMW2 4 14 3 75 1 11 18 ../rain\n", &summary));
    assert(!parse_weather_summary(
        "TMW2 4 14 3 75 1 18 11 rain extra\n", &summary));
    assert(!gps_fallback_due(started + 59, started, 0));
    assert(gps_fallback_due(started + 60, started, 0));
    assert(!gps_fallback_due(started + 59, started, started + 1));
    assert(gps_fallback_due(started + 61, started, started + 1));
    assert(!gps_fallback_due(started, started + 1, 0));
    puts("weather-sync GPS parser tests passed");
    return 0;
}
