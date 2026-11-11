#include "hud_flog_core.h"
#include <string.h>

static uint32_t slot_addr(const flog_t *f, unsigned slot) { return f->base + slot * FLOG_SLOT_BYTES; }
static uint32_t data_start(unsigned sector) { return sector == 0 ? FLOG_HDR_BYTES : FLOG_TAG_BYTES; }

/* размер текста при позиции записи (sector, off) */
static uint32_t size_at(unsigned sector, unsigned off)
{
    if (sector == 0) return off - FLOG_HDR_BYTES;
    return (FLOG_SECTOR - FLOG_HDR_BYTES) + (sector - 1u) * (FLOG_SECTOR - FLOG_TAG_BYTES) + (off - FLOG_TAG_BYTES);
}

static void make_tag(uint8_t t[4], uint16_t seq) { t[0] = 'S'; t[1] = (uint8_t)seq; t[2] = (uint8_t)(seq >> 8); t[3] = 0xA5; }

typedef struct { bool ok; uint16_t seq; uint32_t epoch; } hdr_t;

static bool read_hdr(flog_t *f, unsigned slot, hdr_t *h)
{
    uint8_t b[12];
    h->ok = false;
    if (!f->io.read(slot_addr(f, slot), b, sizeof b, f->io.user)) return false;
    if (memcmp(b, "HLG1", 4) != 0) return true;
    const uint16_t seq = (uint16_t)(b[4] | b[5] << 8), inv = (uint16_t)(b[6] | b[7] << 8);
    if ((uint16_t)~seq != inv || seq >= FLOG_SEQ_MOD || (seq % FLOG_SLOTS) != slot) return true;
    h->ok = true; h->seq = seq;
    h->epoch = (uint32_t)b[8] | (uint32_t)b[9] << 8 | (uint32_t)b[10] << 16 | (uint32_t)b[11] << 24;
    return true;
}

/* конец файла слота: последний сектор с тегом seq и первый байт 0xFF в нём */
static bool scan_end(flog_t *f, unsigned slot, uint16_t seq, unsigned *sector, unsigned *off)
{
    uint8_t want[4], tag[4];
    make_tag(want, seq);
    unsigned last = 0;
    for (unsigned k = 1; k < FLOG_SECTORS; k++) {
        if (!f->io.read(slot_addr(f, slot) + k * FLOG_SECTOR, tag, 4, f->io.user)) return false;
        if (memcmp(tag, want, 4) != 0) break;
        last = k;
    }
    if (!f->io.read(slot_addr(f, slot) + last * FLOG_SECTOR, f->sec, FLOG_SECTOR, f->io.user)) return false;
    unsigned o = (unsigned)data_start(last);
    while (o < FLOG_SECTOR && f->sec[o] != 0xFF) o++;
    *sector = last; *off = o;
    return true;
}

/* начать новый файл seq: стереть сектор 0 слота, записать заголовок */
static bool new_file(flog_t *f, uint16_t seq, uint32_t epoch)
{
    const unsigned slot = seq % FLOG_SLOTS;
    const uint32_t a = slot_addr(f, slot);
    uint8_t h[12];
    if (!f->io.erase(a, f->io.user)) return false;
    memcpy(h, "HLG1", 4);
    h[4] = (uint8_t)seq; h[5] = (uint8_t)(seq >> 8); h[6] = (uint8_t)~seq; h[7] = (uint8_t)(~seq >> 8);
    size_t n = 8;
    if (epoch && epoch != FLOG_EPOCH_UNKNOWN) {
        h[8] = (uint8_t)epoch; h[9] = (uint8_t)(epoch >> 8); h[10] = (uint8_t)(epoch >> 16); h[11] = (uint8_t)(epoch >> 24);
        n = 12;
    }
    if (!f->io.write(a, h, n, f->io.user)) return false;
    f->seq = seq; f->slot = (uint8_t)slot; f->sector = 0; f->off = FLOG_HDR_BYTES;
    f->start_epoch = n == 12 ? epoch : FLOG_EPOCH_UNKNOWN;
    return true;
}

bool flog_mount(flog_t *f, const flog_io_t *io, uint32_t base)
{
    f->io = *io; f->base = base;
    hdr_t h[FLOG_SLOTS];
    int best = -1;
    for (unsigned s = 0; s < FLOG_SLOTS; s++) {
        if (!read_hdr(f, s, &h[s])) return false;
        if (!h[s].ok) continue;
        if (best < 0 || ((h[s].seq + FLOG_SEQ_MOD - h[best].seq) % FLOG_SEQ_MOD) < FLOG_SEQ_MOD / 2) best = (int)s;
    }
    if (best < 0) return new_file(f, 0, 0);
    unsigned sector, off;
    if (!scan_end(f, (unsigned)best, h[best].seq, &sector, &off)) return false;
    f->seq = h[best].seq; f->slot = (uint8_t)best; f->sector = (uint16_t)sector; f->off = (uint16_t)off;
    f->start_epoch = h[best].epoch;
    return true;
}

size_t flog_append(flog_t *f, const uint8_t *data, size_t n, uint32_t epoch_now)
{
    size_t done = 0;
    uint8_t tmp[256];
    while (done < n) {
        if (f->off >= FLOG_SECTOR) {
            if (f->sector + 1u >= FLOG_SECTORS) {
                if (!new_file(f, (uint16_t)((f->seq + 1u) % FLOG_SEQ_MOD), epoch_now)) return done;
                continue;
            }
            const unsigned k = (unsigned)f->sector + 1u;
            const uint32_t a = slot_addr(f, f->slot) + k * FLOG_SECTOR;
            uint8_t tag[4];
            make_tag(tag, f->seq);
            if (!f->io.erase(a, f->io.user) || !f->io.write(a, tag, 4, f->io.user)) return done;
            f->sector = (uint16_t)k; f->off = FLOG_TAG_BYTES;
            continue;
        }
        size_t k = n - done;
        if (k > FLOG_SECTOR - f->off) k = FLOG_SECTOR - f->off;
        const size_t to_page = 256u - (f->off & 255u);
        if (k > to_page) k = to_page;
        for (size_t i = 0; i < k; i++) tmp[i] = data[done + i] == 0xFF ? '?' : data[done + i];
        const uint32_t a = slot_addr(f, f->slot) + (uint32_t)f->sector * FLOG_SECTOR + f->off;
        if (!f->io.write(a, tmp, k, f->io.user)) return done;
        f->off = (uint16_t)(f->off + k);
        done += k;
    }
    return done;
}

bool flog_set_epoch_if_unknown(flog_t *f, uint32_t epoch)
{
    if (!epoch || epoch == FLOG_EPOCH_UNKNOWN || f->start_epoch != FLOG_EPOCH_UNKNOWN) return true;
    const uint8_t e[4] = { (uint8_t)epoch, (uint8_t)(epoch >> 8), (uint8_t)(epoch >> 16), (uint8_t)(epoch >> 24) };
    if (!f->io.write(slot_addr(f, f->slot) + 8, e, 4, f->io.user)) return false;
    f->start_epoch = epoch;
    return true;
}

int flog_list(flog_t *f, flog_file_t out[FLOG_SLOTS])
{
    flog_file_t all[FLOG_SLOTS];
    int n = 0;
    for (unsigned s = 0; s < FLOG_SLOTS; s++) {
        hdr_t h;
        if (!read_hdr(f, s, &h) || !h.ok) continue;
        flog_file_t *o = &all[n];
        o->seq = h.seq; o->slot = (uint8_t)s;
        o->start_epoch = h.epoch == FLOG_EPOCH_UNKNOWN ? 0 : h.epoch;
        if (s == f->slot && h.seq == f->seq) o->size = size_at(f->sector, f->off);
        else {
            unsigned sector, off;
            if (!scan_end(f, s, h.seq, &sector, &off)) continue;
            o->size = size_at(sector, off);
        }
        n++;
    }
    /* от старого к новому: возраст относительно самого нового (в кольце 0..9999) */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            const unsigned ai = (f->seq + FLOG_SEQ_MOD - all[i].seq) % FLOG_SEQ_MOD;
            const unsigned aj = (f->seq + FLOG_SEQ_MOD - all[j].seq) % FLOG_SEQ_MOD;
            if (aj > ai) { flog_file_t t = all[i]; all[i] = all[j]; all[j] = t; }
        }
    for (int i = 0; i < n; i++) out[i] = all[i];
    return n;
}

bool flog_read(flog_t *f, const flog_file_t *file, uint32_t off, uint8_t *dst, size_t n)
{
    const uint32_t first = FLOG_SECTOR - FLOG_HDR_BYTES, per = FLOG_SECTOR - FLOG_TAG_BYTES;
    while (n) {
        uint32_t phys, room;
        if (off < first) { phys = FLOG_HDR_BYTES + off; room = first - off; }
        else {
            const uint32_t o = off - first, k = 1u + o / per, in = o % per;
            phys = k * FLOG_SECTOR + FLOG_TAG_BYTES + in; room = per - in;
        }
        const size_t c = n < room ? n : room;
        if (!f->io.read(slot_addr(f, file->slot) + phys, dst, c, f->io.user)) return false;
        dst += c; off += (uint32_t)c; n -= c;
    }
    return true;
}

bool flog_still_valid(flog_t *f, const flog_file_t *file)
{
    hdr_t h;
    return read_hdr(f, file->slot, &h) && h.ok && h.seq == file->seq;
}

/* ---------------- tar (ustar) ---------------- */
static void put_oct(uint8_t *p, size_t len, uint32_t v)       /* len-1 цифр и NUL */
{
    for (size_t i = len - 1; i-- > 0;) { p[i] = (uint8_t)('0' + (v & 7u)); v >>= 3; }
    p[len - 1] = 0;
}

static void tar_header(const flog_file_t *fl, uint8_t h[512])
{
    memset(h, 0, 512);
    h[0] = 'h'; h[1] = 'u'; h[2] = 'd'; h[3] = '.'; h[4] = 'l'; h[5] = 'o'; h[6] = 'g'; h[7] = '.';
    for (int i = 0, d = 1000; i < 4; i++, d /= 10) h[8 + i] = (uint8_t)('0' + (fl->seq / (unsigned)d) % 10u);
    put_oct(h + 100, 8, 0644); put_oct(h + 108, 8, 0); put_oct(h + 116, 8, 0);
    put_oct(h + 124, 12, fl->size);
    put_oct(h + 136, 12, fl->start_epoch);
    memset(h + 148, ' ', 8);
    h[156] = '0';
    memcpy(h + 257, "ustar", 6);          /* "ustar\0" */
    h[263] = '0'; h[264] = '0';
    uint32_t sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    for (int i = 5; i >= 0; i--) { h[148 + i] = (uint8_t)('0' + (sum & 7u)); sum >>= 3; }
    h[154] = 0; h[155] = ' ';
}

static uint32_t pad512(uint32_t n) { return (512u - (n & 511u)) & 511u; }

void flog_tar_begin(flog_t *f, flog_tar_t *t)
{
    memset(t, 0, sizeof *t);
    t->n = flog_list(f, t->files);
    uint32_t total = 0;
    for (int i = 0; i < t->n; i++) total += 512u + t->files[i].size + pad512(t->files[i].size);
    t->total = t->n ? total + 1024u : 0;
}

uint32_t flog_tar_total(const flog_tar_t *t) { return t->total; }

size_t flog_tar_read(flog_t *f, flog_tar_t *t, uint8_t *dst, size_t n)
{
    size_t out = 0;
    while (out < n) {
        if (t->idx >= t->n) {
            if (t->phase != 3) { t->phase = 3; t->off = 0; }
            if (t->off >= 1024u) break;
            size_t k = n - out; if (k > 1024u - t->off) k = 1024u - t->off;
            memset(dst + out, 0, k); t->off += (uint32_t)k; out += k;
            continue;
        }
        const flog_file_t *fl = &t->files[t->idx];
        if (t->phase == 0) {
            if (t->off == 0) tar_header(fl, t->hdr);
            size_t k = n - out; if (k > 512u - t->off) k = 512u - t->off;
            memcpy(dst + out, t->hdr + t->off, k); t->off += (uint32_t)k; out += k;
            if (t->off >= 512u) { t->phase = 1; t->off = 0; }
        } else if (t->phase == 1) {
            if (t->off >= fl->size) { t->phase = 2; t->off = 0; continue; }
            size_t k = n - out; if (k > fl->size - t->off) k = fl->size - t->off;
            if (t->err || !flog_still_valid(f, fl) || !flog_read(f, fl, t->off, dst + out, k)) { t->err = true; memset(dst + out, 0, k); }
            t->off += (uint32_t)k; out += k;
        } else {
            const uint32_t pad = pad512(fl->size);
            if (t->off >= pad) { t->idx++; t->phase = 0; t->off = 0; continue; }
            size_t k = n - out; if (k > pad - t->off) k = pad - t->off;
            memset(dst + out, 0, k); t->off += (uint32_t)k; out += k;
        }
    }
    return out;
}
