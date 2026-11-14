#include "aiosu_swap.h"

#include <string.h>
#include <libs/fatfs/ff.h>
#include <mem/heap.h>
#include <utils/types.h>
#include <utils/util.h>
#include "gfx/gfx.h"

#define SWAP_MARKER  "sd:/config/aio-switch-updater/swap_pending.txt"
#define SWAP_KEEP    "sd:/config/aio-switch-updater/swap_keep.txt"
#define SWAP_STAGING "aiosu_staging"
#define MAX_KEEP     512
#define KEEP_LEN     256

static char g_keep[MAX_KEEP][KEEP_LEN];
static int g_keepCount;
static u32 g_removed, g_failed, g_moved, g_skipped;

typedef struct {
    char *name;
    u8 isDir;
    u8 sys;
} SwapEntry;

typedef struct {
    SwapEntry *v;
    int n;
    int cap;
} SwapList;

/* ---------- small helpers ---------- */

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (lc(*a) != lc(*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* s starts with the first n chars of p (case-insensitive) */
static int istartsn(const char *s, const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (!s[i] || lc(s[i]) != lc(p[i])) return 0;
    }
    return 1;
}

static char *join(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *r = (char *)malloc(la + lb + 2);
    if (!r) return NULL;
    memcpy(r, a, la);
    size_t p = la;
    if (la && a[la - 1] != '/') r[p++] = '/';
    memcpy(r + p, b, lb);
    r[p + lb] = 0;
    return r;
}

static void list_free(SwapList *l)
{
    for (int i = 0; i < l->n; i++) free(l->v[i].name);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

static int list_read(const char *path, SwapList *l)
{
    DIR dir;
    FILINFO fno;
    l->v = NULL;
    l->n = l->cap = 0;
    FRESULT r = f_opendir(&dir, path);
    if (r != FR_OK) return (int)r;
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (l->n == l->cap) {
            int nc = l->cap ? l->cap * 2 : 32;
            SwapEntry *nv = (SwapEntry *)malloc((size_t)nc * sizeof(SwapEntry));
            if (!nv) break;
            if (l->v) {
                memcpy(nv, l->v, (size_t)l->n * sizeof(SwapEntry));
                free(l->v);
            }
            l->v = nv;
            l->cap = nc;
        }
        size_t len = strlen(fno.fname);
        char *nm = (char *)malloc(len + 1);
        if (!nm) break;
        memcpy(nm, fno.fname, len + 1);
        l->v[l->n].name = nm;
        l->v[l->n].isDir = (fno.fattrib & AM_DIR) ? 1 : 0;
        l->v[l->n].sys = (fno.fattrib & AM_SYS) ? 1 : 0;
        l->n++;
    }
    f_closedir(&dir);
    return FR_OK;
}

/* ---------- keep list ---------- */

static void normalize(char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++)
        if (s[i] == '\\') s[i] = '/';
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (istartsn(p, "sd:", 3)) p += 3;
    while (*p == '/') p++;
    memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n && (s[n - 1] == '/' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static void add_keep(const char *line)
{
    char tmp[KEEP_LEN];
    size_t n = strlen(line);
    if (n == 0 || n >= KEEP_LEN || g_keepCount >= MAX_KEEP) return;
    memcpy(tmp, line, n + 1);
    normalize(tmp);
    if (tmp[0] == 0) return; /* never accept "/" as an entry */
    memcpy(g_keep[g_keepCount++], tmp, strlen(tmp) + 1);
}

/* Returns 0 if the keep file is missing/unreadable: then NOTHING is wiped. */
static int load_keep(void)
{
    static const char *always[] = {
        "Nintendo", "emuMMC", "backup", "payload.bin", "payload.bin.aio",
        "config/aio-switch-updater", SWAP_STAGING,
        /* Android / Linux (L4T) and the hekate entries that boot them */
        "switchroot", "bootloader/ini", "bootloader/res",
    };
    g_keepCount = 0;
    for (unsigned i = 0; i < sizeof(always) / sizeof(always[0]); i++) add_keep(always[i]);

    FIL f;
    if (f_open(&f, SWAP_KEEP, FA_READ) != FR_OK) return 0;
    u32 size = (u32)f_size(&f);
    if (size == 0 || size > 65536) {
        f_close(&f);
        return 0;
    }
    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        f_close(&f);
        return 0;
    }
    UINT br = 0;
    FRESULT r = f_read(&f, buf, size, &br);
    f_close(&f);
    if (r != FR_OK) {
        free(buf);
        return 0;
    }
    buf[br] = 0;
    char *line = buf;
    for (UINT i = 0; i <= br; i++) {
        if (buf[i] == '\n' || buf[i] == 0) {
            buf[i] = 0;
            add_keep(line);
            line = buf + i + 1;
        }
    }
    free(buf);
    return 1;
}

static int is_kept(const char *rel)
{
    for (int i = 0; i < g_keepCount; i++)
        if (ieq(g_keep[i], rel)) return 1;
    return 0;
}

/* some kept path lies below rel */
static int is_ancestor_of_kept(const char *rel)
{
    size_t n = strlen(rel);
    for (int i = 0; i < g_keepCount; i++)
        if (istartsn(g_keep[i], rel, n) && g_keep[i][n] == '/') return 1;
    return 0;
}

/* rel lies below some kept path */
static int is_under_kept(const char *rel)
{
    for (int i = 0; i < g_keepCount; i++) {
        size_t n = strlen(g_keep[i]);
        if (istartsn(rel, g_keep[i], n) && rel[n] == '/') return 1;
    }
    return 0;
}

/* Folders where files of the pack replace same-named files, but nothing else
 * is ever deleted (user payloads stay even if the pack has no newer ones). */
static int is_merge(const char *rel)
{
    static const char *const m[] = {"bootloader/payloads"};
    for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++)
        if (ieq(rel, m[i]) || (istartsn(rel, m[i], strlen(m[i])) && rel[strlen(m[i])] == '/')) return 1;
    return 0;
}

/* ---------- delete ---------- */

static int rm_rf(const char *abs)
{
    SwapList l;
    int res = list_read(abs, &l);
    if (res != FR_OK) return res;
    int fail = 0;
    for (int i = 0; i < l.n; i++) {
        char *c = join(abs, l.v[i].name);
        if (!c) {
            fail = 1;
            continue;
        }
        int r = l.v[i].isDir ? rm_rf(c) : (int)f_unlink(c);
        if (r != FR_OK) fail = 1;
        free(c);
    }
    list_free(&l);
    if (fail) return FR_DENIED;
    return (int)f_unlink(abs);
}

static void wipe_dir(const char *abs, const char *rel, int depth)
{
    SwapList l;
    if (list_read(abs, &l) != FR_OK) {
        g_failed++;
        return;
    }
    for (int i = 0; i < l.n; i++) {
        SwapEntry *e = &l.v[i];
        if (e->sys) continue; /* e.g. "System Volume Information" */
        char *cabs = join(abs, e->name);
        char *crel = rel[0] ? join(rel, e->name) : join("", e->name);
        if (!cabs || !crel) {
            g_failed++;
            free(cabs);
            free(crel);
            continue;
        }
        if (!is_kept(crel) && !is_merge(crel)) {
            if (e->isDir && is_ancestor_of_kept(crel)) {
                wipe_dir(cabs, crel, depth + 1);
            }
            else {
                int r = e->isDir ? rm_rf(cabs) : (int)f_unlink(cabs);
                if (r == FR_OK) {
                    g_removed++;
                    if (depth == 0) gfx_printf("  - %s\n", e->name);
                }
                else {
                    g_failed++;
                    gfx_printf("%k  ! %s (%d)%k\n", 0xFFFF0000, e->name, r, 0xFFCCCCCC);
                }
            }
        }
        free(cabs);
        free(crel);
    }
    list_free(&l);
}

/* Only top-level folders of the pack that are really "firmware/CFW" get their
 * stale files removed. Everything else on the SD (Android, Linux, roms, the
 * user's own folders ...) is never even opened, so it costs no time or RAM. */
static const char *const MANAGED_DIRS[] = {"atmosphere", "bootloader", "sept", "SaltySD"};
/* Removed even if the new pack does not contain them: old themes may not
 * work on the new firmware. */
static const char *const ALWAYS_REMOVE[] = {"themes"};

static void scoped_wipe(void)
{
    SwapList top;
    if (list_read("sd:/" SWAP_STAGING, &top) == FR_OK) {
        for (int i = 0; i < top.n; i++) {
            if (!top.v[i].isDir) continue;
            for (unsigned m = 0; m < sizeof(MANAGED_DIRS) / sizeof(MANAGED_DIRS[0]); m++) {
                if (!ieq(top.v[i].name, MANAGED_DIRS[m])) continue;
                char *abs = join("sd:/", top.v[i].name);
                if (abs) {
                    FILINFO fi;
                    if (f_stat(abs, &fi) == FR_OK && (fi.fattrib & AM_DIR)) {
                        gfx_printf("  cleaning %s\n", top.v[i].name);
                        wipe_dir(abs, top.v[i].name, 1);
                    }
                    free(abs);
                }
            }
        }
        list_free(&top);
    }
    for (unsigned a = 0; a < sizeof(ALWAYS_REMOVE) / sizeof(ALWAYS_REMOVE[0]); a++) {
        char *abs = join("sd:/", ALWAYS_REMOVE[a]);
        FILINFO fi;
        if (abs && !is_kept(ALWAYS_REMOVE[a]) && f_stat(abs, &fi) == FR_OK) {
            int r = (fi.fattrib & AM_DIR) ? rm_rf(abs) : (int)f_unlink(abs);
            if (r == FR_OK) {
                g_removed++;
                gfx_printf("  - %s\n", ALWAYS_REMOVE[a]);
            }
            else g_failed++;
        }
        free(abs);
    }
}

/* ---------- move staged files into place ---------- */

static void move_tree(const char *src, const char *dst, const char *rel)
{
    SwapList l;
    if (list_read(src, &l) != FR_OK) {
        g_failed++;
        return;
    }
    for (int i = 0; i < l.n; i++) {
        SwapEntry *e = &l.v[i];
        char *s = join(src, e->name);
        char *d = join(dst, e->name);
        char *crel = join(rel, e->name);
        if (!s || !d || !crel) {
            g_failed++;
            free(s);
            free(d);
            free(crel);
            continue;
        }
        FILINFO fi;
        int exists = (f_stat(d, &fi) == FR_OK);

        if (e->isDir) {
            if (exists && !(fi.fattrib & AM_DIR)) {
                f_unlink(d);
                exists = 0;
            }
            if (!exists && f_rename(s, d) == FR_OK) {
                g_moved++;
            }
            else {
                if (!exists) f_mkdir(d);
                move_tree(s, d, crel);
                f_unlink(s); /* empty now (or left over if something was skipped) */
            }
        }
        else {
            if (exists && (is_kept(crel) || is_under_kept(crel))) {
                g_skipped++; /* the user's own file wins */
            }
            else {
                if (exists) {
                    if (fi.fattrib & AM_DIR) rm_rf(d);
                    else f_unlink(d);
                }
                if (f_rename(s, d) == FR_OK) g_moved++;
                else {
                    g_failed++;
                    gfx_printf("%k  ! move %s%k\n", 0xFFFF0000, crel, 0xFFCCCCCC);
                }
            }
        }
        free(s);
        free(d);
        free(crel);
    }
    list_free(&l);
}

/* ---------- hekate ---------- */

static void copy_file(const char *from, const char *to)
{
    FIL in, out;
    if (f_open(&in, from, FA_READ) != FR_OK) return;
    f_unlink(to);
    if (f_open(&out, to, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        f_close(&in);
        return;
    }
    const UINT bufSize = 0x10000;
    u8 *buf = (u8 *)malloc(bufSize);
    if (buf) {
        UINT br, bw;
        while (f_read(&in, buf, bufSize, &br) == FR_OK && br) {
            if (f_write(&out, buf, br, &bw) != FR_OK || bw != br) break;
        }
        free(buf);
    }
    f_close(&in);
    f_close(&out);
}

/* The pack ships hekate_ctcaer_X.bin in its root: make it the hekate that
 * boots next (Erista: bootloader/update.bin, Mariko: payload.bin.aio which
 * the existing code below renames to payload.bin). */
static void refresh_hekate(void)
{
    SwapList l;
    if (list_read("sd:/", &l) != FR_OK) return;
    for (int i = 0; i < l.n; i++) {
        const char *n = l.v[i].name;
        size_t len = strlen(n);
        if (!l.v[i].isDir && istartsn(n, "hekate_ctcaer", 13) && len > 4 && ieq(n + len - 4, ".bin")) {
            char *src = join("sd:/", n);
            if (src) {
                FILINFO fi;
                if (f_stat("sd:/bootloader", &fi) == FR_OK) copy_file(src, "sd:/bootloader/update.bin");
                /* always (re)create it: without payload.bin.aio the Mariko path would
                 * keep booting this payload forever */
                copy_file(src, "sd:/payload.bin.aio");
                free(src);
            }
            break;
        }
    }
    list_free(&l);
}

/* ---------- entry point ---------- */

void aiosu_run_pending_swap(void)
{
    FILINFO fi;
    if (f_stat(SWAP_MARKER, NULL) != FR_OK) {
        if (f_stat("sd:/" SWAP_STAGING, NULL) == FR_OK) {
            gfx_printf("\n%kCustomUpdater: %s exists but swap_pending.txt is missing.\nNothing was changed.%k\n", 0xFFFF0000, SWAP_STAGING, 0xFFCCCCCC);
            usleep(6000000);
        }
        else {
            gfx_printf("\nCustomUpdater: no pending install.\n");
            usleep(1500000);
        }
        return;
    }

    gfx_printf("\n%kCustomUpdater: installing pack%k\n", 0xFF00FFCC, 0xFFCCCCCC);

    int ok = 1;
    if (f_stat("sd:/" SWAP_STAGING, &fi) != FR_OK || !(fi.fattrib & AM_DIR)) {
        gfx_printf("%kStaging folder missing.%k\n", 0xFFFF0000, 0xFFCCCCCC);
        ok = 0;
    }
    else if (f_stat("sd:/" SWAP_STAGING "/atmosphere", NULL) != FR_OK && f_stat("sd:/" SWAP_STAGING "/bootloader", NULL) != FR_OK) {
        gfx_printf("%kStaging folder has no atmosphere/bootloader.%k\n", 0xFFFF0000, 0xFFCCCCCC);
        ok = 0;
    }
    else if (!load_keep()) {
        gfx_printf("%kKeep list missing.%k\n", 0xFFFF0000, 0xFFCCCCCC);
        ok = 0;
    }

    if (!ok) {
        gfx_printf("%kNothing was deleted. Aborting.%k\n", 0xFFFF0000, 0xFFCCCCCC);
        f_unlink(SWAP_MARKER);
        usleep(4000000);
        return;
    }

    gfx_printf("Replacing CFW files in 5 s. Hold POWER to abort.\n");
    for (int i = 5; i > 0; i--) {
        gfx_printf("%d ", i);
        usleep(1000000);
    }
    gfx_printf("\n");

    g_removed = g_failed = g_moved = g_skipped = 0;
    scoped_wipe();
    gfx_printf("Installing new files...\n");
    move_tree("sd:/" SWAP_STAGING, "sd:/", "");
    refresh_hekate();
    rm_rf("sd:/" SWAP_STAGING);
    f_unlink(SWAP_MARKER);
    f_unlink(SWAP_KEEP);

    gfx_printf("Removed %d, installed %d, kept %d, errors %d\n", g_removed, g_moved, g_skipped, g_failed);
    usleep(g_failed ? 6000000 : 2000000);
}
