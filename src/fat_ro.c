/*
 * fat_ro.c -- minimal read-only FAT12/FAT16 with VFAT long filenames.
 * Pure C over storage_read_blocks(); host-testable (test/host/test_fat.c).
 */
#include <string.h>
#include "fat_ro.h"
#include "storage.h"

#define BS 512u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fat_mount(struct fat_vol *v)
{
    uint8_t b[BS];
    if (storage_read_blocks(0, b, 1))
        return -1;
    if (b[510] != 0x55 || b[511] != 0xAA)
        return -2;
    if (rd16(&b[11]) != BS)                 /* bytes per sector */
        return -3;

    v->sec_per_clus = b[13];
    v->reserved     = rd16(&b[14]);
    uint8_t  nfats  = b[16];
    uint16_t rootent = rd16(&b[17]);
    uint32_t totsec = rd16(&b[19]);
    if (!totsec)
        totsec = rd32(&b[32]);
    uint16_t fatsz  = rd16(&b[22]);
    if (!v->sec_per_clus || !nfats || !fatsz || !rootent || !totsec)
        return -4;

    v->total_secs = totsec;
    v->fat_start  = v->reserved;
    v->root_start = v->reserved + (uint32_t)nfats * fatsz;
    v->root_secs  = ((uint32_t)rootent * 32 + BS - 1) / BS;
    v->data_start = v->root_start + v->root_secs;
    if (v->data_start >= totsec)
        return -5;
    v->clusters   = (totsec - v->data_start) / v->sec_per_clus;
    v->fat_type   = (v->clusters < 4085) ? 12 : 16;
    return 0;
}

/* FAT chain lookup with a one-sector cache. */
static uint32_t next_cluster(const struct fat_vol *v, uint32_t clus)
{
    static uint8_t  fsec[BS];
    static uint32_t fsec_lba = UINT32_MAX;
    uint32_t off = (v->fat_type == 16) ? clus * 2 : clus + clus / 2;
    uint32_t lba = v->fat_start + off / BS;
    uint32_t idx = off % BS;

    if (lba != fsec_lba) {
        if (storage_read_blocks(lba, fsec, 1))
            return 1;                       /* 1 = invalid cluster */
        fsec_lba = lba;
    }
    if (v->fat_type == 16)
        return rd16(&fsec[idx]);

    /* FAT12: 12-bit entries may straddle a sector boundary. */
    uint8_t lo = fsec[idx], hi;
    if (idx + 1 < BS) {
        hi = fsec[idx + 1];
    } else {
        if (storage_read_blocks(lba + 1, fsec, 1))
            return 1;
        fsec_lba = lba + 1;
        hi = fsec[0];
    }
    uint16_t ent = (uint16_t)(lo | (hi << 8));
    return (clus & 1) ? (ent >> 4) : (ent & 0x0FFF);
}

static int end_of_chain(const struct fat_vol *v, uint32_t clus)
{
    return (v->fat_type == 16) ? (clus >= 0xFFF8) : (clus >= 0x0FF8);
}

/* ---- root directory walk with LFN assembly ------------------------------ */

struct dirwalk {
    char name[FAT_NAME_MAX];    /* assembled LFN (empty if none)            */
    int  have_lfn;
};

static void lfn_collect(struct dirwalk *w, const uint8_t *e)
{
    /* 13 UCS-2 chars per LFN entry at fixed offsets; ord field gives the
     * 1-based slot. Non-ASCII is replaced by '?'. */
    static const uint8_t off[13] = { 1,3,5,7,9, 14,16,18,20,22,24, 28,30 };
    unsigned ord = (e[0] & 0x1F);
    if (!ord || ord > FAT_NAME_MAX / 13)
        return;
    w->have_lfn = 1;
    unsigned base = (ord - 1) * 13;
    for (unsigned i = 0; i < 13 && base + i < FAT_NAME_MAX - 1; i++) {
        uint16_t c = rd16(&e[off[i]]);
        if (c == 0x0000 || c == 0xFFFF) {
            w->name[base + i] = 0;
            return;
        }
        w->name[base + i] = (c < 0x80) ? (char)c : '?';
    }
}

static void short_name(const uint8_t *e, char *out)
{
    int n = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++)
        out[n++] = (char)e[i];
    if (e[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++)
            out[n++] = (char)e[i];
    }
    out[n] = 0;
}

/* cb2 also receives the raw entry for fat_open. */
static int walk_root(const struct fat_vol *v,
                     int (*cb2)(const char *name, const uint8_t *ent, void *),
                     void *arg)
{
    uint8_t sec[BS];
    struct dirwalk w = { .have_lfn = 0 };
    int visited = 0;

    for (uint32_t s = 0; s < v->root_secs; s++) {
        if (storage_read_blocks(v->root_start + s, sec, 1))
            return -1;
        for (unsigned i = 0; i < BS; i += 32) {
            const uint8_t *e = &sec[i];
            if (e[0] == 0x00)
                return visited;             /* end of directory */
            if (e[0] == 0xE5) {             /* deleted */
                w.have_lfn = 0;
                continue;
            }
            if ((e[11] & 0x3F) == 0x0F) {   /* LFN part */
                lfn_collect(&w, e);
                continue;
            }
            if (e[11] & 0x08 || e[11] & 0x10) {  /* volume label / dir */
                w.have_lfn = 0;
                continue;
            }
            char sname[13];
            short_name(e, sname);
            const char *name = w.have_lfn ? w.name : sname;
            visited++;
            int rc = cb2(name, e, arg);
            w.have_lfn = 0;
            memset(w.name, 0, sizeof(w.name));
            if (rc)
                return visited;
        }
    }
    return visited;
}

/* ---- public API --------------------------------------------------------- */

struct list_ctx {
    int (*cb)(const char *, uint32_t, void *);
    void *arg;
};

static int list_cb(const char *name, const uint8_t *e, void *arg)
{
    struct list_ctx *c = arg;
    return c->cb(name, rd32(&e[28]), c->arg);
}

int fat_list(const struct fat_vol *v,
             int (*cb)(const char *name, uint32_t size, void *arg), void *arg)
{
    struct list_ctx c = { cb, arg };
    return walk_root(v, list_cb, &c);
}

static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
    }
    return *a == *b;
}

struct open_ctx {
    const char *want;
    struct fat_file *f;
    int found;
};

static int open_cb(const char *name, const uint8_t *e, void *arg)
{
    struct open_ctx *c = arg;
    if (!ieq(name, c->want))
        return 0;
    c->f->size       = rd32(&e[28]);
    c->f->first_clus = rd16(&e[26]);
    c->found = 1;
    return 1;
}

int fat_open(const struct fat_vol *v, const char *name, struct fat_file *f)
{
    struct open_ctx c = { name, f, 0 };
    memset(f, 0, sizeof(*f));
    f->vol = v;
    if (walk_root(v, open_cb, &c) < 0)
        return -1;
    if (!c.found)
        return -2;
    f->cur_clus = f->first_clus;
    f->clus_pos = 0;
    return 0;
}

int fat_read(struct fat_file *f, void *buf, size_t len)
{
    const struct fat_vol *v = f->vol;
    uint8_t sec[BS];
    uint8_t *dst = buf;
    size_t done = 0;
    uint32_t clus_bytes = (uint32_t)v->sec_per_clus * BS;

    if (f->pos >= f->size)
        return 0;
    if (len > f->size - f->pos)
        len = f->size - f->pos;

    while (done < len) {
        /* advance the cluster cursor to the one containing pos */
        while (f->pos - f->clus_pos >= clus_bytes) {
            uint32_t nx = next_cluster(v, f->cur_clus);
            if (nx < 2 || end_of_chain(v, nx))
                return (int)done;
            f->cur_clus = nx;
            f->clus_pos += clus_bytes;
        }
        uint32_t in_clus = f->pos - f->clus_pos;
        uint32_t lba = v->data_start +
                       (f->cur_clus - 2) * v->sec_per_clus + in_clus / BS;
        if (storage_read_blocks(lba, sec, 1))
            return -1;
        uint32_t in_sec = in_clus % BS;
        uint32_t n = BS - in_sec;
        if (n > len - done)
            n = (uint32_t)(len - done);
        memcpy(dst + done, &sec[in_sec], n);
        done   += n;
        f->pos += n;
    }
    return (int)done;
}
