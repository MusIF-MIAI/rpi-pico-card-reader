/*
 * console.c -- CDC-ACM command shell (core0).
 *
 * ASCII lines, echo, backspace; replies "OK ..." / "ERR <reason>".
 * Command set per docs/ARCHITECTURE.md sec. 9.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tusb.h"

#include "config.h"
#include "console.h"
#include "deckload.h"
#include "ge_proto.h"
#include "ipc.h"
#include "storage.h"

void monitor_set_trace(int on);
int  monitor_trace(void);
int  cfg_save(void);

#define LINE_MAX 96

static char line[LINE_MAX];
static unsigned linelen;
static struct fat_vol vol;
static int vol_ok;

void con_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0 || !tud_cdc_connected())
        return;
    for (int off = 0; off < n; ) {
        uint32_t w = tud_cdc_write(buf + off, (uint32_t)(n - off));
        if (!w) {
            tud_cdc_write_flush();
            tud_task();
            if (!tud_cdc_connected())
                return;
            continue;
        }
        off += (int)w;
    }
    tud_cdc_write_flush();
}

static int mount(void)
{
    if (!vol_ok)
        vol_ok = (fat_mount(&vol) == 0);
    return vol_ok;
}

/* ---- commands ----------------------------------------------------------- */

static int ls_cb(const char *name, uint32_t size, void *arg)
{
    (void)arg;
    con_printf("  %-40s %lu B\r\n", name, (unsigned long)size);
    return 0;
}

static void cmd_ls(void)
{
    vol_ok = 0;                       /* re-mount: host may have rewritten */
    if (!mount()) {
        con_printf("ERR no FAT volume (format the USB drive first)\r\n");
        return;
    }
    int n = fat_list(&vol, ls_cb, NULL);
    if (n < 0)
        con_printf("ERR reading root directory\r\n");
    else
        con_printf("OK %d file(s)\r\n", n);
}

static void cmd_batches(void)
{
    for (int i = 0; i < surgery_n_batches; i++) {
        const struct surgery_batch *b = &surgery_batches[i];
        con_printf("  %-24s %s (", b->name, b->title);
        for (int s = 0; s < b->n_src; s++)
            con_printf("%s%s", s ? " + " : "", b->src[s].file);
        con_printf(")\r\n");
    }
    con_printf("OK %d batch(es)\r\n", surgery_n_batches);
}

static void cmd_arm(char *arg)
{
    if (g_feeder_status.state != FS_DISARMED) {
        con_printf("ERR already armed -- disarm first\r\n");
        return;
    }
    if (!arg || !*arg) {
        con_printf("ERR usage: arm <file|batch> [--raw]\r\n");
        return;
    }
    char *name = strtok(arg, " ");
    char *flag = strtok(NULL, " ");
    int raw = flag && !strcmp(flag, "--raw");

    storage_flush();
    vol_ok = 0;
    if (!mount()) {
        con_printf("ERR no FAT volume\r\n");
        return;
    }

    int rc;
    const struct surgery_batch *b = deckload_find_batch(name);
    if (b)
        rc = deckload_batch(&vol, b, &g_deck[0], &g_deck[1]);
    else
        rc = deckload_prepare(&vol, name, raw, &g_deck[0], &g_deck[1]);
    if (rc) {
        con_printf("ERR %s: %s\r\n", name,
                   rc == -1 ? "not found / unreadable" : "parse/surgery failed");
        return;
    }
    ipc_send(IPC_ARM, 0, 0);
    strncpy(g_cfg.last_deck, name, sizeof(g_cfg.last_deck) - 1);
    con_printf("OK armed '%s': %u cards, loader at %d%s\r\n",
               g_deck[0].name, g_deck[0].n_cards, g_deck[0].loader_card,
               raw ? " (raw)" : "");
}

static const char *state_name(uint8_t s)
{
    switch (s) {
    case FS_DISARMED:   return "DISARMED";
    case FS_ARMED_WAIT: return "ARMED_WAIT";
    case FS_PRESENTING: return "PRESENTING";
    case FS_CARD_DONE:  return "CARD_DONE";
    case FS_DONE:       return "DONE";
    case FS_ERROR:      return "ERROR";
    default:            return "?";
    }
}

static void cmd_status(void)
{
    const struct feeder_status *st = &g_feeder_status;
    con_printf("state:    %s\r\n", state_name(st->state));
    if (st->state != FS_DISARMED)
        con_printf("deck:     '%s' (%u cards, loader %d)\r\n"
                   "cursor:   card %u col %u half %u\r\n",
                   g_deck[0].name, g_deck[0].n_cards, g_deck[0].loader_card,
                   st->card, st->col, st->half);
    con_printf("mode:     %u  (0=NORM 1=BIN 2=HEX 3=COLBIN)\r\n"
               "counters: cmds %lu feeds %lu autofeeds %lu nibbles %lu unknown %lu\r\n"
               "timing:   W=%u G=%u S=%u ticks (100ns)  D=%u us  finto=%u us\r\n"
               "policy:   post_loader_colbin=%u auto_rewind=%u/%us\r\n",
               st->mode,
               (unsigned long)st->n_cmds, (unsigned long)st->n_feeds,
               (unsigned long)st->n_autofeeds, (unsigned long)st->n_nibbles,
               (unsigned long)st->n_unknown_cmd,
               g_cfg.w_ticks, g_cfg.g_ticks, g_cfg.s_ticks,
               g_cfg.d_us, g_cfg.finin_to_us,
               g_cfg.post_loader_colbin, g_cfg.auto_rewind,
               g_cfg.auto_rewind_s);
}

static void cmd_set(char *arg)
{
    char *name = arg ? strtok(arg, " ") : NULL;
    char *vstr = name ? strtok(NULL, " ") : NULL;
    if (!name || !vstr) {
        con_printf("ERR usage: set <w|g|s|d|finto|plc|arw> <value>\r\n");
        return;
    }
    unsigned v = (unsigned)strtoul(vstr, NULL, 0);
    uint16_t *field = NULL;
    uint8_t param = 0;

    if      (!strcmp(name, "w"))     { field = &g_cfg.w_ticks;     param = PARAM_W_TICKS; }
    else if (!strcmp(name, "g"))     { field = &g_cfg.g_ticks;     param = PARAM_G_TICKS; }
    else if (!strcmp(name, "s"))     { field = &g_cfg.s_ticks;     param = PARAM_S_TICKS; }
    else if (!strcmp(name, "d"))     { field = &g_cfg.d_us;        param = PARAM_D_US; }
    else if (!strcmp(name, "finto")) { field = &g_cfg.finin_to_us; param = PARAM_FININ_TO_US; }
    else if (!strcmp(name, "plc"))   { g_cfg.post_loader_colbin = v & 1; param = PARAM_POLICY; }
    else if (!strcmp(name, "arw"))   { g_cfg.auto_rewind = v & 1;        param = PARAM_POLICY; }
    else {
        con_printf("ERR unknown parameter '%s'\r\n", name);
        return;
    }
    if (field)
        *field = (uint16_t)v;
    uint16_t policy = (uint16_t)(g_cfg.post_loader_colbin |
                                 (g_cfg.auto_rewind << 1));
    ipc_send(IPC_SET_PARAM, param,
             param == PARAM_POLICY ? policy : (uint16_t)v);
    con_printf("OK %s = %u ('save' to persist)\r\n", name, v);
}

static void help(void)
{
    con_printf(
      "ls | batches | arm <file|batch> [--raw] | disarm | rewind | eject\r\n"
      "status | set <w|g|s|d|finto|plc|arw> <v> | save\r\n"
      "trace on|off | inject-error | version | help\r\n");
}

static void execute(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg)
        *arg++ = 0;
    if (!*cmd)                        { }
    else if (!strcmp(cmd, "ls"))      cmd_ls();
    else if (!strcmp(cmd, "batches")) cmd_batches();
    else if (!strcmp(cmd, "arm"))     cmd_arm(arg);
    else if (!strcmp(cmd, "disarm")) { ipc_send(IPC_DISARM, 0, 0); con_printf("OK\r\n"); }
    else if (!strcmp(cmd, "rewind")) { ipc_send(IPC_REWIND, 0, 0); con_printf("OK\r\n"); }
    else if (!strcmp(cmd, "eject"))  { ipc_send(IPC_EJECT, 0, 0);  con_printf("OK\r\n"); }
    else if (!strcmp(cmd, "status"))  cmd_status();
    else if (!strcmp(cmd, "set"))     cmd_set(arg);
    else if (!strcmp(cmd, "save"))    con_printf(cfg_save() ? "ERR save failed\r\n" : "OK saved\r\n");
    else if (!strcmp(cmd, "trace"))  { monitor_set_trace(!(arg && !strcmp(arg, "off")));
                                       con_printf("OK trace %s\r\n", monitor_trace() ? "on" : "off"); }
    else if (!strcmp(cmd, "inject-error")) { ipc_send(IPC_INJECT_ERROR, 0, 0); con_printf("OK LUREN asserted\r\n"); }
    else if (!strcmp(cmd, "version")) con_printf("ge120-cardreader step2 (" __DATE__ ")\r\n");
    else if (!strcmp(cmd, "help"))    help();
    else con_printf("ERR unknown command '%s' (try 'help')\r\n", cmd);
    con_printf("ge120> ");
}

void console_poll(void)
{
    static int greeted;
    if (!tud_cdc_connected()) {
        greeted = 0;
        return;
    }
    if (!greeted) {
        greeted = 1;
        con_printf("\r\nGE-120 card reader simulator -- 'help' for commands\r\n"
                   "ge120> ");
    }
    while (tud_cdc_available()) {
        char c;
        if (tud_cdc_read(&c, 1) != 1)
            break;
        if (c == '\r' || c == '\n') {
            con_printf("\r\n");
            line[linelen] = 0;
            linelen = 0;
            execute(line);
        } else if (c == 0x08 || c == 0x7F) {
            if (linelen) {
                linelen--;
                con_printf("\b \b");
            }
        } else if (c >= 0x20 && linelen < LINE_MAX - 1) {
            line[linelen++] = c;
            con_printf("%c", c);
        }
    }
}
