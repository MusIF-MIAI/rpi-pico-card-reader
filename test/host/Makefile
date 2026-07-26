# Host-built unit tests for the pure-C firmware core (no pico-sdk needed).

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -O1 -g
CFLAGS  += -I../../include

DECKSRC = ../../src/deck.c ../../src/transcode.c ../../src/surgery.c
DECK   ?= ../../../gemu/Site_Acceptance_Test/funktionalcpu.cap

all: test_deck test_fat test_bin

test_deck: test_deck.c $(DECKSRC)
	$(CC) $(CFLAGS) -o $@ $^

# test_fat provides its own storage_read_blocks shim
test_fat: test_fat.c ../../src/fat_ro.c ../../src/deck.c
	$(CC) $(CFLAGS) -o $@ $^

test_bin: test_bin.c ../../src/bin2deck.c ../../src/transcode.c
	$(CC) $(CFLAGS) -o $@ $^

# FAT16 fixture: the reduced funktionalcpu deck (long filename) + this
# Makefile as a second, small 8.3-unfriendly file.
fixture.img: ../../tools/capstrip.py ../../tools/mkfatimg.py $(DECK)
	python3 ../../tools/capstrip.py $(DECK) -o . --check
	python3 ../../tools/mkfatimg.py -o $@ funktionalcpu.hex.cap Makefile

check: test_deck test_fat test_bin fixture.img
	./test_deck $(DECK)
	./test_fat fixture.img funktionalcpu.hex.cap funktionalcpu.hex.cap
	./test_bin

clean:
	rm -f test_deck test_fat test_bin fixture.img funktionalcpu.hex.cap

.PHONY: all check clean
