#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <barcelona/Barc_Gpio.h>
#include "power_button_policy.h"

#define ID_BUTTON 1
#define ID_LOWBATT 8192
#define DEFAULT_CONFIG "/mnt/sdcard/opentom/etc/power-button.cfg"
#define CONFIG_LINE_SIZE 256
#define ACTION_PATH_SIZE 128
#define MAX_PRESS_MS 60000UL

typedef struct {
	unsigned long quick_max_ms;
	unsigned long suspend_min_ms;
	char quick_action[ACTION_PATH_SIZE];
	char suspend_action[ACTION_PATH_SIZE];
	char low_battery_action[ACTION_PATH_SIZE];
} ButtonConfig;

static volatile sig_atomic_t config_reload_requested;

static void request_config_reload(int signal_number)
{
	(void)signal_number;
	config_reload_requested = 1;
}

int pollStatus(int devHWStatus)
{
	HARDWARE_STATUS hwstatus;

	if (ioctl(devHWStatus, IOR_HWSTATUS, &hwstatus) < 0) {
		fprintf(stderr, "Could not read hardware status: %s", strerror(errno));
		return -1;
	}

	return hwstatus.u8InputStatus;
}

int resetStatus(int devHWStatus)
{
	UINT32 status = 0;

	if (ioctl(devHWStatus, IOW_RESET_ONOFF_STATE, &status) < 0) {
		fprintf(stderr, "Could not reset button status: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static char *trim(char *text)
{
	char *end;

	while (*text != '\0' && isspace((unsigned char)*text))
		text++;
	end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1]))
		end--;
	*end = '\0';
	return text;
}

static int parse_milliseconds(const char *text, unsigned long *value)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' ||
	    parsed > MAX_PRESS_MS)
		return -1;
	*value = parsed;
	return 0;
}

static int copy_action(char *target, const char *source)
{
	size_t length = strlen(source);
	size_t i;

	if (length == 0 || length >= ACTION_PATH_SIZE)
		return -1;
	for (i = 0; i < length; i++) {
		if (isspace((unsigned char)source[i]))
			return -1;
	}
	memcpy(target, source, length + 1);
	return 0;
}

static int load_button_config(const char *path, ButtonConfig *config)
{
	FILE *file;
	char line[CONFIG_LINE_SIZE];
	unsigned int seen = 0;
	unsigned int line_number = 0;
	int invalid = 0;

	file = fopen(path, "r");
	if (file == NULL) {
		perror(path);
		return -1;
	}
	while (fgets(line, sizeof(line), file) != NULL) {
		char *entry;
		char *equals;
		unsigned int bit;

		line_number++;
		entry = trim(line);
		if (*entry == '\0' || *entry == '#')
			continue;
		equals = strchr(entry, '=');
		if (equals == NULL) {
			invalid = 1;
			break;
		}
		*equals++ = '\0';
		entry = trim(entry);
		equals = trim(equals);
		if (strcmp(entry, "quick_max_ms") == 0) {
			bit = 1U;
			if ((seen & bit) != 0 ||
			    parse_milliseconds(equals, &config->quick_max_ms) != 0)
				invalid = 1;
		} else if (strcmp(entry, "suspend_min_ms") == 0) {
			bit = 2U;
			if ((seen & bit) != 0 ||
			    parse_milliseconds(equals, &config->suspend_min_ms) != 0)
				invalid = 1;
		} else if (strcmp(entry, "quick_action") == 0) {
			bit = 4U;
			if ((seen & bit) != 0 ||
			    copy_action(config->quick_action, equals) != 0)
				invalid = 1;
		} else if (strcmp(entry, "suspend_action") == 0) {
			bit = 8U;
			if ((seen & bit) != 0 ||
			    copy_action(config->suspend_action, equals) != 0)
				invalid = 1;
		} else if (strcmp(entry, "low_battery_action") == 0) {
			bit = 16U;
			if ((seen & bit) != 0 ||
			    copy_action(config->low_battery_action, equals) != 0)
				invalid = 1;
		} else {
			invalid = 1;
			break;
		}
		if (invalid)
			break;
		seen |= bit;
	}
	if (ferror(file))
		invalid = 1;
	if (fclose(file) != 0)
		invalid = 1;
	if (invalid || seen != 31U ||
	    config->quick_max_ms >= config->suspend_min_ms) {
		fprintf(stderr, "%s:%u: invalid power-button configuration\n",
			path, line_number);
		return -1;
	}
	return 0;
}

static int run_job(const char *job, char *envp[])
{
	pid_t child;
	int status;
	char *arguments[2];

	if (job == NULL)
		return 0;
	child = fork();
	if (child < 0) {
		perror("fork");
		return -1;
	}
	if (child == 0) {
		arguments[0] = (char *)job;
		arguments[1] = NULL;
		execve(job, arguments, envp);
		perror(job);
		_exit(127);
	}
	while (waitpid(child, &status, 0) < 0) {
		if (errno != EINTR) {
			perror("waitpid");
			return -1;
		}
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int wait_button_duration(int devHWStatus, char *envp[],
				const char *config_path)
{
	struct pollfd descriptor;
	ButtonConfig config;
	GPIO_BUTTON_EVENT event;
	UINT32 last_sequence;
	int result;

	if (load_button_config(config_path, &config) != 0)
		return -1;
	if (ioctl(devHWStatus, IOR_BUTTON_EVENT, &event) < 0) {
		fprintf(stderr, "Cannot read GPIO button events: %s\n",
			strerror(errno));
		return -1;
	}
	last_sequence = event.sequence;
	if (resetStatus(devHWStatus) != 0)
		return -1;
	descriptor.fd = devHWStatus;
	descriptor.events = POLLIN;
	for (;;) {
		int ready;

		descriptor.revents = 0;
		ready = poll(&descriptor, 1, -1);
		if (ready < 0) {
			if (errno == EINTR)
			{
				if (config_reload_requested) {
					ButtonConfig updated_config;

					config_reload_requested = 0;
					if (load_button_config(config_path,
							       &updated_config) == 0) {
						config = updated_config;
						fprintf(stderr,
							"power_button: configuration reloaded\n");
					}
				}
				continue;
			}
			perror("poll");
			return -1;
		}
		if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			fprintf(stderr, "GPIO button event device disconnected\n");
			return -1;
		}
		result = pollStatus(devHWStatus);
		if (result < 0)
			return -1;
		if (result & ID_LOWBATT) {
			fprintf(stderr, "power_button: low-battery action\n");
			if (run_job(config.low_battery_action, envp) != 0)
				return -1;
		}
		if (result & ID_BUTTON) {
			if (ioctl(devHWStatus, IOR_BUTTON_EVENT, &event) < 0) {
				fprintf(stderr, "Cannot read GPIO button event: %s\n",
					strerror(errno));
				return -1;
			}
			if (event.sequence != last_sequence) {
				const char *job = NULL;
				int action;

				last_sequence = event.sequence;
				action = power_button_action_for_duration(
					event.duration_ms, config.quick_max_ms,
					config.suspend_min_ms);
				if (action == POWER_BUTTON_ACTION_QUICK) {
					job = config.quick_action;
				} else if (action == POWER_BUTTON_ACTION_SUSPEND) {
					job = config.suspend_action;
				}
				if (run_job(job, envp) != 0)
					return -1;
			} else {
				fprintf(stderr,
					"power_button: non-button shutdown event\n");
				if (run_job(config.suspend_action, envp) != 0)
					return -1;
			}
			if (resetStatus(devHWStatus) != 0)
				return -1;
		}
	}
}

int waitButton(int devHWStatus, char *envp[], char *bjob, char *ljob)
{
	int result, cpid;
	char *job;

	while (1) {
		resetStatus(devHWStatus);
		sleep(1);
		while (((result = pollStatus(devHWStatus)) & (ID_LOWBATT | ID_BUTTON)) == 0) {
			sleep(1);
		}
		job = ljob;
		if (result & ID_BUTTON) {
			job = bjob;
		}

		if (job == NULL) {
			if ((result & ID_BUTTON) == 0) {
				continue;
			}
			return result;
		}
		cpid = fork();
		if (cpid == -1) {
			perror("fork");
			return -1;
		}
		if (cpid == 0) {
			char *arguments[2];

			arguments[0] = job;
			arguments[1] = NULL;
			execve(job, arguments, envp);
			perror(job);
			_exit(127);
		}
		wait(NULL);
	}
	return 0;
}

int main(int argc, char **argv, char *envp[])
{
	int fd = open("/dev/hwstatus", O_RDONLY);
	if (fd == -1) {
		perror("/dev/hwstatus");
		return 2;
	}

	if (argc > 1) {
		if (strncmp(argv[1], "-r", 2) == 0) {
			if (resetStatus(fd)) {
				return -1;
			}
			printf("Resetting ON/OFF state: OK\n");
		} else if (strncmp(argv[1], "-b", 2) == 0) {
			return waitButton(fd, envp, (argc > 2 ? argv[2] : NULL), (argc > 3 ? argv[3] : NULL));
		} else if (strncmp(argv[1], "-d", 2) == 0) {
			struct sigaction action;

			memset(&action, 0, sizeof(action));
			action.sa_handler = request_config_reload;
			sigemptyset(&action.sa_mask);
			if (sigaction(SIGHUP, &action, NULL) != 0) {
				perror("sigaction");
				return 1;
			}
			return wait_button_duration(fd, envp,
				argc > 2 ? argv[2] : DEFAULT_CONFIG);
		} else {
			printf("Usage: %s [-r]\n", argv[0]);
			printf("Usage: %s -b [button_command [low_batt_command]]\n", argv[0]);
			printf("Usage: %s -d [button_config]\n", argv[0]);
			return -1;
		}
	} else {
		printf("Input status: %d\n", pollStatus(fd));
	}

	return 0;
}
