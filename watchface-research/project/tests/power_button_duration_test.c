#include <stdio.h>
#define main power_button_program_main
#include "../applications/src/tools/power_button.c"
#undef main

static int
check_duration(const ButtonConfig *config, unsigned long duration_ms,
	       int expected)
{
	return power_button_action_for_duration(duration_ms, config->quick_max_ms,
					       config->suspend_min_ms) == expected;
}

int
main(void)
{
	ButtonConfig config;
	int failures;

	failures = load_button_config("src/opentom_skel/etc/power-button.cfg",
				      &config) != 0;
	if (!failures) {
		failures += config.quick_max_ms != 250;
		failures += config.suspend_min_ms != 400;
		failures += strcmp(config.quick_action,
				   "bin/watchface-toggle-info") != 0;
		failures += strcmp(config.suspend_action, "bin/suspend") != 0;
		failures += strcmp(config.low_battery_action, "bin/suspend") != 0;
		failures += !check_duration(&config, 0, POWER_BUTTON_ACTION_QUICK);
		failures += !check_duration(&config, 250, POWER_BUTTON_ACTION_QUICK);
		failures += !check_duration(&config, 251, POWER_BUTTON_ACTION_NONE);
		failures += !check_duration(&config, 399, POWER_BUTTON_ACTION_NONE);
		failures += !check_duration(&config, 400, POWER_BUTTON_ACTION_SUSPEND);
		failures += !check_duration(&config, 1000, POWER_BUTTON_ACTION_SUSPEND);
	}
	printf("11 config and duration checks, %d failures\n", failures);
	return failures != 0;
}
