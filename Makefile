CC=cc -g
XFLAGS=-Wall -Werror -std=c11
LIBEVDEV=-levdev
INCDIR=/usr/include/libevdev-1.0/
CFLAGS=$(XFLAGS) $(LIBEVDEV) -I$(INCDIR)

all: keylogger

keylogger: main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f keylogger
