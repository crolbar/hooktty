.PHONY=all install run clean

CC=gcc

SRC=main.c \
	xdg-shell-client-protocol.c \

BINS ?= main

CFLAGS=
PREFIX ?= /usr/local
LDFLAGS=-lwayland-client

PRO=xdg-shell.xml
PRO_OUT=xdg-shell-client-protocol.h xdg-shell-client-protocol.c

all: $(BINS)

$(BINS): $(SRC) $(PRO_OUT)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $(BINS) $(SRC)

simple-shm: os-compatibility.c simple-shm.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o simple-shm os-compatibility.c simple-shm.c xdg-shell-client-protocol.c
	./simple-shm

$(PRO_OUT): $(PRO)
	wayland-scanner client-header xdg-shell.xml xdg-shell-client-protocol.h
	wayland-scanner private-code xdg-shell.xml xdg-shell-client-protocol.c

install: all
	install -D -t $(DESTDIR)$(PREFIX)/bin $(BINS)

run: clean $(BINS)
	./$(BINS)

clean:
	$(RM) $(BINS)
