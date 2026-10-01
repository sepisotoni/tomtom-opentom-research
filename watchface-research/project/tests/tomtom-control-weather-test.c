#define main tomtom_control_daemon_main
#include "../src/opentom_skel/bin/tomtom-control.c"
#undef main

#include <assert.h>

int
main(void)
{
    int condition;
    int temperature;
    int alert;
    int precipitation;
    int noteworthy;
    int high;
    int low;
    char icon_key[WEATHER_ICON_KEY_MAX];

    assert(parse_weather_values(
        "4 14 3 75 1 18 11 rain",
        &condition, &temperature, &alert, &precipitation, &noteworthy,
        &high, &low, icon_key));
    assert(condition == 4 && temperature == 14);
    assert(alert == 3 && precipitation == 75 && noteworthy == 1);
    assert(high == 18 && low == 11);
    assert(strcmp(icon_key, "rain") == 0);

    assert(parse_weather_values(
        "2 20 0 15 0",
        &condition, &temperature, &alert, &precipitation, &noteworthy,
        &high, &low, icon_key));
    assert(high == 20 && low == 20);
    assert(strcmp(icon_key, "-") == 0);

    assert(!parse_weather_values(
        "4 14 3 75 1 11 18 rain",
        &condition, &temperature, &alert, &precipitation, &noteworthy,
        &high, &low, icon_key));
    assert(!parse_weather_values(
        "4 14 3 75 1 18 11 ../rain",
        &condition, &temperature, &alert, &precipitation, &noteworthy,
        &high, &low, icon_key));
    assert(!parse_weather_values(
        "4 14 3 75 1 18 11 rain extra",
        &condition, &temperature, &alert, &precipitation, &noteworthy,
        &high, &low, icon_key));
    puts("tomtom-control weather protocol tests passed");
    return 0;
}
