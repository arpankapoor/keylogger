CC=cc -g
XFLAGS=-Wall -std=c11
LIBEVDEV=-levdev
PTHREAD=-pthread
INCDIR=/usr/include/libevdev-1.0/
CFLAGS=$(XFLAGS) $(LIBEVDEV) $(PTHREAD) -I$(INCDIR)

all: keylogger

keylogger: main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f keylogger
