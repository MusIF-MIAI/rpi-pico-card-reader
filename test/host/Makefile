# Host-built unit tests for the pure-C firmware core (no pico-sdk needed).

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -O1 -g
CFLAGS  += -I../../include

SRC = ../../src/deck.c ../../src/transcode.c ../../src/surgery.c
DECK ?= ../../../gemu/Site_Acceptance_Test/funktionalcpu.cap

all: test_deck

test_deck: test_deck.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

check: test_deck
	./test_deck $(DECK)

clean:
	rm -f test_deck

.PHONY: all check clean
