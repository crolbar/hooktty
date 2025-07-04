.PHONY=all install run clean

CC=gcc

SRC=main.c \
	xdg-shell-client-protocol.c \
	xdg-shell.c \
	seat.c

BINS ?= hooktty

CFLAGS=`pkg-config --cflags --libs freetype2`
PREFIX ?= /usr/local
LDFLAGS=-lwayland-client -lxkbcommon -lfontconfig -lpixman-1

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
