/*
 * convert.c - the player's own copy of Apeiron, turned into the artwork folder the game runs on.
 *
 * This is tools/extract_assets.py (with rsrc.py and pict.py) written again in C, so that the game
 * can do the job itself: on a Google TV there is no computer to run Python on, and a player who
 * has only the downloaded zip should not need one. The Python tools stay as the reference, and the
 * test for this file is that the folder it writes is byte for byte the one they write.
 *
 * What it reads is the game's .zip: inside, each Mac file has its resource fork in a hidden
 * ".rsrc" folder beside it, and those forks hold the pictures, sounds, fonts and strings. What it
 * writes is 8-bit BMPs (the game's palette), WAVs, and a few small text files.
 *
 * Errors do not unwind through every function: fail() prints the reason and longjmps back to
 * convert_zip, which reports it. Memory is taken from a stack-like arena that is released a piece
 * at a time as each picture is finished, so a television's memory is not asked for everything at
 * once.
 */
#include "apeiron.h"

#include <setjmp.h>
#include <stdarg.h>

#ifdef _MSC_VER
#define NORET __declspec(noreturn)
#else
#define NORET __attribute__((noreturn))
#endif

/* ----------------------------------------------------------------------------- plumbing */
static jmp_buf g_jb;
static char g_err[400];
static ConvSay g_say;
static void *g_ud;
static char g_out[1200];                 /* the folder being written */
static int g_files;

static NORET void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    longjmp(g_jb, 1);
}

static void say(const char *fmt, ...)
{
    char line[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    diag_note("convert: %s", line);
    if (g_say) g_say(g_ud, line);
}

#define ARENA_MAX 8192
static void *g_arena[ARENA_MAX];
static int g_na;

static void *zalloc(size_t n)                 /* zeroed, freed by arena_release */
{
    if (g_na >= ARENA_MAX) fail("out of bookkeeping space");
    void *p = calloc(n ? n : 1, 1);
    if (!p) fail("out of memory");
    g_arena[g_na++] = p;
    return p;
}
static int arena_mark(void) { return g_na; }
static void arena_release(int m)
{
    while (g_na > m) free(g_arena[--g_na]);
}
static void adopt(void *p)
{
    if (g_na >= ARENA_MAX) fail("out of bookkeeping space");
    g_arena[g_na++] = p;
}

typedef struct { const uint8_t *p; size_t n; } Buf;
static unsigned u8(Buf b, size_t i) { return i < b.n ? b.p[i] : 0; }
static unsigned u16(Buf b, size_t i) { return i + 2 <= b.n ? (unsigned)(b.p[i] << 8 | b.p[i + 1]) : 0; }
static int s16(Buf b, size_t i) { return (int16_t)u16(b, i); }
static uint32_t u32(Buf b, size_t i)
{
    return i + 4 <= b.n ? ((uint32_t)b.p[i] << 24 | (uint32_t)b.p[i + 1] << 16 |
                           (uint32_t)b.p[i + 2] << 8 | b.p[i + 3]) : 0;
}
static Buf sub(Buf b, size_t off, size_t len)
{
    Buf r = { b.p, 0 };
    if (off > b.n) return r;
    r.p = b.p + off;
    r.n = off + len > b.n ? b.n - off : len;
    return r;
}

typedef uint8_t Pal[256][3];

/* ----------------------------------------------------------------------------- writing files */
static void path_of(char *dst, size_t n, const char *rel)
{
    snprintf(dst, n, "%s/%s", g_out, rel);
}

static void make_dir(const char *rel)
{
    char p[1400];
    path_of(p, sizeof p, rel);
    SDL_CreateDirectory(p);
}

static void put_file(const char *rel, const void *data, size_t n)
{
    char p[1400];
    path_of(p, sizeof p, rel);
    FILE *f = fopen(p, "wb");
    if (!f) fail("cannot write %s", p);
    if (n && fwrite(data, 1, n, f) != n) { fclose(f); fail("disk full writing %s", p); }
    if (fclose(f) != 0) fail("disk full writing %s", p);
    g_files++;
    if (g_say && !(g_files & 3)) g_say(g_ud, NULL);          /* a tick, so the screen keeps moving */
}

/* A growable byte string, for the small text files. */
typedef struct { char *p; size_t n, cap; } Str;
static void str_add(Str *s, const void *d, size_t n)
{
    if (s->n + n + 1 > s->cap) {
        size_t c = s->cap ? s->cap : 256;
        while (c < s->n + n + 1) c *= 2;
        char *q = realloc(s->p, c);
        if (!q) fail("out of memory");
        if (!s->p) { s->p = q; adopt(q); } else if (q != s->p) {
            for (int i = 0; i < g_na; i++) if (g_arena[i] == s->p) g_arena[i] = q;
            s->p = q;
        }
        s->cap = c;
    }
    memcpy(s->p + s->n, d, n);
    s->n += n;
}
static void str_f(Str *s, const char *fmt, ...)
{
    char t[600];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n > (int)sizeof t - 1) n = (int)sizeof t - 1;
    str_add(s, t, (size_t)n);
}

/* An 8-bit BMP written the way Pillow writes one, so the files are identical: bottom-up rows
 * padded to four bytes, a 256-entry BGR0 palette, and 3780 pixels per metre. */
static void put_bmp(const char *rel, int w, int h, const uint8_t *idx, Pal pal)
{
    size_t rb = ((size_t)w + 3) & ~(size_t)3;
    size_t isz = rb * (size_t)h;
    size_t total = 14 + 40 + 1024 + isz;
    uint8_t *b = zalloc(total);
    #define P16(o, v) do { b[o] = (uint8_t)(v); b[(o) + 1] = (uint8_t)((v) >> 8); } while (0)
    #define P32(o, v) do { P16(o, (v) & 0xFFFF); P16((o) + 2, ((v) >> 16) & 0xFFFF); } while (0)
    b[0] = 'B'; b[1] = 'M';
    P32(2, (uint32_t)total);
    P32(10, 14 + 40 + 1024);
    P32(14, 40);
    P32(18, (uint32_t)w);
    P32(22, (uint32_t)h);
    P16(26, 1);
    P16(28, 8);
    P32(34, (uint32_t)isz);
    P32(38, 3780);
    P32(42, 3780);
    P32(46, 256);
    P32(50, 256);
    for (int i = 0; i < 256; i++) {
        b[54 + i * 4 + 0] = pal[i][2];
        b[54 + i * 4 + 1] = pal[i][1];
        b[54 + i * 4 + 2] = pal[i][0];
    }
    for (int y = 0; y < h; y++)
        memcpy(b + 14 + 40 + 1024 + (size_t)(h - 1 - y) * rb, idx + (size_t)y * (size_t)w, (size_t)w);
    #undef P16
    #undef P32
    put_file(rel, b, total);
}

/* ----------------------------------------------------------------------------- inflate */
/* Deflate, after Mark Adler's puff.c: small, and slow in a way that does not matter for 5 MB. */
#define MAXBITS 15
typedef struct { const uint8_t *in; size_t inlen, incnt; long bitbuf; int bitcnt;
                 uint8_t *out; size_t outlen, outcnt; } IState;
typedef struct { short count[MAXBITS + 1]; short symbol[288]; } Huff;

static int ibits(IState *s, int need)
{
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) fail("the zip is damaged (data ends early)");
        val |= (long)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int idecode(IState *s, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= ibits(s, 1);
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    fail("the zip is damaged (bad code)");
}

static int iconstruct(Huff *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static void icodes(IState *s, const Huff *lencode, const Huff *distcode)
{
    static const short lens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43,
                                    51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4,
                                    4, 4, 5, 5, 5, 5, 0 };
    static const short dists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257,
                                     385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
                                     16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
                                    10, 10, 11, 11, 12, 12, 13, 13 };
    for (;;) {
        int sym = idecode(s, lencode);
        if (sym < 256) {
            if (s->outcnt >= s->outlen) fail("the zip is damaged (too much data)");
            s->out[s->outcnt++] = (uint8_t)sym;
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) fail("the zip is damaged (bad length)");
            int len = lens[sym] + ibits(s, lext[sym]);
            sym = idecode(s, distcode);
            if (sym >= 30) fail("the zip is damaged (bad distance)");
            size_t dist = (size_t)(dists[sym] + ibits(s, dext[sym]));
            if (dist > s->outcnt) fail("the zip is damaged (distance too far)");
            if (s->outcnt + (size_t)len > s->outlen) fail("the zip is damaged (too much data)");
            while (len--) { s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
        }
    }
}

static void idynamic(IState *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[320];
    Huff lencode, distcode;
    int nlen = ibits(s, 5) + 257, ndist = ibits(s, 5) + 1, ncode = ibits(s, 4) + 4;
    if (nlen > 286 || ndist > 30) fail("the zip is damaged (bad table)");
    int index;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)ibits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (iconstruct(&lencode, lengths, 19) != 0) fail("the zip is damaged (bad table)");
    index = 0;
    while (index < nlen + ndist) {
        int sym = idecode(s, &lencode);
        if (sym < 16) {
            lengths[index++] = (short)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (index == 0) fail("the zip is damaged (bad repeat)");
                len = lengths[index - 1];
                rep = 3 + ibits(s, 2);
            } else if (sym == 17) rep = 3 + ibits(s, 3);
            else rep = 11 + ibits(s, 7);
            if (index + rep > nlen + ndist) fail("the zip is damaged (bad repeat)");
            while (rep--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) fail("the zip is damaged (no end code)");
    iconstruct(&lencode, lengths, nlen);
    iconstruct(&distcode, lengths + nlen, ndist);
    icodes(s, &lencode, &distcode);
}

static void inflate_raw(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen)
{
    IState s = { in, inlen, 0, 0, 0, out, outlen, 0 };
    int last;
    do {
        last = ibits(&s, 1);
        int type = ibits(&s, 2);
        if (type == 0) {
            s.bitbuf = 0;
            s.bitcnt = 0;
            if (s.incnt + 4 > s.inlen) fail("the zip is damaged (data ends early)");
            unsigned len = s.in[s.incnt] | (unsigned)s.in[s.incnt + 1] << 8;
            s.incnt += 4;
            if (s.incnt + len > s.inlen || s.outcnt + len > s.outlen) fail("the zip is damaged (stored block)");
            memcpy(s.out + s.outcnt, s.in + s.incnt, len);
            s.incnt += len;
            s.outcnt += len;
        } else if (type == 1) {
            static Huff lc, dc;
            static bool made;
            if (!made) {
                short l[288];
                int sym;
                for (sym = 0; sym < 144; sym++) l[sym] = 8;
                for (; sym < 256; sym++) l[sym] = 9;
                for (; sym < 280; sym++) l[sym] = 7;
                for (; sym < 288; sym++) l[sym] = 8;
                iconstruct(&lc, l, 288);
                for (sym = 0; sym < 30; sym++) l[sym] = 5;
                iconstruct(&dc, l, 30);
                made = true;
            }
            icodes(&s, &lc, &dc);
        } else if (type == 2) {
            idynamic(&s);
        } else {
            fail("the zip is damaged (bad block)");
        }
    } while (!last);
    if (s.outcnt != outlen) fail("the zip is damaged (wrong size)");
}

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    static uint32_t t[256];
    if (!t[1]) for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[i] = c;
    }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = t[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

/* ----------------------------------------------------------------------------- the zip */
typedef struct { char *name; uint32_t csize, usize, crc, lho; int method; } ZEnt;
typedef struct { uint8_t *p; size_t n; ZEnt *e; int ne; } Zip;

static unsigned le16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint8_t *read_whole(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > 512L * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)len + 1);
    if (!p) { fclose(f); return NULL; }
    size_t got = fread(p, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(p); return NULL; }
    *n = (size_t)len;
    return p;
}

/* Parse the central directory. Returns NULL if this is not a zip. */
static bool zip_parse(Zip *z)
{
    if (z->n < 22) return false;
    size_t i = z->n - 22;
    for (;; i--) {
        if (le32(z->p + i) == 0x06054b50u) break;
        if (i == 0 || z->n - i > 65557) return false;
    }
    int ne = (int)le16(z->p + i + 10);
    size_t cdsz = le32(z->p + i + 12), cdoff = le32(z->p + i + 16);
    if (cdoff + cdsz > z->n) return false;
    z->e = zalloc(sizeof(ZEnt) * (size_t)(ne ? ne : 1));
    size_t q = cdoff;
    int got = 0;
    for (int k = 0; k < ne; k++) {
        if (q + 46 > z->n || le32(z->p + q) != 0x02014b50u) break;
        ZEnt *e = &z->e[got];
        e->method = (int)le16(z->p + q + 10);
        e->crc = le32(z->p + q + 16);
        e->csize = le32(z->p + q + 20);
        e->usize = le32(z->p + q + 24);
        unsigned nl = le16(z->p + q + 28), xl = le16(z->p + q + 30), cl = le16(z->p + q + 32);
        e->lho = le32(z->p + q + 42);
        if (q + 46 + nl > z->n) break;
        e->name = zalloc(nl + 1);
        for (unsigned c = 0; c < nl; c++) {
            char ch = (char)z->p[q + 46 + c];
            e->name[c] = ch == '\\' ? '/' : ch;
        }
        q += 46 + nl + xl + cl;
        got++;
    }
    z->ne = got;
    return got > 0;
}

static uint8_t *zip_extract(const Zip *z, int i, size_t *outn)
{
    const ZEnt *e = &z->e[i];
    size_t lh = e->lho;
    if (lh + 30 > z->n || le32(z->p + lh) != 0x04034b50u) fail("the zip is damaged (bad entry)");
    size_t data = lh + 30 + le16(z->p + lh + 26) + le16(z->p + lh + 28);
    if (data + e->csize > z->n) fail("the zip is damaged (entry runs past the end)");
    uint8_t *out = zalloc((size_t)e->usize + 1);
    if (e->method == 0) {
        if (e->csize != e->usize) fail("the zip is damaged (stored size)");
        memcpy(out, z->p + data, e->usize);
    } else if (e->method == 8) {
        inflate_raw(z->p + data, e->csize, out, e->usize);
    } else {
        fail("the zip uses a compression method (%d) this game cannot read", e->method);
    }
    if (crc32_of(out, e->usize) != e->crc) fail("the zip is damaged (checksum of '%s')", e->name);
    *outn = e->usize;
    return out;
}

/* Where the game's own folder is inside the zip: the shallowest one holding ".rsrc/Apeiron Titles".
 * Fills `prefix` with the path up to and including the slash before ".rsrc"; false if there is none. */
static bool zip_game_prefix(const Zip *z, char *prefix, size_t n)
{
    int best = -1, bi = -1;
    for (int i = 0; i < z->ne; i++) {
        const char *nm = z->e[i].name;
        const char *k = strstr(nm, ".rsrc/Apeiron Titles");
        if (!k || strcmp(k, ".rsrc/Apeiron Titles") != 0) continue;
        if (k != nm && k[-1] != '/') continue;
        int plen = (int)(k - nm);
        if (best < 0 || plen < best) { best = plen; bi = i; }
    }
    if (bi < 0 || (size_t)best + 1 > n) return false;
    memcpy(prefix, z->e[bi].name, (size_t)best);
    prefix[best] = 0;
    return true;
}

/* ----------------------------------------------------------------------------- resource forks */
typedef struct { char type[5]; int id; Buf data; } Res;
typedef struct { Res *r; int n; } Fork;

static void fork_parse(Fork *f, Buf r)
{
    memset(f, 0, sizeof *f);
    uint32_t dof = u32(r, 0), mo = u32(r, 4);
    if (r.n < 16 || mo + 28 > r.n) fail("a resource fork is not in the format I know");
    size_t tlist = mo + u16(r, mo + 24);
    int nt = (int)u16(r, tlist) + 1;
    int total = 0;
    for (int i = 0; i < nt; i++) total += (int)u16(r, tlist + 2 + (size_t)i * 8 + 4) + 1;
    f->r = zalloc(sizeof(Res) * (size_t)(total + 1));
    for (int i = 0; i < nt; i++) {
        size_t p = tlist + 2 + (size_t)i * 8;
        char t[5] = { (char)u8(r, p), (char)u8(r, p + 1), (char)u8(r, p + 2), (char)u8(r, p + 3), 0 };
        int c = (int)u16(r, p + 4) + 1;
        unsigned ro = u16(r, p + 6);
        for (int j = 0; j < c; j++) {
            size_t q = tlist + ro + (size_t)j * 12;
            Res *x = &f->r[f->n++];
            memcpy(x->type, t, 5);
            x->id = s16(r, q);
            uint32_t off = (u8(r, q + 5) << 16) | (u8(r, q + 6) << 8) | u8(r, q + 7);
            uint32_t ln = u32(r, dof + off);
            x->data = sub(r, dof + off + 4, ln);
        }
    }
}

static bool fork_get(const Fork *f, const char *type, int id, Buf *out)
{
    for (int i = 0; i < f->n; i++)
        if (!memcmp(f->r[i].type, type, 4) && f->r[i].id == id) { *out = f->r[i].data; return true; }
    return false;
}

static Buf need(const Fork *f, const char *fname, const char *type, int id)
{
    Buf b;
    if (!fork_get(f, type, id, &b)) fail("%s has no %s %d - is this the right version (1.0.3)?", fname, type, id);
    return b;
}

/* ----------------------------------------------------------------------------- palettes */
static void clut_to_pal(Buf d, Pal pal)
{
    memset(pal, 0, sizeof(Pal));
    int n = (int)u16(d, 6);
    for (int i = 0; i <= n; i++) {
        unsigned v = u16(d, 8 + (size_t)i * 8);
        pal[v & 255][0] = (uint8_t)(u16(d, 10 + (size_t)i * 8) >> 8);
        pal[v & 255][1] = (uint8_t)(u16(d, 12 + (size_t)i * 8) >> 8);
        pal[v & 255][2] = (uint8_t)(u16(d, 14 + (size_t)i * 8) >> 8);
    }
}

static int nearest(Pal pal, const uint8_t *c)
{
    int best = 0;
    long bd = -1;
    for (int j = 0; j < 256; j++) {
        long dr = pal[j][0] - c[0], dg = pal[j][1] - c[1], db = pal[j][2] - c[2];
        long d = dr * dr + dg * dg + db * db;
        if (bd < 0 || d < bd) { bd = d; best = j; }
    }
    return best;
}

/* src index -> palette index: an exact colour match if there is one (the same index if that matches
 * too), else the nearest entry, which is what QuickDraw does drawing into a window with this table.
 * `present` says which of the n source indices exist. */
static void remap_palette(int n, uint8_t (*rgb)[3], const uint8_t *present, Pal pal, int *remap)
{
    for (int k = 0; k < n; k++) {
        remap[k] = 0;
        if (present && !present[k]) continue;
        int first = -1;
        bool same = false;
        for (int j = 0; j < 256; j++) {
            if (pal[j][0] == rgb[k][0] && pal[j][1] == rgb[k][1] && pal[j][2] == rgb[k][2]) {
                if (first < 0) first = j;
                if (j == k) same = true;
            }
        }
        if (first >= 0) remap[k] = same ? k : first;
        else remap[k] = nearest(pal, rgb[k]);
    }
}

/* ----------------------------------------------------------------------------- PICT */
typedef struct {
    int w, h, pixsize, cmp, packtype;
    int dst[4];
    int nrows;
    size_t *roff, *rlen;
    uint8_t *pool;
    bool has_ctab;
    uint8_t has[256];
    uint8_t ct[256][3];
} Cap;

typedef struct { int w, h; uint8_t *rgb; } Canvas;

static int rect_at(Buf d, size_t o, int *r)
{
    for (int i = 0; i < 4; i++) r[i] = s16(d, o + (size_t)i * 2);
    return 0;
}

/* One run-length-packed row. `unit` is 2 for 16-bit pixels. Appends to the pool; returns new pos. */
static size_t unpack_row(Buf d, size_t pos, bool big, int unit, uint8_t **pool, size_t *plen, size_t *pcap)
{
    size_t n;
    if (big) { n = u16(d, pos); pos += 2; } else { n = u8(d, pos); pos += 1; }
    size_t end = pos + n;
    while (pos < end && pos < d.n) {
        unsigned f = u8(d, pos++);
        size_t k = f < 128 ? (size_t)(f + 1) * (size_t)unit : (size_t)(257 - f);
        size_t add = f < 128 ? k : k * (size_t)unit;
        if (*plen + add > *pcap) {
            size_t c = *pcap ? *pcap : 4096;
            while (c < *plen + add) c *= 2;
            uint8_t *q = realloc(*pool, c);
            if (!q) fail("out of memory");
            *pool = q;
            *pcap = c;
        }
        if (f < 128) {
            for (size_t i = 0; i < k; i++) (*pool)[(*plen)++] = (uint8_t)u8(d, pos + i);
            pos += k;
        } else {
            for (size_t r = 0; r < k; r++)
                for (int i = 0; i < unit; i++) (*pool)[(*plen)++] = (uint8_t)u8(d, pos + (size_t)i);
            pos += (size_t)unit;
        }
    }
    return end;
}

/* QuickDraw's PackBitsRect/DirectBitsRect and friends: leaves the rows and colour table in `c`. */
static size_t pict_draw(Buf d, size_t pos, int op, Cap *c)
{
    bool direct = op == 0x9A || op == 0x9B;
    bool packed = op == 0x98 || op == 0x99 || op == 0x9A || op == 0x9B;
    bool region = op == 0x91 || op == 0x99 || op == 0x9B;
    memset(c, 0, sizeof *c);
    if (direct) pos += 4;
    unsigned rb = u16(d, pos);
    pos += 2;
    bool ispix = (rb & 0x8000) != 0;
    size_t rowbytes = rb & 0x3FFF;
    int bounds[4];
    rect_at(d, pos, bounds);
    pos += 8;
    c->pixsize = 1;
    c->cmp = 1;
    if (ispix) {
        c->packtype = (int)u16(d, pos + 2);
        c->pixsize = (int)u16(d, pos + 18);
        c->cmp = (int)u16(d, pos + 20);
        pos += 36;
        if (!direct) {
            unsigned flags = u16(d, pos + 4), n = u16(d, pos + 6);
            pos += 8;
            c->has_ctab = true;
            for (unsigned i = 0; i <= n; i++) {
                unsigned v = u16(d, pos);
                unsigned idx = (flags & 0x8000) ? i : v;
                if (idx < 256) {
                    c->has[idx] = 1;
                    c->ct[idx][0] = (uint8_t)(u16(d, pos + 2) >> 8);
                    c->ct[idx][1] = (uint8_t)(u16(d, pos + 4) >> 8);
                    c->ct[idx][2] = (uint8_t)(u16(d, pos + 6) >> 8);
                }
                pos += 8;
            }
        }
    }
    int src[4];
    rect_at(d, pos, src);
    pos += 8;
    rect_at(d, pos, c->dst);
    pos += 8;
    pos += 2;                                   /* transfer mode */
    if (region) pos += u16(d, pos);
    int h = bounds[2] - bounds[0], w = bounds[3] - bounds[1];
    if (w < 0 || h < 0 || w > 4096 || h > 4096) fail("a picture has a strange size (%d x %d)", w, h);
    c->w = w;
    c->h = h;
    c->nrows = h;
    c->roff = zalloc(sizeof(size_t) * (size_t)(h ? h : 1));
    c->rlen = zalloc(sizeof(size_t) * (size_t)(h ? h : 1));
    uint8_t *pool = NULL;
    size_t plen = 0, pcap = 0;
    for (int y = 0; y < h; y++) {
        c->roff[y] = plen;
        if (!packed || rowbytes < 8 || c->packtype == 1) {
            size_t take = pos + rowbytes > d.n ? (pos < d.n ? d.n - pos : 0) : rowbytes;
            if (plen + take > pcap) {
                size_t cc = pcap ? pcap : 4096;
                while (cc < plen + take) cc *= 2;
                uint8_t *q = realloc(pool, cc);
                if (!q) fail("out of memory");
                pool = q;
                pcap = cc;
            }
            if (take) memcpy(pool + plen, d.p + pos, take);
            plen += take;
            pos += rowbytes;
        } else {
            int unit = c->pixsize == 16 ? 2 : 1;
            pos = unpack_row(d, pos, rowbytes > 250, unit, &pool, &plen, &pcap);
        }
        c->rlen[y] = plen - c->roff[y];
    }
    if (pool) adopt(pool);
    c->pool = pool;
    return pos;
}

static unsigned cap_byte(const Cap *c, int y, size_t i)
{
    if (y < 0 || y >= c->nrows || i >= c->rlen[y]) return 0;
    return c->pool[c->roff[y] + i];
}

static unsigned cap_pix(const Cap *c, int x, int y)       /* an index, for the 1/2/4/8-bit kinds */
{
    int ps = c->pixsize;
    if (ps == 8) return cap_byte(c, y, (size_t)x);
    int per = 8 / ps, mask = (1 << ps) - 1;
    unsigned byte = cap_byte(c, y, (size_t)(x / per));
    return (byte >> (8 - ps * (x % per + 1))) & (unsigned)mask;
}

static void cap_rgb(const Cap *c, int x, int y, uint8_t *o)
{
    int ps = c->pixsize;
    if (ps == 1 || ps == 2 || ps == 4 || ps == 8) {
        unsigned v = cap_pix(c, x, y);
        if (c->has_ctab) {
            if (v < 256 && c->has[v]) { o[0] = c->ct[v][0]; o[1] = c->ct[v][1]; o[2] = c->ct[v][2]; }
            else o[0] = o[1] = o[2] = 0;
        } else if (ps == 1) {
            o[0] = o[1] = o[2] = v ? 0 : 255;
        } else {
            o[0] = o[1] = o[2] = (uint8_t)(v * 17);
        }
    } else if (ps == 16) {
        unsigned v = cap_byte(c, y, (size_t)(2 * x)) << 8 | cap_byte(c, y, (size_t)(2 * x + 1));
        o[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
        o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
        o[2] = (uint8_t)((v & 31) * 255 / 31);
    } else if (ps == 32) {
        if (c->cmp == 3 || c->cmp == 4) {
            size_t off = c->cmp == 3 ? 0 : (size_t)c->w;
            o[0] = (uint8_t)cap_byte(c, y, off + (size_t)x);
            o[1] = (uint8_t)cap_byte(c, y, off + (size_t)c->w + (size_t)x);
            o[2] = (uint8_t)cap_byte(c, y, off + 2 * (size_t)c->w + (size_t)x);
        } else {
            o[0] = (uint8_t)cap_byte(c, y, (size_t)(4 * x + 1));
            o[1] = (uint8_t)cap_byte(c, y, (size_t)(4 * x + 2));
            o[2] = (uint8_t)cap_byte(c, y, (size_t)(4 * x + 3));
        }
    } else {
        o[0] = o[1] = o[2] = 0;
    }
}

static int fixed_len(int op)
{
    switch (op) {
    case 0x00: return 0;
    case 0x02: return 8;   case 0x03: return 2;  case 0x04: return 2;  case 0x05: return 2;
    case 0x06: return 4;   case 0x07: return 4;  case 0x08: return 2;  case 0x09: return 8;
    case 0x0A: return 8;   case 0x0B: return 4;  case 0x0C: return 4;  case 0x0D: return 2;
    case 0x0E: return 4;   case 0x0F: return 4;  case 0x10: return 8;
    case 0x1A: return 6;   case 0x1B: return 6;  case 0x1C: return 0;  case 0x1D: return 6;
    case 0x1E: return 0;   case 0x1F: return 6;  case 0x20: return 8;  case 0x21: return 4;
    case 0x22: return 6;   case 0x23: return 2;
    case 0xA0: return 2;   case 0x0C00: return 24;
    }
    if (op >= 0x30 && op <= 0x34) return 8;
    if (op >= 0x38 && op <= 0x3C) return 0;
    if (op >= 0x40 && op <= 0x44) return 8;
    if (op >= 0x48 && op <= 0x4C) return 0;
    if (op >= 0x50 && op <= 0x54) return 8;
    if (op >= 0x58 && op <= 0x5C) return 0;
    if (op >= 0x60 && op <= 0x64) return 8;
    if (op >= 0x68 && op <= 0x6C) return 0;
    return -1;
}

/* Walk a picture. The first bitmap it draws is left in *first (when given); every one is pasted on
 * the canvas (when given). Returns NULL, or why it could not be read. */
static const char *pict_walk(Buf d, Cap *first, int *nfirst, Canvas *cv)
{
    int frame[4];
    rect_at(d, 2, frame);
    if (cv) {
        cv->w = frame[3] - frame[1] > 1 ? frame[3] - frame[1] : 1;
        cv->h = frame[2] - frame[0] > 1 ? frame[2] - frame[0] : 1;
        if (cv->w > 4096 || cv->h > 4096) return "picture too large";
        cv->rgb = zalloc((size_t)cv->w * (size_t)cv->h * 3);
        memset(cv->rgb, 255, (size_t)cv->w * (size_t)cv->h * 3);
    }
    size_t pos = 10;
    int ver = 1;
    if (u16(d, 10) == 0x0011 && u16(d, 12) == 0x02FF) { ver = 2; pos = 14; }
    else if (u16(d, 10) == 0x1101) pos += 2;
    int drew = 0;
    while (pos + (ver == 2 ? 2 : 1) <= d.n) {
        int op;
        if (ver == 2) { op = (int)u16(d, pos); pos += 2; } else { op = (int)u8(d, pos); pos += 1; }
        if (op == 0xFF) break;
        if (op >= 0x80 && op <= 0x84) { pos += u16(d, pos); continue; }
        if (op == 0x01) { pos += u16(d, pos); continue; }
        if ((op >= 0x70 && op <= 0x74) || (op >= 0x78 && op <= 0x7C)) { pos += u16(d, pos); continue; }
        if (op == 0x28) { pos += 4; pos += 1 + u8(d, pos); continue; }
        if (op == 0x29 || op == 0x2A || op == 0x2B) { pos += 1; pos += 1 + u8(d, pos); continue; }
        if (op == 0x2C) { pos += 2; pos += 1 + u8(d, pos); continue; }
        if (op == 0xA1) { pos += 4 + u16(d, pos + 2); continue; }
        if (op == 0x90 || op == 0x91 || op == 0x98 || op == 0x99 || op == 0x9A || op == 0x9B) {
            Cap c;
            pos = pict_draw(d, pos, op, &c);
            if (first && *nfirst == 0) {
                *first = c;
                *nfirst = 1;
            }
            if (cv) {
                int ox = c.dst[1] - frame[1], oy = c.dst[0] - frame[0];
                for (int y = 0; y < c.h; y++) {
                    int cy = oy + y;
                    if (cy < 0 || cy >= cv->h) continue;
                    for (int x = 0; x < c.w; x++) {
                        int cx = ox + x;
                        if (cx < 0 || cx >= cv->w) continue;
                        cap_rgb(&c, x, y, cv->rgb + ((size_t)cy * (size_t)cv->w + (size_t)cx) * 3);
                    }
                }
            }
            drew++;
            continue;
        }
        if (op == 0x8200 || op == 0x8201) return "QuickTime PICT";
        int fl = fixed_len(op);
        if (fl >= 0) { pos += (size_t)fl; continue; }
        if (op >= 0x0100 && op <= 0x7FFF) { pos += (size_t)(op >> 8) * 2; continue; }
        if (op >= 0x8000 && op <= 0x80FF) continue;
        if (op >= 0x8100) { pos += 4 + u32(d, pos); continue; }
        return "unknown picture opcode";
    }
    if (!drew) return "no bitmap ops (vector-only PICT)";
    return NULL;
}

/* An indexed picture as 8-bit indices plus its colour table. Returns NULL or why not. */
typedef struct { int w, h; int *idx; bool has_ctab; uint8_t has[256]; uint8_t ct[256][3]; } Indexed;

static const char *pict_indexed(Buf d, Indexed *im)
{
    Cap c;
    int got = 0;
    memset(&c, 0, sizeof c);
    const char *why = pict_walk(d, &c, &got, NULL);
    if (why) return why;
    if (!got) return "no bitmap";
    if (c.pixsize != 1 && c.pixsize != 2 && c.pixsize != 4 && c.pixsize != 8) return "not indexed";
    im->w = c.w;
    im->h = c.h;
    im->has_ctab = c.has_ctab;
    memcpy(im->has, c.has, sizeof c.has);
    memcpy(im->ct, c.ct, sizeof c.ct);
    im->idx = zalloc(sizeof(int) * (size_t)(c.w ? c.w : 1) * (size_t)(c.h ? c.h : 1));
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++) im->idx[(size_t)y * (size_t)c.w + (size_t)x] = (int)cap_pix(&c, x, y);
    return NULL;
}

/* The colour table to remap from: the picture's own, or black and white for a plain bitmap. */
static void indexed_table(const Indexed *im, uint8_t has[256], uint8_t ct[256][3])
{
    bool any = false;
    if (im->has_ctab) for (int i = 0; i < 256; i++) if (im->has[i]) { any = true; break; }
    memset(has, 0, 256);
    if (any) {
        memcpy(has, im->has, 256);
        memcpy(ct, im->ct, 256 * 3);
    } else {
        has[0] = has[1] = 1;
        ct[0][0] = ct[0][1] = ct[0][2] = 255;
        ct[1][0] = ct[1][1] = ct[1][2] = 0;
    }
}

static void indexed_bmp(Buf d, Pal pal, const char *rel)
{
    int m = arena_mark();
    Indexed im;
    const char *why = pict_indexed(d, &im);
    if (why) fail("%s: %s", rel, why);
    uint8_t has[256], ct[256][3];
    int remap[256];
    indexed_table(&im, has, ct);
    remap_palette(256, ct, has, pal, remap);
    size_t n = (size_t)im.w * (size_t)im.h;
    uint8_t *out = zalloc(n ? n : 1);
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)remap[im.idx[i] & 255];
    put_bmp(rel, im.w, im.h, out, pal);
    arena_release(m);
}

/* ----------------------------------------------------------------------------- patterns, icons */
static void pattern_bmp(Buf d, Pal pal, const char *rel)
{
    int m = arena_mark();
    size_t pmap = u32(d, 2), pdata = u32(d, 6);
    unsigned rb = u16(d, pmap + 4);
    int top = s16(d, pmap + 6), left = s16(d, pmap + 8), bot = s16(d, pmap + 10), right = s16(d, pmap + 12);
    size_t tab = u32(d, pmap + 42);
    int w = right - left, h = bot - top;
    size_t rowb = rb & 0x3FFF;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) fail("%s: odd pattern size", rel);
    uint8_t ct[256][3], has[256];
    memset(has, 0, sizeof has);
    memset(ct, 0, sizeof ct);
    unsigned fl = u16(d, tab + 4), n = u16(d, tab + 6);
    for (unsigned i = 0; i <= n; i++) {
        unsigned v = u16(d, tab + 8 + (size_t)i * 8);
        unsigned k = (fl & 0x8000) ? i : v;
        if (k < 256) {
            has[k] = 1;
            ct[k][0] = (uint8_t)(u16(d, tab + 10 + (size_t)i * 8) >> 8);
            ct[k][1] = (uint8_t)(u16(d, tab + 12 + (size_t)i * 8) >> 8);
            ct[k][2] = (uint8_t)(u16(d, tab + 14 + (size_t)i * 8) >> 8);
        }
    }
    uint8_t *px = zalloc((size_t)w * (size_t)h);
    uint8_t used[256];
    memset(used, 0, sizeof used);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned v = u8(d, pdata + (size_t)y * rowb + (size_t)x);
            px[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)v;
            used[v] = 1;
        }
    int remap[256];
    remap_palette(256, ct, used, pal, remap);
    for (size_t i = 0; i < (size_t)w * (size_t)h; i++) px[i] = (uint8_t)remap[px[i]];
    put_bmp(rel, w, h, px, pal);
    arena_release(m);
}

typedef struct { int w, h; uint8_t *px; uint8_t has[256]; uint8_t ct[256][3]; Buf mask; int mrb; } Cicn;

static void parse_cicn(Buf d, Cicn *c)
{
    memset(c, 0, sizeof *c);
    size_t pm_rb = u16(d, 4) & 0x3FFF;
    int top = s16(d, 6), left = s16(d, 8), bot = s16(d, 10), right = s16(d, 12);
    unsigned pixsize = u16(d, 4 + 28);
    unsigned mask_rb = u16(d, 54);
    int mtop = s16(d, 56), mbot = s16(d, 60);
    unsigned icon_rb = u16(d, 68);
    int itop = s16(d, 70), ibot = s16(d, 74);
    int w = right - left, h = bot - top;
    if (w <= 0 || h <= 0 || w > 256 || h > 256 || (pixsize != 1 && pixsize != 2 && pixsize != 4 && pixsize != 8))
        fail("a colour icon is not in the format I know");
    size_t p = 82;
    c->mask = sub(d, p, (size_t)mask_rb * (size_t)(mbot - mtop));
    c->mrb = (int)mask_rb;
    p += (size_t)mask_rb * (size_t)(mbot - mtop);
    p += (size_t)icon_rb * (size_t)(ibot - itop);
    unsigned flags = u16(d, p + 4), n = u16(d, p + 6);
    p += 8;
    for (unsigned i = 0; i <= n; i++) {
        unsigned v = u16(d, p);
        unsigned k = (flags & 0x8000) ? i : v;
        if (k < 256) {
            c->has[k] = 1;
            c->ct[k][0] = (uint8_t)(u16(d, p + 2) >> 8);
            c->ct[k][1] = (uint8_t)(u16(d, p + 4) >> 8);
            c->ct[k][2] = (uint8_t)(u16(d, p + 6) >> 8);
        }
        p += 8;
    }
    c->w = w;
    c->h = h;
    c->px = zalloc((size_t)w * (size_t)h);
    for (int y = 0; y < h; y++) {
        size_t row = p + (size_t)y * pm_rb;
        for (int x = 0; x < w; x++) {
            unsigned v;
            if (pixsize == 8) v = (size_t)x < pm_rb ? u8(d, row + (size_t)x) : 0;
            else {
                int per = 8 / (int)pixsize, mk = (1 << pixsize) - 1;
                unsigned byte = (size_t)(x / per) < pm_rb ? u8(d, row + (size_t)(x / per)) : 0;
                v = (byte >> (8 - (int)pixsize * (x % per + 1))) & (unsigned)mk;
            }
            c->px[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)v;
        }
    }
}

static int cicn_mask_bit(const Cicn *c, int x, int y)
{
    if (c->mask.n && (size_t)(y * c->mrb + x / 8) < c->mask.n)
        return (c->mask.p[y * c->mrb + x / 8] >> (7 - x % 8)) & 1;
    return 1;
}

/* The icon with its OWN colour table, so the port can map it to whatever table is on the monitor. */
static void cicn_native_bmp(Buf d, const char *rel)
{
    int m = arena_mark();
    Cicn c;
    parse_cicn(d, &c);
    Pal pal;
    for (int i = 0; i < 256; i++) pal[i][0] = pal[i][1] = pal[i][2] = 255;
    int remap[256];
    memset(remap, 0, sizeof remap);
    int nxt = 1;
    for (int k = 0; k < 256; k++) {
        if (!c.has[k]) continue;
        if (nxt > 255) break;
        memcpy(pal[nxt], c.ct[k], 3);
        remap[k] = nxt++;
    }
    uint8_t *out = zalloc((size_t)c.w * (size_t)c.h);
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++)
            out[(size_t)y * (size_t)c.w + (size_t)x] =
                cicn_mask_bit(&c, x, y) ? (uint8_t)remap[c.px[(size_t)y * (size_t)c.w + (size_t)x]] : 0;
    put_bmp(rel, c.w, c.h, out, pal);
    arena_release(m);
}

static int white_index(Pal pal)
{
    for (int i = 0; i < 256; i++)
        if (pal[i][0] == 255 && pal[i][1] == 255 && pal[i][2] == 255) return i;
    return 0;
}

static void cicn_bmp(Buf d, Pal pal, const char *rel)
{
    int m = arena_mark();
    Cicn c;
    parse_cicn(d, &c);
    int remap[256];
    remap_palette(256, c.ct, c.has, pal, remap);
    int trans = white_index(pal);
    uint8_t *out = zalloc((size_t)c.w * (size_t)c.h);
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++) {
            int v = c.px[(size_t)y * (size_t)c.w + (size_t)x];
            out[(size_t)y * (size_t)c.w + (size_t)x] =
                cicn_mask_bit(&c, x, y) ? (uint8_t)(c.has[v] ? remap[v] : trans) : (uint8_t)trans;
        }
    put_bmp(rel, c.w, c.h, out, pal);
    arena_release(m);
}

/* ----------------------------------------------------------------------------- bitmap fonts */
static Pal FONT_PAL_INIT = { { 0, 0, 0 }, { 255, 255, 255 } };

static void font_bmp(const char *stem, int W, int H, const uint8_t *idx)
{
    char rel[120];
    snprintf(rel, sizeof rel, "%s.bmp", stem);
    put_bmp(rel, W, H, idx, FONT_PAL_INIT);
}

static void nfnt_to_font(Buf n, const char *stem)
{
    int fc = s16(n, 2), lc = s16(n, 4), km = s16(n, 8), frh = s16(n, 14);
    unsigned ow = u16(n, 16);
    int asc = s16(n, 18), desc = s16(n, 20), rw = s16(n, 24);
    int cnt = lc - fc + 3;
    if (cnt < 4 || cnt > 300 || rw <= 0 || frh <= 0 || rw > 512 || frh > 128) fail("a font is not in the format I know");
    size_t loc_off = 26 + (size_t)rw * 2 * (size_t)frh;
    unsigned *loc = zalloc(sizeof(unsigned) * (size_t)cnt), *owt = zalloc(sizeof(unsigned) * (size_t)cnt);
    for (int i = 0; i < cnt; i++) loc[i] = u16(n, loc_off + (size_t)i * 2);
    for (int i = 0; i < cnt - 1; i++) owt[i] = u16(n, 16 + (size_t)ow * 2 + (size_t)i * 2);
    owt[cnt - 1] = 0xFFFF;
    int W = rw * 16;
    uint8_t *img = zalloc((size_t)W * (size_t)frh);
    for (int y = 0; y < frh; y++)
        for (int x = 0; x < W; x++)
            if ((u8(n, 26 + (size_t)y * (size_t)rw * 2 + (size_t)(x >> 3)) >> (7 - (x & 7))) & 1)
                img[(size_t)y * (size_t)W + (size_t)x] = 1;
    font_bmp(stem, W, frh, img);
    Str s = { 0 };
    str_f(&s, "FONT %d %d %d\n", asc, desc, frh);
    int missing = lc - fc + 1;
    for (int c = 0; c < 256; c++) {
        int idx = (c >= fc && c <= lc) ? c - fc : missing;
        if (owt[idx] == 0xFFFF) idx = missing;
        int x = (int)loc[idx], w = (int)loc[idx + 1] - (int)loc[idx];
        int offset = (int)(owt[idx] >> 8), adv = (int)(owt[idx] & 0xFF);
        str_f(&s, "%d %d %d %d\n", x, w, km + offset, adv);
    }
    char rel[120];
    snprintf(rel, sizeof rel, "%s.fnt", stem);
    put_file(rel, s.p, s.n);
}

static void baked_font(const BakedFont *f)
{
    int rb = (f->w + 7) / 8;
    uint8_t *img = zalloc((size_t)f->w * (size_t)f->h);
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++)
            if (f->bits[y * rb + (x >> 3)] & (0x80 >> (x & 7))) img[(size_t)y * (size_t)f->w + (size_t)x] = 1;
    font_bmp(f->stem, f->w, f->h, img);
    char rel[120];
    snprintf(rel, sizeof rel, "%s.fnt", f->stem);
    put_file(rel, f->fnt, (size_t)f->fnt_len);
}

/* ----------------------------------------------------------------------------- sprite sheets */
typedef struct { int pid, frames, w, h; const char *name; } SpriteDef;
static const SpriteDef SPRITES[] = {
    { 1000, 52, 16, 16, "shroom" },   { 1001, 62, 16, 16, "pede" },      { 1002, 33, 16, 16, "shooter" },
    { 1003, 1, 4, 12, "shot" },       { 1004, 8, 16, 16, "flea" },       { 1005, 18, 42, 20, "gecko" },
    { 1006, 20, 24, 24, "scorpion" }, { 1007, 12, 4, 4, "scum" },        { 1008, 10, 8, 8, "points" },
    { 1009, 19, 16, 16, "explode_shooter" }, { 1010, 102, 8, 8, "flak" }, { 1011, 16, 16, 16, "splat" },
    { 1012, 16, 16, 16, "splat_red" }, { 1013, 28, 16, 16, "coin" },     { 1014, 9, 32, 32, "scorpsplat" },
    { 1015, 4, 14, 14, "yummie" },    { 1016, 1, 4, 12, "ricochet" },    { 1017, 40, 20, 20, "fodder" },
    { 1018, 10, 8, 8, "bonuspoints" }, { 1019, 6, 16, 16, "shield" },    { 1020, 9, 32, 32, "geckosplat" },
    { 1100, 11, 24, 32, "numbers" },  { 1101, 1, 16, 16, "life" },
    { 1102, 1, 16, 16, "wpn_none" },  { 1103, 1, 14, 14, "wpn_curve" },
    { 1104, 1, 14, 14, "wpn_freemove" }, { 1105, 1, 14, 14, "wpn_machinegun" },
    { 1106, 1, 14, 14, "wpn_shield" }, { 1107, 1, 14, 14, "wpn_lock" },
};
#define N_SPRITES (int)(sizeof SPRITES / sizeof SPRITES[0])

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* <dir>/<name>.bmp: the frames re-indexed to `pal`, stacked one above the next; <name>.spr says how
 * many, how big, and which index is transparent. */
static void extract_sprite(Buf d, const SpriteDef *sd, Pal pal, const char *dir, int ox, int oy)
{
    int m = arena_mark();
    int fw = sd->w, fh = sd->h, frames = sd->frames;
    Indexed im;
    int W, H;
    int remap[256];
    const char *why = pict_indexed(d, &im);
    uint8_t has[256], ct[256][3];
    uint32_t *cols = NULL;
    int ncols = 0;
    uint32_t *pix = NULL;
    int *remap_d = NULL;
    if (!why && im.has_ctab) {
        indexed_table(&im, has, ct);
        remap_palette(256, ct, has, pal, remap);
        W = im.w;
        H = im.h;
    } else {
        /* The Simpsons and Muppets sheets are direct-colour pictures rather than indexed ones, so
         * they are decoded to RGB and then mapped to the game's table the way QuickDraw would. */
        Canvas cv;
        memset(&cv, 0, sizeof cv);
        why = pict_walk(d, NULL, NULL, &cv);
        if (why) fail("%s/%s: %s", dir, sd->name, why);
        W = cv.w;
        H = cv.h;
        size_t np = (size_t)W * (size_t)H;
        pix = zalloc(sizeof(uint32_t) * np);
        cols = zalloc(sizeof(uint32_t) * np);
        for (size_t i = 0; i < np; i++)
            pix[i] = (uint32_t)cv.rgb[i * 3] << 16 | (uint32_t)cv.rgb[i * 3 + 1] << 8 | cv.rgb[i * 3 + 2];
        memcpy(cols, pix, sizeof(uint32_t) * np);
        qsort(cols, np, sizeof(uint32_t), cmp_u32);
        for (size_t i = 0; i < np; i++)
            if (i == 0 || cols[i] != cols[i - 1]) cols[ncols++] = cols[i];
        uint8_t (*rgb)[3] = zalloc((size_t)ncols * 3);
        for (int i = 0; i < ncols; i++) {
            rgb[i][0] = (uint8_t)(cols[i] >> 16);
            rgb[i][1] = (uint8_t)(cols[i] >> 8);
            rgb[i][2] = (uint8_t)cols[i];
        }
        remap_d = zalloc(sizeof(int) * (size_t)ncols);
        remap_palette(ncols, rgb, NULL, pal, remap_d);
    }
    int trans = white_index(pal);
    int pitch = fw + 1;
    if (W == fw && H == frames * fh) { ox = 0; oy = 0; pitch = fw; }
    int n = (W - ox + (pitch - fw)) / pitch;
    if (n < 1) n = 1;
    if (n > frames) n = frames;
    uint8_t *out = zalloc((size_t)n * (size_t)fw * (size_t)fh);
    for (int f = 0; f < n; f++) {
        int sx = ox + f * pitch;
        for (int y = 0; y < fh; y++) {
            int row = oy + y;
            if (row >= H) continue;
            for (int x = 0; x < fw; x++) {
                if (sx + x >= W) continue;
                size_t at = (size_t)row * (size_t)W + (size_t)(sx + x);
                int v;
                if (!pix) v = remap[im.idx[at] & 255];
                else {
                    uint32_t key = pix[at];
                    int lo = 0, hi = ncols - 1;
                    while (lo < hi) {
                        int mid = (lo + hi) / 2;
                        if (cols[mid] < key) lo = mid + 1; else hi = mid;
                    }
                    v = remap_d[lo];
                }
                out[(size_t)f * (size_t)fw * (size_t)fh + (size_t)y * (size_t)fw + (size_t)x] = (uint8_t)v;
            }
        }
    }
    char rel[200];
    snprintf(rel, sizeof rel, "%s/%s.bmp", dir, sd->name);
    put_bmp(rel, fw, n * fh, out, pal);
    char t[64];
    int tn = snprintf(t, sizeof t, "SPRITE %d %d %d %d\n", n, fw, fh, trans);
    snprintf(rel, sizeof rel, "%s/%s.spr", dir, sd->name);
    put_file(rel, t, (size_t)tn);
    arena_release(m);
}

/* ----------------------------------------------------------------------------- QuickDraw text pictures */
typedef struct { char k; int a, b, c, d, e, f; const uint8_t *txt; int ntxt; } DOp;
#define MAX_DOPS 4096
#define MAX_DBMP 16

static int fix2(int op)
{
    switch (op) {
    case 0x11: return 1;
    case 0x15: return 2;  case 0x16: return 2;
    case 0x17: case 0x18: case 0x19: return 0;
    }
    return fixed_len(op);
}

/* PICT 4000-4002 and 5555 are drawing commands, not pictures: text runs with colour and rectangle
 * fills. They are flattened into a draw list the port replays. */
static void write_draw_pict(Buf d, Pal pal, const char *stem, int ox, int oy)
{
    int m = arena_mark();
    DOp *ops = zalloc(sizeof(DOp) * MAX_DOPS);
    Cap *bm = zalloc(sizeof(Cap) * MAX_DBMP);
    int nops = 0, nbm = 0;
    size_t pos = u16(d, 10) == 0x0011 && u16(d, 12) == 0x02FF ? 14 : 10;
    int x = 0, y = 0, font = 3, size = 12, face = 0;
    uint8_t col[3] = { 0, 0, 0 };
    int lr[4] = { 0, 0, 0, 0 };
    while (pos + 2 <= d.n && nops < MAX_DOPS - 1) {
        int op = (int)u16(d, pos);
        pos += 2;
        if (op == 0xFF) break;
        if (op == 0x0C00) pos += 24;
        else if (op == 0x01) pos += u16(d, pos);
        else if (op == 0xA1) pos += 2 + 2 + u16(d, pos + 2);
        else if (op >= 0x2C && op <= 0x2F) pos += 2 + u16(d, pos);
        else if (op == 0x03) { font = (int)u16(d, pos); pos += 2; }
        else if (op == 0x0D) { size = (int)u16(d, pos); pos += 2; }
        else if (op == 0x04) { face = (int)u8(d, pos); pos += 2; }
        else if (op == 0x1A) {
            col[0] = (uint8_t)(u16(d, pos) >> 8);
            col[1] = (uint8_t)(u16(d, pos + 2) >> 8);
            col[2] = (uint8_t)(u16(d, pos + 4) >> 8);
            pos += 6;
        } else if (op == 0x20) {
            int y1 = s16(d, pos), x1 = s16(d, pos + 2), y2 = s16(d, pos + 4), x2 = s16(d, pos + 6);
            pos += 8;
            ops[nops++] = (DOp){ 'L', x1, y1, x2, y2, nearest(pal, col), 0, NULL, 0 };
        } else if (op >= 0x28 && op <= 0x2B) {
            if (op == 0x28) { y = s16(d, pos); x = s16(d, pos + 2); pos += 4; }
            else if (op == 0x29) { x += (int)u8(d, pos); pos += 1; }
            else if (op == 0x2A) { y += (int)u8(d, pos); pos += 1; }
            else { x += (int)u8(d, pos); y += (int)u8(d, pos + 1); pos += 2; }
            int n = (int)u8(d, pos);
            const uint8_t *txt = pos + 1 <= d.n ? d.p + pos + 1 : d.p;
            int avail = (int)(d.n > pos + 1 ? d.n - pos - 1 : 0);
            ops[nops++] = (DOp){ 'T', x, y, font, size, face, nearest(pal, col), txt, n < avail ? n : avail };
            pos += 1 + (size_t)n;
        } else if (op >= 0x30 && op <= 0x34) {
            int t_ = s16(d, pos), l_ = s16(d, pos + 2), b_ = s16(d, pos + 4), r_ = s16(d, pos + 6);
            pos += 8;
            lr[0] = l_; lr[1] = t_; lr[2] = r_; lr[3] = b_;
            ops[nops++] = (DOp){ op == 0x31 ? 'R' : 'F', l_, t_, r_, b_, nearest(pal, col), 0, NULL, 0 };
        } else if (op >= 0x38 && op <= 0x3C) {
            ops[nops++] = (DOp){ op == 0x39 ? 'R' : 'F', lr[0], lr[1], lr[2], lr[3], nearest(pal, col), 0, NULL, 0 };
        } else if (op == 0x90 || op == 0x98 || op == 0x99 || op == 0x9A || op == 0x9B || op == 0x91) {
            if (nbm >= MAX_DBMP) break;
            Cap *c = &bm[nbm];
            pos = pict_draw(d, pos, op, c);
            ops[nops++] = (DOp){ 'B', c->dst[1], c->dst[0], nbm, c->dst[3] - c->dst[1], c->dst[2] - c->dst[0], 0, NULL, 0 };
            nbm++;
        } else {
            int fl = fix2(op);
            if (fl < 0) break;
            pos += (size_t)fl;
        }
        if (pos & 1) pos++;
    }
    Str s = { 0 };
    for (int i = 0; i < nops; i++) {
        DOp o = ops[i];
        bool shift = ox != 0 || oy != 0;
        if (o.k == 'T') {
            str_f(&s, "T %d %d %d %d %d %d ", o.a - (shift ? ox : 0), o.b - (shift ? oy : 0), o.c, o.d, o.e, o.f);
            str_add(&s, o.txt, (size_t)o.ntxt);
            str_add(&s, "\n", 1);
        } else if (o.k == 'B') {
            str_f(&s, "B %d %d %d %d %d\n", o.a - ox, o.b - oy, o.c, o.d, o.e);
        } else {
            str_f(&s, "%c %d %d %d %d %d\n", o.k, o.a - ox, o.b - oy, o.c - ox, o.d - oy, o.e);
        }
    }
    char rel[200];
    snprintf(rel, sizeof rel, "%s.txt", stem);
    put_file(rel, s.p ? s.p : "", s.n);
    for (int i = 0; i < nbm; i++) {
        Cap *c = &bm[i];
        int mm = arena_mark();
        uint8_t has[256], ct[256][3];
        memcpy(has, c->has, 256);
        memcpy(ct, c->ct, sizeof ct);
        int remap[256];
        remap_palette(256, ct, has, pal, remap);
        uint8_t *out = zalloc((size_t)c->w * (size_t)c->h + 1);
        for (int yy = 0; yy < c->h; yy++)
            for (int xx = 0; xx < c->w; xx++) {
                unsigned v = c->pixsize <= 8 ? cap_pix(c, xx, yy) : 0;
                out[(size_t)yy * (size_t)c->w + (size_t)xx] = (uint8_t)(v < 256 && has[v] ? remap[v] : 0);
            }
        snprintf(rel, sizeof rel, "%s_img%d.bmp", stem, i);
        put_bmp(rel, c->w, c->h, out, pal);
        arena_release(mm);
    }
    arena_release(m);
}

/* ----------------------------------------------------------------------------- cursor, sound, text */
static void write_cursor(Buf d, const char *rel)
{
    size_t pmap = u32(d, 2), pdata = u32(d, 6);
    int hy = s16(d, 84), hx = s16(d, 86);
    size_t rb = u16(d, pmap + 4) & 0x3FFF;
    int top = s16(d, pmap + 6), left = s16(d, pmap + 8), bot = s16(d, pmap + 10), right = s16(d, pmap + 12);
    int pixsize = (int)u16(d, pmap + 32);
    size_t ctoff = u32(d, pmap + 42);
    unsigned fl = u16(d, ctoff + 4), n = u16(d, ctoff + 6);
    int w = right - left, h = bot - top;
    if (w <= 0 || h <= 0 || w > 64 || h > 64 || (pixsize != 1 && pixsize != 2 && pixsize != 4 && pixsize != 8))
        fail("the cursor is not in the format I know");
    uint8_t has[256], ct[256][3];
    memset(has, 0, sizeof has);
    for (unsigned i = 0; i <= n; i++) {
        unsigned v = u16(d, ctoff + 8 + (size_t)i * 8);
        unsigned k = (fl & 0x8000) ? i : v;
        if (k < 256) {
            has[k] = 1;
            ct[k][0] = (uint8_t)(u16(d, ctoff + 10 + (size_t)i * 8) >> 8);
            ct[k][1] = (uint8_t)(u16(d, ctoff + 12 + (size_t)i * 8) >> 8);
            ct[k][2] = (uint8_t)(u16(d, ctoff + 14 + (size_t)i * 8) >> 8);
        }
    }
    int keyidx[256], nk = 0;
    for (int k = 0; k < 256; k++) keyidx[k] = has[k] ? nk++ : -1;
    Str s = { 0 };
    str_f(&s, "CURSOR %d %d %d %d\nCOLORS %d\n", w, h, hx, hy, nk);
    for (int k = 0; k < 256; k++)
        if (has[k]) str_f(&s, "%d %d %d\n", ct[k][0], ct[k][1], ct[k][2]);
    int per = 8 / pixsize, mk = (1 << pixsize) - 1;
    for (int y = 0; y < h; y++) {
        unsigned mrow = u16(d, 52 + (size_t)y * 2);
        for (int x = 0; x < w; x++) {
            unsigned byte = u8(d, pdata + (size_t)y * rb + (size_t)(x / per));
            int v = (int)((byte >> (8 - pixsize * (x % per + 1))) & (unsigned)mk);
            char ch = '.';
            if ((mrow >> (15 - x)) & 1) ch = (char)('a' + (keyidx[v] < 0 ? 0 : keyidx[v]));
            str_add(&s, &ch, 1);
        }
        str_add(&s, "\n", 1);
    }
    put_file(rel, s.p, s.n);
}

static bool snd_to_wav(Buf d, const char *rel)
{
    unsigned fmt = u16(d, 0);
    size_t p = 2;
    if (fmt == 1) p += 2 + 6 * (size_t)u16(d, p); else p += 2;
    unsigned nc = u16(d, p);
    p += 2;
    long off = -1;
    for (unsigned i = 0; i < nc; i++) {
        unsigned cmd = u16(d, p);
        uint32_t p2 = u32(d, p + 4);
        p += 8;
        if ((cmd & 0x7FFF) == 0x50 || (cmd & 0x7FFF) == 0x51) off = (long)p2;
    }
    if (off < 0 || (size_t)off + 22 > d.n) return false;
    size_t n = u32(d, (size_t)off + 4);
    /* the rate is 16.16 fixed point, rounded half to even the way Python's round() does it */
    uint32_t fx = u32(d, (size_t)off + 8), rate = fx >> 16, rem = fx & 0xFFFF;
    if (rem > 0x8000 || (rem == 0x8000 && (rate & 1))) rate++;
    size_t have = d.n - (size_t)off - 22;
    if (n > have) n = have;
    uint8_t *w = zalloc(44 + n);
    memcpy(w, "RIFF", 4);
    uint32_t v = (uint32_t)(36 + n);
    w[4] = (uint8_t)v; w[5] = (uint8_t)(v >> 8); w[6] = (uint8_t)(v >> 16); w[7] = (uint8_t)(v >> 24);
    memcpy(w + 8, "WAVEfmt ", 8);
    w[16] = 16; w[20] = 1; w[22] = 1;
    w[24] = (uint8_t)rate; w[25] = (uint8_t)(rate >> 8); w[26] = (uint8_t)(rate >> 16); w[27] = (uint8_t)(rate >> 24);
    w[28] = (uint8_t)rate; w[29] = (uint8_t)(rate >> 8); w[30] = (uint8_t)(rate >> 16); w[31] = (uint8_t)(rate >> 24);
    w[32] = 1; w[34] = 8;
    memcpy(w + 36, "data", 4);
    w[40] = (uint8_t)n; w[41] = (uint8_t)(n >> 8); w[42] = (uint8_t)(n >> 16); w[43] = (uint8_t)(n >> 24);
    memcpy(w + 44, d.p + off + 22, n);
    put_file(rel, w, 44 + n);
    return true;
}

static void write_strings(Buf d, const char *rel)
{
    Str s = { 0 };
    unsigned c = u16(d, 0);
    size_t q = 2;
    for (unsigned i = 0; i < c; i++) {
        unsigned L = u8(d, q);
        if (q + 1 + L <= d.n) str_add(&s, d.p + q + 1, L);
        str_add(&s, "\n", 1);
        q += 1 + L;
    }
    str_f(&s, "1.0.3\nNOT REGISTERED\n\n");
    put_file(rel, s.p, s.n);
}

static void write_thanks(Buf d, const char *rel)
{
    Str s = { 0 };
    unsigned c = u16(d, 0);
    size_t q = 2;
    for (unsigned i = 0; i < c; i++) {
        unsigned L = u8(d, q);
        if (q + 1 + L <= d.n) str_add(&s, d.p + q + 1, L);
        str_add(&s, "\n", 1);
        q += 1 + L;
    }
    put_file(rel, s.p ? s.p : "", s.n);
}

static void write_scores(Buf ap, const char *rel)
{
    Str s = { 0 };
    str_f(&s, "volume %d\nmusic %d\n", s16(ap, 0x100), s16(ap, 0x104));
    for (int i = 0; i < 10; i++) {
        size_t o = 0x106 + (size_t)i * 0x100;
        unsigned L = u8(ap, o);
        if (o + 1 + L <= ap.n) str_add(&s, ap.p + o + 1, L);
        str_f(&s, "\t%u\t%d\n", (unsigned)u32(ap, 0xB06 + 4 * (size_t)i), s16(ap, 0xB2E + 2 * (size_t)i));
    }
    put_file(rel, s.p, s.n);
}

/* ----------------------------------------------------------------------------- the job */
typedef struct { Zip z; } Game;

/* The path of an entry that is `tail` below the game's own folder, directly or inside a pack folder
 * whose name starts with `head` (the folder names carry a Mac florin that unzippers spell several
 * ways, so only the start is compared). */
static int find_fork_entry(const Game *g, const char *prefix, const char *head, const char *tail)
{
    for (int i = 0; i < g->z.ne; i++) {
        const char *nm = g->z.e[i].name;
        if (strncmp(nm, prefix, strlen(prefix)) != 0) continue;
        const char *rest = nm + strlen(prefix);
        if (!head) {
            if (!strcmp(rest, tail)) return i;
        } else {
            const char *slash = strchr(rest, '/');
            if (!slash) continue;
            if (strncmp(rest, head, strlen(head)) != 0) continue;
            if (!strcmp(slash + 1, tail)) return i;
        }
    }
    return -1;
}

static Buf get_fork_data(const Game *g, const char *prefix, const char *head, const char *tail, bool must)
{
    Buf none = { NULL, 0 };
    int i = find_fork_entry(g, prefix, head, tail);
    if (i < 0) {
        if (must) fail("the zip has no '%s' - is it the Apeiron 1.0.3 download?", tail);
        return none;
    }
    size_t n;
    uint8_t *p = zip_extract(&g->z, i, &n);
    Buf b = { p, n };
    return b;
}

static void do_packs(const Game *g, const char *prefix, Pal game_clut)
{
    static const struct { const char *name, *head, *tail; bool sprites; } P[] = {
        { "simpsons", "Simpson Apeiron", ".rsrc/Apeiron Sprites", true },
        { "muppets", "Apeiron Muppets", ".rsrc/Apeiron Muppets", true },
        { "startrek", "Star Trek Apeiron", "Equipment/.rsrc/Apeiron Sounds", false },
    };
    for (int pi = 0; pi < 3; pi++) {
        int m = arena_mark();
        Buf f = get_fork_data(g, prefix, P[pi].head, P[pi].tail, false);
        if (!f.p) { say("  pack %s: not in this zip, skipped", P[pi].name); arena_release(m); continue; }
        Fork fk;
        fork_parse(&fk, f);
        char dir[200];
        if (P[pi].sprites) {
            snprintf(dir, sizeof dir, "packs/%s/spr", P[pi].name);
            make_dir("packs");
            make_dir(dir);
            int done = 0;
            for (int i = 0; i < N_SPRITES; i++) {
                Buf pict;
                if (!fork_get(&fk, "PICT", SPRITES[i].pid, &pict)) continue;
                bool shot = SPRITES[i].pid == 1003 || SPRITES[i].pid == 1016;
                extract_sprite(pict, &SPRITES[i], game_clut, dir, shot ? 7 : 1, shot ? 3 : 1);
                done++;
            }
            say("  pack %s: %d sprite sheets", P[pi].name, done);
        } else {
            snprintf(dir, sizeof dir, "packs/%s/snd", P[pi].name);
            make_dir("packs");
            make_dir(dir);
            int n = 0;
            for (int i = 0; i < fk.n; i++) {
                if (memcmp(fk.r[i].type, "snd ", 4)) continue;
                char rel[260];
                snprintf(rel, sizeof rel, "%s/%d.wav", dir, fk.r[i].id);
                if (snd_to_wav(fk.r[i].data, rel)) n++;
            }
            Buf mf = get_fork_data(g, prefix, P[pi].head, "Equipment/.rsrc/Apeiron Music", false);
            if (mf.p) {
                Fork mk;
                fork_parse(&mk, mf);
                static const struct { int id; const char *stem; } M[] = { { 1000, "music_start" }, { 1001, "music_loop" } };
                for (int k = 0; k < 2; k++) {
                    Buf sn;
                    char rel[260];
                    snprintf(rel, sizeof rel, "%s/%s.wav", dir, M[k].stem);
                    if (fork_get(&mk, "snd ", M[k].id, &sn) && snd_to_wav(sn, rel)) n++;
                }
            }
            say("  pack %s: %d sounds", P[pi].name, n);
        }
        arena_release(m);
    }
}

static void convert_main(const Game *g, const char *prefix)
{
    Buf f_app = get_fork_data(g, prefix, NULL, ".rsrc/Apeiron", true);
    Buf f_titles = get_fork_data(g, prefix, NULL, ".rsrc/Apeiron Titles", true);
    Buf f_sounds = get_fork_data(g, prefix, NULL, ".rsrc/Apeiron Sounds", true);
    Buf f_sprites = get_fork_data(g, prefix, NULL, ".rsrc/Apeiron Sprites", true);
    Buf f_music = get_fork_data(g, prefix, NULL, ".rsrc/Apeiron Music", true);
    Buf f_scores = get_fork_data(g, prefix, NULL, ".rsrc/Apeiron Scores", true);
    Fork app, titles, sounds, sprites, music, scores;
    fork_parse(&app, f_app);
    fork_parse(&titles, f_titles);
    fork_parse(&sounds, f_sounds);
    fork_parse(&sprites, f_sprites);
    fork_parse(&music, f_music);
    fork_parse(&scores, f_scores);

    Pal game_clut, title_clut, c999, c889, c888;
    clut_to_pal(need(&app, "Apeiron", "clut", 128), game_clut);
    clut_to_pal(need(&app, "Apeiron", "clut", 129), title_clut);
    clut_to_pal(need(&titles, "Apeiron Titles", "clut", 999), c999);
    clut_to_pal(need(&titles, "Apeiron Titles", "clut", 889), c889);
    clut_to_pal(need(&titles, "Apeiron Titles", "clut", 888), c888);

    say("title pictures");
    indexed_bmp(need(&titles, "Apeiron Titles", "PICT", 999), c999, "ambrosia_logo.bmp");
    indexed_bmp(need(&titles, "Apeiron Titles", "PICT", 889), c889, "apeiron_intro.bmp");
    indexed_bmp(need(&titles, "Apeiron Titles", "PICT", 400), title_clut, "title_logo.bmp");
    indexed_bmp(need(&titles, "Apeiron Titles", "PICT", 402), title_clut, "title_morrison.bmp");

    say("backgrounds");
    static const int PAT[] = { 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 400, 401, 402 };
    for (int i = 0; i < 14; i++) {
        char rel[64];
        snprintf(rel, sizeof rel, "pattern_%d.bmp", PAT[i]);
        pattern_bmp(need(&titles, "Apeiron Titles", "ppat", PAT[i]), PAT[i] == 400 ? title_clut : game_clut, rel);
    }

    say("sounds");
    make_dir("snd");
    int nsnd = 0;
    for (int i = 0; i < sounds.n; i++) {
        if (memcmp(sounds.r[i].type, "snd ", 4)) continue;
        char rel[64];
        snprintf(rel, sizeof rel, "snd/%d.wav", sounds.r[i].id);
        if (snd_to_wav(sounds.r[i].data, rel)) nsnd++;
        else say("  sound %d could not be read", sounds.r[i].id);
    }
    {
        Buf s1, s2;
        if (fork_get(&music, "snd ", 1000, &s1) && snd_to_wav(s1, "snd/music_start.wav")) nsnd++;
        if (fork_get(&music, "snd ", 1001, &s2) && snd_to_wav(s2, "snd/music_loop.wav")) nsnd++;
    }
    say("  %d sounds", nsnd);

    say("sprites");
    make_dir("spr");
    for (int i = 0; i < N_SPRITES; i++) {
        bool shot = SPRITES[i].pid == 1003 || SPRITES[i].pid == 1016;
        extract_sprite(need(&sprites, "Apeiron Sprites", "PICT", SPRITES[i].pid), &SPRITES[i], game_clut, "spr",
                       shot ? 7 : 1, shot ? 3 : 1);
    }

    say("fonts");
    static const struct { int size, id; } NY[] = { { 14, 1067 }, { 18, 32121 }, { 24, 28514 } };
    for (int i = 0; i < 3; i++) {
        char stem[64];
        snprintf(stem, sizeof stem, "font_newyork%d", NY[i].size);
        nfnt_to_font(need(&app, "Apeiron", "NFNT", NY[i].id), stem);
    }
    baked_font(&baked_fonts[0]);
    baked_font(&baked_fonts[1]);
    {
        Str s = { 0 };
        str_add(&s, baked_credits, (size_t)baked_credits_len);
        str_f(&s, "\n\nThe Geneva stand-in above is Geneva 9.1; its licence follows.\n\n");
        str_add(&s, baked_ofl, (size_t)baked_ofl_len);
        put_file("fonts-credits.txt", s.p, s.n);
    }

    say("score panel, buttons and instruction pages");
    indexed_bmp(need(&titles, "Apeiron Titles", "PICT", 401), game_clut, "scorematt.bmp");
    static const struct { int id; const char *name; } BTN[] = {
        { 1000, "btn_up" }, { 1001, "btn_up_hit" }, { 1002, "btn_down" }, { 1003, "btn_down_hit" } };
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof rel, "%s.bmp", BTN[i].name);
        cicn_bmp(need(&app, "Apeiron", "cicn", BTN[i].id), title_clut, rel);
    }
    cicn_native_bmp(need(&app, "Apeiron", "cicn", 128), "dialog_icon.bmp");
    for (int i = 0; i < 3; i++) {
        char stem[32];
        snprintf(stem, sizeof stem, "about_%d", i);
        write_draw_pict(need(&titles, "Apeiron Titles", "PICT", 4000 + i), game_clut, stem, 0, 0);
    }
    indexed_bmp(need(&titles, "Apeiron Titles", "PICT", 3130), game_clut, "paused.bmp");
    write_cursor(need(&app, "Apeiron", "crsr", 128), "cursor_hand.cur");
    write_thanks(need(&app, "Apeiron", "STR#", 150), "thanks.txt");

    say("the optional art and sound packs");
    do_packs(g, prefix, game_clut);

    say("game over and ending pictures");
    indexed_bmp(need(&titles, "Apeiron Titles", "PICT", 888), c888, "gameover_logo.bmp");
    for (int i = 0; i < 8; i++) {
        char rel[64];
        snprintf(rel, sizeof rel, "gameover_anim%d.bmp", i);
        indexed_bmp(need(&titles, "Apeiron Titles", "PICT", 900 + i), c888, rel);
    }
    indexed_bmp(need(&app, "Apeiron", "PICT", 667), c888, "ending.bmp");
    indexed_bmp(need(&app, "Apeiron", "PICT", 151), title_clut, "about_logo.bmp");
    indexed_bmp(need(&app, "Apeiron", "PICT", 150), title_clut, "about_credits.bmp");
    indexed_bmp(need(&app, "Apeiron", "PICT", 153), title_clut, "about_strip.bmp");
    write_draw_pict(need(&app, "Apeiron", "PICT", 5555), title_clut, "poem", 74 - 4, 48 - 5);

    write_strings(need(&app, "Apeiron", "STR#", 133), "strings.txt");
    write_scores(need(&scores, "Apeiron Scores", "ApPr", 128), "scores.txt");
}

/* Is this the game's zip? Looks only at the directory at the end of the file - a Downloads folder
 * can hold zips of any size, and a TV has no business reading all of them. */
bool convert_zip_ok(const char *zip)
{
    FILE *f = fopen(zip, "rb");
    if (!f) return false;
    bool ok = false;
    uint8_t *tail = NULL, *cd = NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 22) goto out;
    long tl = len < 70000 ? len : 70000;
    tail = malloc((size_t)tl);
    if (!tail) goto out;
    fseek(f, len - tl, SEEK_SET);
    if (fread(tail, 1, (size_t)tl, f) != (size_t)tl) goto out;
    long i;
    for (i = tl - 22; i >= 0; i--)
        if (le32(tail + i) == 0x06054b50u) break;
    if (i < 0) goto out;
    size_t cdsz = le32(tail + i + 12), cdoff = le32(tail + i + 16);
    if (cdsz == 0 || cdsz > 16u * 1024 * 1024 || cdoff + cdsz > (size_t)len) goto out;
    cd = malloc(cdsz);
    if (!cd) goto out;
    fseek(f, (long)cdoff, SEEK_SET);
    if (fread(cd, 1, cdsz, f) != cdsz) goto out;
    size_t q = 0;
    while (q + 46 <= cdsz && le32(cd + q) == 0x02014b50u) {
        unsigned nl = le16(cd + q + 28), xl = le16(cd + q + 30), cl = le16(cd + q + 32);
        if (q + 46 + nl > cdsz) break;
        static const char key[] = ".rsrc/Apeiron Titles";
        size_t kl = sizeof key - 1;
        if (nl >= kl && !memcmp(cd + q + 46 + nl - kl, key, kl)) { ok = true; break; }
        q += 46 + nl + xl + cl;
    }
out:
    free(tail);
    free(cd);
    fclose(f);
    return ok;
}

bool convert_zip(const char *zip, const char *out, ConvSay say_cb, void *ud, char *err, size_t errn)
{
    g_say = say_cb;
    g_ud = ud;
    g_files = 0;
    g_err[0] = 0;
    snprintf(g_out, sizeof g_out, "%s", out);
    int m = arena_mark();
    size_t n = 0;
    uint8_t *raw = read_whole(zip, &n);
    bool ok = false;
    if (!raw) {
        snprintf(err, errn, "Cannot read '%s'.", zip);
        g_say = NULL;
        return false;
    }
    if (!setjmp(g_jb)) {
        Game g;
        memset(&g, 0, sizeof g);
        g.z.p = raw;
        g.z.n = n;
        if (!zip_parse(&g.z)) fail("'%s' is not a zip file I can read", zip);
        char prefix[600];
        if (!zip_game_prefix(&g.z, prefix, sizeof prefix))
            fail("This zip does not have the game's resource forks in it (a '.rsrc/Apeiron Titles' "
                 "file). It should be the Apeiron 1.0.3 download for the Mac.");
        const char *base = zip;
        for (const char *c = zip; *c; c++) if (*c == '/' || *c == '\\') base = c + 1;
        say("reading %s", base);
        SDL_CreateDirectory(g_out);
        convert_main(&g, prefix);
        say("done: %d files", g_files);
        ok = true;
    } else {
        snprintf(err, errn, "%s", g_err);
    }
    arena_release(m);
    free(raw);
    g_say = NULL;
    return ok;
}

/* ----------------------------------------------------------------------------- finding things */
static bool has_word(const char *name, const char *w)
{
    size_t lw = strlen(w);
    for (const char *p = name; *p; p++)
        if (!SDL_strncasecmp(p, w, lw)) return true;
    return false;
}

static bool zip_name_ok(const char *name)
{
    size_t n = strlen(name);
    if (n < 5 || SDL_strcasecmp(name + n - 4, ".zip")) return false;
    return has_word(name, "apeiron") && !has_word(name, "font") && !has_word(name, "tools") &&
           !has_word(name, "assets");
}

static bool scan_for_zip(const char *dir, char *out, size_t n)
{
    if (!dir || !*dir) return false;
    DirScan s;
    if (!dir_open(&s, dir)) return false;
    char best[1200] = "";
    while (dir_next(&s)) {
        if (!zip_name_ok(s.name)) continue;
        char full[1200];
        if (snprintf(full, sizeof full, "%s/%s", dir, s.name) >= (int)sizeof full) continue;
        if (!convert_zip_ok(full)) continue;
        if (!best[0] || strcmp(full, best) < 0) memcpy(best, full, strlen(full) + 1);
    }
    dir_close(&s);
    if (!best[0]) return false;
    snprintf(out, n, "%s", best);
    return true;
}

bool convert_find_zip(char *out, size_t n)
{
    const char *tries[16];
    char store[10][1200];
    int t = 0, ns = 0;
    #define ADD(...) do { if (t < 15 && ns < 10) { snprintf(store[ns], sizeof store[0], __VA_ARGS__); tries[t++] = store[ns++]; } } while (0)
    const char *base = SDL_GetBasePath();
    if (base) ADD("%s", base);
    ADD(".");
#ifdef __ANDROID__
    const char *ext = SDL_GetAndroidExternalStoragePath();
    if (ext && *ext) ADD("%s", ext);
    ADD("/sdcard/Download");
    ADD("/storage/emulated/0/Download");
    ADD("/sdcard");
#else
    static const SDL_Folder F[] = { SDL_FOLDER_DOWNLOADS, SDL_FOLDER_DESKTOP, SDL_FOLDER_DOCUMENTS, SDL_FOLDER_HOME };
    for (int i = 0; i < 4; i++) {
        const char *fp = SDL_GetUserFolder(F[i]);
        if (fp && *fp) {
            ADD("%s", fp);
            size_t l = strlen(store[ns - 1]);
            while (l > 1 && (store[ns - 1][l - 1] == '/' || store[ns - 1][l - 1] == '\\')) store[ns - 1][--l] = 0;
        }
    }
#endif
    #undef ADD
    for (int i = 0; i < t; i++)
        if (scan_for_zip(tries[i], out, n)) return true;
    return false;
}

static bool writable_dir(const char *dir)
{
    if (!dir || !*dir) return false;
    SDL_CreateDirectory(dir);
    char p[1300];
    snprintf(p, sizeof p, "%s/.write-test", dir);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    fclose(f);
    remove(p);
    return true;
}

bool convert_out_dir(char *out, size_t n)
{
    char p[1300];
#ifdef __ANDROID__
    /* The app's own folder needs no permission, always works, and is the first place the game
     * looks. It is also removed with the app, which is the right thing for 20 MB of someone else's
     * artwork. */
    const char *ext = SDL_GetAndroidExternalStoragePath();
    const char *inr = SDL_GetAndroidInternalStoragePath();
    if (ext && *ext) { snprintf(p, sizeof p, "%s/apeiron_classic_assets", ext); if (writable_dir(p)) { snprintf(out, n, "%s", p); return true; } }
    if (inr && *inr) { snprintf(p, sizeof p, "%s/apeiron_classic_assets", inr); if (writable_dir(p)) { snprintf(out, n, "%s", p); return true; } }
    return false;
#else
    const char *base = SDL_GetBasePath();
    if (base && *base) {
        snprintf(p, sizeof p, "%sapeiron_classic_assets", base);
        if (writable_dir(p)) { snprintf(out, n, "%s", p); return true; }
    }
    char *pref = SDL_GetPrefPath("apeiron-sdl3", "apeiron");
    if (pref) {
        snprintf(p, sizeof p, "%sapeiron_classic_assets", pref);
        SDL_free(pref);
        if (writable_dir(p)) { snprintf(out, n, "%s", p); return true; }
    }
    return false;
#endif
}
