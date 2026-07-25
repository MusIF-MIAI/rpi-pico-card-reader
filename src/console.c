/*
 * console.c -- CDC-ACM command shell (core0).
 *
 * ASCII, newline-terminated; replies "OK ..." / "ERR <reason>".
 * Command set: ARCHITECTURE.md sec. 9.
 *
 * STATUS: stub (step 2). The table below is the contract.
 */
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "ipc.h"

struct cmd {
    const char *name;
    const char *help;
};

static const struct cmd commands[] = {
    { "ls",           "list .cap files on the FAT region" },
    { "batches",      "list built-in SAT batch recipes" },
    { "arm",          "arm <file|batch> [--raw]: parse (+surgery) -> RAM" },
    { "disarm",       "end the feed session" },
    { "rewind",       "back to card 0" },
    { "eject",        "skip current card" },
    { "status",       "FSM state, cursor, mode, counters, tunables" },
    { "set",          "set <W|G|S|D|finto|policy> <value>" },
    { "save",         "persist tunables to flash" },
    { "passive",      "passive on|off: tri-state the output shifters" },
    { "trace",        "trace on|off: live event dump" },
    { "inject-error", "assert LUREN until the machine sends 0x47" },
    { "version",      "firmware + protocol versions" },
    { "help",         "this text" },
};

void console_poll(void)
{
    /* TODO(step 2): read CDC line, tokenize, dispatch. arm: storage ->
     * cap_parse_* -> surgery -> g_deck[slot], then IPC_ARM to core1. */
    (void)commands;
}
