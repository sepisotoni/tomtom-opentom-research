#ifndef POWER_BUTTON_POLICY_H
#define POWER_BUTTON_POLICY_H

#define POWER_BUTTON_ACTION_NONE 0
#define POWER_BUTTON_ACTION_QUICK 1
#define POWER_BUTTON_ACTION_SUSPEND 2

static int
power_button_action_for_duration(unsigned long duration_ms,
				 unsigned long quick_max_ms,
				 unsigned long suspend_min_ms)
{
	if (duration_ms <= quick_max_ms)
		return POWER_BUTTON_ACTION_QUICK;
	if (duration_ms >= suspend_min_ms)
		return POWER_BUTTON_ACTION_SUSPEND;
	return POWER_BUTTON_ACTION_NONE;
}

#endif
