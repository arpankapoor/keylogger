#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Linux Kernel Documentation/input/input.txt (3.2.4 evdev) */
#define NO_OF_EVENT_INTERFACES 32

struct libevdev *devs[NO_OF_EVENT_INTERFACES];

/*
 * Heuristic to check if given evdev device is a keyboard or not.
 * Linux Kernel Documentation/input/event-codes.txt
 *
 * @param dev An initialized evdev device
 * @return true if this device seems to be a keyboard, or false otherwise.
 */
static bool is_keyboard(struct libevdev *dev)
{
	return libevdev_has_event_type(dev, EV_KEY)
		&& !libevdev_has_event_type(dev, EV_REL)
		&& !libevdev_has_event_type(dev, EV_ABS)
		&& !libevdev_has_event_type(dev, EV_SW)
		&& !libevdev_has_event_type(dev, EV_FF)
		&& !libevdev_has_event_type(dev, EV_PWR)
		&& !libevdev_has_event_type(dev, EV_FF_STATUS);
}

/*
 * Initialize libevdev structs for devices that seem to be keyboards.
 *
 * @return Number of keyboard-like devices detected.
 */
static int init_keydevs(void)
{
	int keydevs = 0;
	const char *path_prefix = "/dev/input/event";
	const size_t path_len = strlen(path_prefix) + 10;

	for (int i = 0; i < NO_OF_EVENT_INTERFACES; i++) {
		int fd, rc;
		char path[path_len];
		struct libevdev *device = NULL;

		devs[i] = NULL;
		snprintf(path, path_len, "%s%d", path_prefix, i);

		fd = open(path, O_RDONLY);
		if (fd == -1) continue;

		rc = libevdev_new_from_fd(fd, &device);
		if (rc < 0) continue;

		if (is_keyboard(device)) {
			devs[i] = device;
			keydevs++;
		} else {
			libevdev_free(device);
			close(fd);
		}
	}

	return keydevs;
}

int main(int argc, char *argv[])
{
	int no_of_keydevs = 0;
	no_of_keydevs = init_keydevs();
	printf("Detected %d keyboard-like devices.\n", no_of_keydevs);
	return 0;
}
