.PHONY=all install run clean

CC=gcc

SRC=main.c \
	xdg-shell-client-protocol.c \
	xdg-shell.c

BINS ?= hooktty

CFLAGS=
PREFIX ?= /usr/local
LDFLAGS=-lwayland-client

PRO=xdg-shell.xml
PRO_OUT=xdg-shell-client-protocol.h xdg-shell-client-protocol.c

all: $(BINS)

$(BINS): clean $(SRC) $(PRO_OUT)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $(BINS) $(SRC)

$(PRO_OUT): $(PRO)
	wayland-scanner client-header xdg-shell.xml xdg-shell-client-protocol.h
	wayland-scanner private-code xdg-shell.xml xdg-shell-client-protocol.c

install: all
	install -D -t $(DESTDIR)$(PREFIX)/bin $(BINS)

run: clean $(BINS)
	./$(BINS)

clean:
	$(RM) $(BINS)
