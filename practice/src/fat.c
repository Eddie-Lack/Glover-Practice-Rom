/* A small FAT16 and FAT32 file system (fat.h). It uses no game code. The host compiler builds it for tests/fathost.c. */
#include "fat.h"

#define NOSECTOR 0xFFFFFFFFu
#define ENTRY_SIZE 32
/* The date and time of every entry: noon on 1 January 2025. The console has no clock. */
#define STAMP_TIME 0x6000
#define STAMP_DATE 0x5A21

static u32 Rd16(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8;
}

static u32 Rd32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static void Wr16(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void Wr32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static void Zero(u8 *p, u32 n) {
    while (n-- > 0) {
        *p++ = 0;
    }
}

/* ---- The device and the two caches ---------------------------------------------------------------------------- */

static int DevRead(Fat *f, u32 lba, u32 count, u8 *buf) {
    return f->dev.read(f->dev.ctx, lba, count, buf) == 0 ? FAT_OK : FAT_ERR_IO;
}

static int DevWrite(Fat *f, u32 lba, u32 count, const u8 *buf) {
    return f->dev.write(f->dev.ctx, lba, count, buf) == 0 ? FAT_OK : FAT_ERR_IO;
}

/* The one-sector cache for directories and boot blocks. A change is written at once. */
static int LoadData(Fat *f, u32 lba) {
    int r;

    if (f->dataSector == lba) {
        return FAT_OK;
    }
    f->dataSector = NOSECTOR;
    r = DevRead(f, lba, 1, f->dataBuf);
    if (r == FAT_OK) {
        f->dataSector = lba;
    }
    return r;
}

static int StoreData(Fat *f) {
    int r = DevWrite(f, f->dataSector, 1, f->dataBuf);

    if (r != FAT_OK) {
        f->dataSector = NOSECTOR;
    }
    return r;
}

/* The FAT is cached FAT_CACHE_SECTORS sectors at a time. A change is written back, to every copy, by FatFlush. */
static int FatFlush(Fat *f) {
    u32 n;

    if (!f->fatDirty) {
        return FAT_OK;
    }
    n = f->fatSectors - f->fatGroup;
    if (n > FAT_CACHE_SECTORS) {
        n = FAT_CACHE_SECTORS;
    }
    for (u32 k = 0; k < f->fatWrites; k++) {
        int r = DevWrite(f, f->fatStart + k * f->fatSectors + f->fatGroup, n, f->fatBuf);

        if (r != FAT_OK) {
            return r;
        }
    }
    f->fatDirty = 0;
    return FAT_OK;
}

static int FatLoad(Fat *f, u32 index) {
    u32 group = index / FAT_CACHE_SECTORS * FAT_CACHE_SECTORS;
    u32 n;
    int r;

    if (f->fatGroup == group) {
        return FAT_OK;
    }
    r = FatFlush(f);
    if (r != FAT_OK) {
        return r;
    }
    f->fatGroup = NOSECTOR;
    n = f->fatSectors - group;
    if (n > FAT_CACHE_SECTORS) {
        n = FAT_CACHE_SECTORS;
    }
    r = DevRead(f, f->fatStart + group, n, f->fatBuf);
    if (r == FAT_OK) {
        f->fatGroup = group;
    }
    return r;
}

static u8 *FatEntryPtr(Fat *f, u32 cluster) {
    u32 off = f->type == 32 ? cluster * 4 : cluster * 2;

    return f->fatBuf + (off / FAT_SECTOR - f->fatGroup) * FAT_SECTOR + off % FAT_SECTOR;
}

static int FatGet(Fat *f, u32 cluster, u32 *value) {
    int r = FatLoad(f, (f->type == 32 ? cluster * 4 : cluster * 2) / FAT_SECTOR);

    if (r != FAT_OK) {
        return r;
    }
    *value = f->type == 32 ? Rd32(FatEntryPtr(f, cluster)) & 0x0FFFFFFF : Rd16(FatEntryPtr(f, cluster));
    return FAT_OK;
}

static int FatSet(Fat *f, u32 cluster, u32 value) {
    int r = FatLoad(f, (f->type == 32 ? cluster * 4 : cluster * 2) / FAT_SECTOR);
    u8 *p;

    if (r != FAT_OK) {
        return r;
    }
    p = FatEntryPtr(f, cluster);
    if (f->type == 32) {
        Wr32(p, (Rd32(p) & 0xF0000000) | (value & 0x0FFFFFFF));
    } else {
        Wr16(p, value);
    }
    f->fatDirty = 1;
    return FAT_OK;
}

static int IsEnd(const Fat *f, u32 value) {
    return value >= (f->type == 32 ? 0x0FFFFFF8u : 0xFFF8u);
}

static u32 EndMark(const Fat *f) {
    return f->type == 32 ? 0x0FFFFFFF : 0xFFFF;
}

static int ValidCluster(const Fat *f, u32 c) {
    return c >= 2 && c <= f->clusters + 1;
}

static u32 ClusterLba(const Fat *f, u32 c) {
    return f->dataStart + (c - 2) * f->spc;
}

/* The cluster after C: 0 at the end of the chain. */
static int NextCluster(Fat *f, u32 c, u32 *next) {
    u32 v;
    int r = FatGet(f, c, &v);

    if (r != FAT_OK) {
        return r;
    }
    if (IsEnd(f, v)) {
        *next = 0;
        return FAT_OK;
    }
    if (!ValidCluster(f, v)) {
        return FAT_ERR_BAD;
    }
    *next = v;
    return FAT_OK;
}

/* Takes a free cluster and marks it as the end of a chain. */
static int AllocCluster(Fat *f, u32 *out) {
    u32 c = f->allocHint;

    if (!ValidCluster(f, c)) {
        c = 2;
    }
    for (u32 tried = 0; tried < f->clusters; tried++) {
        u32 v;
        int r = FatGet(f, c, &v);

        if (r != FAT_OK) {
            return r;
        }
        if (v == 0) {
            r = FatSet(f, c, EndMark(f));
            *out = c;
            f->allocHint = c + 1;
            return r;
        }
        c = c + 1 > f->clusters + 1 ? 2 : c + 1;
    }
    return FAT_ERR_FULL;
}

static int FreeChain(Fat *f, u32 c) {
    u32 first = c;

    for (u32 n = 0; ValidCluster(f, c) && n <= f->clusters; n++) {
        u32 next;
        int r = FatGet(f, c, &next);

        if (r != FAT_OK) {
            return r;
        }
        r = FatSet(f, c, 0);
        if (r != FAT_OK) {
            return r;
        }
        if (IsEnd(f, next)) {
            if (first < f->allocHint) {
                f->allocHint = first;
            }
            return FAT_OK;
        }
        c = next;
    }
    return FAT_ERR_BAD;
}

/* Writes the FAT, and the FSInfo block of a FAT32 volume with the free count unknown. */
static int Sync(Fat *f) {
    int r = FatFlush(f);

    if (r == FAT_OK && f->fsInfo != 0) {
        r = LoadData(f, f->fsInfo);
        if (r == FAT_OK && Rd32(f->dataBuf) == 0x41615252 && Rd32(f->dataBuf + 484) == 0x61417272) {
            Wr32(f->dataBuf + 488, 0xFFFFFFFF);
            Wr32(f->dataBuf + 492, f->allocHint);
            r = StoreData(f);
        }
    }
    return r;
}

/* ---- Mounting ------------------------------------------------------------------------------------------------- */

/* 1 if the sector looks like the boot block of a FAT volume. */
static int LooksLikeBpb(const u8 *b) {
    u32 bps = Rd16(b + 11);
    u32 spc = b[13];

    return Rd16(b + 510) == 0xAA55 && (b[0] == 0xEB || b[0] == 0xE9) && (bps == 512 || bps == 1024 || bps == 2048 || bps == 4096) &&
           spc != 0 && (spc & (spc - 1)) == 0 && Rd16(b + 14) != 0 && (b[16] == 1 || b[16] == 2);
}

static int MountVolume(Fat *f, u32 volume) {
    const u8 *b;
    u32 reserved, total, fat16, fat32, dataSectors;
    int r = LoadData(f, volume);

    if (r != FAT_OK) {
        return r;
    }
    b = f->dataBuf;
    if (!LooksLikeBpb(b)) {
        return FAT_ERR_NOFS;
    }
    if (Rd16(b + 11) != FAT_SECTOR) {
        return FAT_ERR_UNSUPPORTED;
    }
    f->volume = volume;
    f->spc = b[13];
    f->numFats = b[16];
    reserved = Rd16(b + 14);
    f->rootEntries = (u16)Rd16(b + 17);
    total = Rd16(b + 19) != 0 ? Rd16(b + 19) : Rd32(b + 32);
    fat16 = Rd16(b + 22);
    fat32 = Rd32(b + 36);
    f->fatSectors = fat16 != 0 ? fat16 : fat32;
    f->fatStart = volume + reserved;
    f->fatWrites = f->numFats;
    f->rootStart = f->fatStart + f->numFats * f->fatSectors;
    f->rootSectors = ((u32)f->rootEntries * ENTRY_SIZE + FAT_SECTOR - 1) / FAT_SECTOR;
    f->dataStart = f->rootStart + f->rootSectors;
    if (f->fatSectors == 0 || total < f->dataStart - volume) {
        return FAT_ERR_NOFS;
    }
    dataSectors = total - (f->dataStart - volume);
    f->clusters = dataSectors / f->spc;
    if (f->clusters < 4085) {
        return FAT_ERR_UNSUPPORTED;
    }
    f->type = f->clusters < 65525 ? 16 : 32;
    f->fsInfo = 0;
    f->rootCluster = 0;
    if (f->type == 32) {
        u32 info = Rd16(b + 48);

        if (fat16 != 0 || f->rootEntries != 0) {
            return FAT_ERR_NOFS;
        }
        f->rootCluster = Rd32(b + 44);
        /* ExtFlags: with bit 7 set the FAT is not mirrored, and only the FAT that bits 0 to 3 name is in use. */
        if (Rd16(b + 40) & 0x80) {
            u32 active = Rd16(b + 40) & 0x0F;

            if (active >= f->numFats) {
                return FAT_ERR_NOFS;
            }
            f->fatStart += active * f->fatSectors;
            f->fatWrites = 1;
        }
        if (info != 0 && info != 0xFFFF) {
            f->fsInfo = volume + info;
        }
    } else if (fat16 == 0 || f->rootEntries == 0) {
        return FAT_ERR_NOFS;
    }
    if ((f->clusters + 2) > f->fatSectors * (f->type == 32 ? 128u : 256u) || (f->type == 32 && !ValidCluster(f, f->rootCluster))) {
        return FAT_ERR_NOFS;
    }
    f->allocHint = 2;
    if (f->fsInfo != 0) {
        r = LoadData(f, f->fsInfo);
        if (r != FAT_OK) {
            return r;
        }
        if (Rd32(f->dataBuf) == 0x41615252 && Rd32(f->dataBuf + 484) == 0x61417272 && ValidCluster(f, Rd32(f->dataBuf + 492))) {
            f->allocHint = Rd32(f->dataBuf + 492);
        }
    }
    return FAT_OK;
}

int FatMount(Fat *f, const FatDev *dev, u8 *scratch, u32 scratch_sectors) {
    u8 parts[64];
    int r;

    f->dev = *dev;
    f->scratch = scratch;
    f->scratchSectors = scratch_sectors;
    f->fatGroup = NOSECTOR;
    f->dataSector = NOSECTOR;
    f->fatDirty = 0;
    r = LoadData(f, 0);
    if (r != FAT_OK) {
        return r;
    }
    if (LooksLikeBpb(f->dataBuf)) {
        return MountVolume(f, 0);
    }
    if (Rd16(f->dataBuf + 510) != 0xAA55) {
        return FAT_ERR_NOFS;
    }
    for (int i = 0; i < 64; i++) {
        parts[i] = f->dataBuf[446 + i];
    }
    r = FAT_ERR_NOFS;
    for (int i = 0; i < 4; i++) {
        const u8 *p = parts + 16 * i;
        u32 type = p[4];
        u32 start = Rd32(p + 8);

        if (start == 0 || !(type == 0x04 || type == 0x06 || type == 0x0B || type == 0x0C || type == 0x0E)) {
            continue;
        }
        r = MountVolume(f, start);
        if (r != FAT_ERR_NOFS) {
            break;
        }
    }
    return r;
}

/* ---- Names and directories ------------------------------------------------------------------------------------ */

static int NameChar(int c) {
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return 1;
    }
    for (const char *s = "!#$%&'()-@^_`{}~"; *s != '\0'; s++) {
        if (*s == c) {
            return 1;
        }
    }
    return 0;
}

int FatName(u8 *out, const char *name) {
    int n = 0;

    for (int i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    for (; *name != '\0' && *name != '.'; name++) {
        int c = *name >= 'a' && *name <= 'z' ? *name - 32 : *name;

        if (n == 8 || !NameChar(c)) {
            return FAT_ERR_NAME;
        }
        out[n++] = (u8)c;
    }
    if (n == 0) {
        return FAT_ERR_NAME;
    }
    if (*name == '.') {
        n = 8;
        for (name++; *name != '\0'; name++) {
            int c = *name >= 'a' && *name <= 'z' ? *name - 32 : *name;

            if (n == 11 || !NameChar(c)) {
                return FAT_ERR_NAME;
            }
            out[n++] = (u8)c;
        }
    }
    return FAT_OK;
}

static int SameName(const u8 *a, const u8 *b) {
    for (int i = 0; i < 11; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* A place in a directory: a sector, and the cluster it is in (0 in the fixed root directory of a FAT16 volume). */
typedef struct Dir {
    u32 cluster;
    u32 k;    /* the sector in the cluster; f->spc after the last one */
    u32 lba;
    u32 left; /* fixed root: the sectors after this one */
    u32 steps; /* the clusters passed: a chain that loops is damaged */
} Dir;

static int DirOpen(Fat *f, u32 dir, Dir *d) {
    if (dir == f->rootCluster && f->type == 32) {
        dir = 0;
    }
    d->k = 0;
    d->steps = 0;
    if (dir == 0 && f->type == 16) {
        d->cluster = 0;
        d->lba = f->rootStart;
        d->left = f->rootSectors - 1;
        return FAT_OK;
    }
    if (dir == 0) {
        dir = f->rootCluster;
    }
    if (!ValidCluster(f, dir)) {
        return FAT_ERR_BAD;
    }
    d->cluster = dir;
    d->lba = ClusterLba(f, dir);
    return FAT_OK;
}

/* Goes to the next sector of the directory: FAT_OK, 1 at its end (D then stays at the last cluster), or an error. */
static int DirNext(Fat *f, Dir *d) {
    if (d->cluster == 0) {
        if (d->left == 0) {
            return 1;
        }
        d->left--;
        d->lba++;
        return FAT_OK;
    }
    if (d->k + 1 == f->spc) {
        u32 next;
        int r = NextCluster(f, d->cluster, &next);

        if (r != FAT_OK) {
            return r;
        }
        if (next == 0) {
            return 1;
        }
        if (++d->steps > f->clusters) {
            return FAT_ERR_BAD;
        }
        d->cluster = next;
        d->k = 0;
    } else {
        d->k++;
    }
    d->lba = ClusterLba(f, d->cluster) + d->k;
    return FAT_OK;
}

static void Decode(const u8 *e, u32 sector, u32 offset, int fat32, FatEntry *out) {
    for (int i = 0; i < 11; i++) {
        out->name[i] = e[i];
    }
    out->attr = e[11];
    out->cluster = Rd16(e + 26) | (fat32 ? Rd16(e + 20) << 16 : 0);
    out->size = Rd32(e + 28);
    out->sector = sector;
    out->offset = (u16)offset;
}

/* An entry that names a file or a folder: not free, not part of a long name, not the volume label. */
static int InUse(const u8 *e) {
    return e[0] != 0xE5 && (e[11] & 0x0F) != 0x0F && !(e[11] & 0x08);
}

/* Looks for NAME in DIR. If it is there, fills FOUND and returns FAT_OK. Otherwise returns FAT_ERR_NOTFOUND, with
 * SLOT (if not NULL) holding the first free entry (sector 0 if the directory has none) and END the last sector. */
static int Locate(Fat *f, u32 dir, const u8 *name, FatEntry *found, FatEntry *slot, Dir *end) {
    Dir d;
    int r = DirOpen(f, dir, &d);

    if (slot != NULL) {
        slot->sector = 0;
    }
    if (r != FAT_OK) {
        return r;
    }
    for (;;) {
        r = LoadData(f, d.lba);
        if (r != FAT_OK) {
            return r;
        }
        for (u32 off = 0; off < FAT_SECTOR; off += ENTRY_SIZE) {
            const u8 *e = f->dataBuf + off;

            if (e[0] == 0 || e[0] == 0xE5) {
                if (slot != NULL && slot->sector == 0) {
                    slot->sector = d.lba;
                    slot->offset = (u16)off;
                }
                if (e[0] == 0) {
                    *end = d;
                    return FAT_ERR_NOTFOUND;
                }
                continue;
            }
            if (InUse(e) && SameName(e, name)) {
                Decode(e, d.lba, off, f->type == 32, found);
                return FAT_OK;
            }
        }
        r = DirNext(f, &d);
        if (r < 0) {
            return r;
        }
        if (r == 1) {
            *end = d;
            return FAT_ERR_NOTFOUND;
        }
    }
}

int FatFind(Fat *f, u32 dir, const char *name, FatEntry *e) {
    u8 n[11];
    Dir end;
    int r = FatName(n, name);

    return r != FAT_OK ? r : Locate(f, dir, n, e, NULL, &end);
}

int FatList(Fat *f, u32 dir, FatListFn fn, void *ctx) {
    Dir d;
    int r = DirOpen(f, dir, &d);

    if (r != FAT_OK) {
        return r;
    }
    for (;;) {
        for (u32 off = 0; off < FAT_SECTOR; off += ENTRY_SIZE) {
            FatEntry e;

            r = LoadData(f, d.lba);
            if (r != FAT_OK) {
                return r;
            }
            if (f->dataBuf[off] == 0) {
                return FAT_OK;
            }
            if (!InUse(f->dataBuf + off) || f->dataBuf[off] == '.') {
                continue;
            }
            Decode(f->dataBuf + off, d.lba, off, f->type == 32, &e);
            r = fn(ctx, &e);
            if (r != 0) {
                return r;
            }
        }
        r = DirNext(f, &d);
        if (r < 0) {
            return r;
        }
        if (r == 1) {
            return FAT_OK;
        }
    }
}

/* Builds the entry E in the sector cache and writes it. */
static int PutEntry(Fat *f, const FatEntry *e) {
    u8 *p;
    int r = LoadData(f, e->sector);

    if (r != FAT_OK) {
        return r;
    }
    p = f->dataBuf + e->offset;
    for (int i = 0; i < 11; i++) {
        p[i] = e->name[i];
    }
    p[11] = e->attr;
    p[12] = 0;
    p[13] = 0;
    Wr16(p + 14, STAMP_TIME);
    Wr16(p + 16, STAMP_DATE);
    Wr16(p + 18, STAMP_DATE);
    Wr16(p + 20, f->type == 32 ? e->cluster >> 16 : 0);
    Wr16(p + 22, STAMP_TIME);
    Wr16(p + 24, STAMP_DATE);
    Wr16(p + 26, e->cluster);
    Wr32(p + 28, e->size);
    return StoreData(f);
}

/* Writes zeros over all sectors of a cluster, in as few commands as the scratch memory allows. */
static int ZeroCluster(Fat *f, u32 c) {
    u8 *buf = f->scratch != NULL && f->scratchSectors > 0 ? f->scratch : f->dataBuf;
    u32 cap = buf == f->dataBuf ? 1 : f->scratchSectors;
    u32 n = f->spc < cap ? f->spc : cap;

    f->dataSector = NOSECTOR;
    Zero(buf, n * FAT_SECTOR);
    for (u32 k = 0; k < f->spc; k += n) {
        u32 m = f->spc - k < n ? f->spc - k : n;
        int r = DevWrite(f, ClusterLba(f, c) + k, m, buf);

        if (r != FAT_OK) {
            return r;
        }
    }
    return FAT_OK;
}

/* Gives back a cluster that was taken and is not in use (after an error). */
static void Release(Fat *f, u32 c) {
    if (FatSet(f, c, 0) == FAT_OK && c < f->allocHint) {
        f->allocHint = c;
    }
}

/* Makes a new entry in DIR at SLOT, or at the start of a new cluster of the directory when SLOT is empty. */
static int AddEntry(Fat *f, FatEntry *slot, const Dir *end, const u8 *name, u8 attr, u32 cluster, u32 size) {
    FatEntry e;

    if (slot->sector == 0) {
        u32 c;
        int r;

        if (end->cluster == 0) {
            return FAT_ERR_DIRFULL;
        }
        r = AllocCluster(f, &c);
        if (r != FAT_OK) {
            return r;
        }
        r = ZeroCluster(f, c);
        if (r == FAT_OK) {
            r = FatSet(f, end->cluster, c);
        }
        if (r != FAT_OK) {
            Release(f, c);
            return r;
        }
        slot->sector = ClusterLba(f, c);
        slot->offset = 0;
    }
    for (int i = 0; i < 11; i++) {
        e.name[i] = name[i];
    }
    e.attr = attr;
    e.cluster = cluster;
    e.size = size;
    e.sector = slot->sector;
    e.offset = slot->offset;
    return PutEntry(f, &e);
}

int FatFolder(Fat *f, u32 parent, const char *name, int make, u32 *cluster) {
    u8 n[11];
    FatEntry e, slot;
    Dir end;
    u32 c;
    int r = FatName(n, name);

    if (r != FAT_OK) {
        return r;
    }
    r = Locate(f, parent, n, &e, &slot, &end);
    if (r == FAT_OK) {
        if (!(e.attr & FAT_ATTR_DIR)) {
            return FAT_ERR_EXISTS;
        }
        /* A folder that starts at cluster 0 is damaged. Only the ".." of a folder in the root directory has 0. */
        if (!ValidCluster(f, e.cluster)) {
            return FAT_ERR_BAD;
        }
        *cluster = e.cluster;
        return FAT_OK;
    }
    if (r != FAT_ERR_NOTFOUND || !make) {
        return r;
    }
    if (parent == f->rootCluster && f->type == 32) {
        parent = 0;
    }
    r = AllocCluster(f, &c);
    if (r != FAT_OK) {
        return r;
    }
    r = ZeroCluster(f, c);
    if (r == FAT_OK) {
        /* The first two entries of a folder: "." (itself) and ".." (its parent, 0 for the root directory). */
        FatEntry dot;

        for (int i = 0; i < 11; i++) {
            dot.name[i] = ' ';
        }
        dot.name[0] = '.';
        dot.attr = FAT_ATTR_DIR;
        dot.cluster = c;
        dot.size = 0;
        dot.sector = ClusterLba(f, c);
        dot.offset = 0;
        r = PutEntry(f, &dot);
        if (r == FAT_OK) {
            dot.name[1] = '.';
            dot.cluster = parent;
            dot.offset = ENTRY_SIZE;
            r = PutEntry(f, &dot);
        }
    }
    if (r == FAT_OK) {
        r = FatFlush(f);
    }
    if (r == FAT_OK) {
        r = AddEntry(f, &slot, &end, n, FAT_ATTR_DIR, c, 0);
    }
    if (r != FAT_OK) {
        /* The entry does not name the new folder. */
        Release(f, c);
        return r;
    }
    *cluster = c;
    return Sync(f);
}

/* ---- Files ---------------------------------------------------------------------------------------------------- */

void FatWriteBegin(Fat *f, FatWriter *w) {
    (void)f;
    w->first = 0;
    w->last = 0;
    w->lba = 0;
    w->left = 0;
}

int FatWrite(Fat *f, FatWriter *w, const u8 *data, u32 sectors) {
    while (sectors > 0) {
        u32 n;
        int r;

        if (w->left == 0) {
            /* A new run: a cluster, and the clusters right behind it that are free, as many as the data needs. */
            u32 c, need = (sectors + f->spc - 1) / f->spc;

            r = AllocCluster(f, &c);
            if (r != FAT_OK) {
                return r;
            }
            if (w->last != 0) {
                r = FatSet(f, w->last, c);
                if (r != FAT_OK) {
                    Release(f, c);
                    return r;
                }
            } else {
                w->first = c;
            }
            w->lba = ClusterLba(f, c);
            w->left = f->spc;
            while (need > 1 && c + 1 <= f->clusters + 1) {
                u32 v;

                r = FatGet(f, c + 1, &v);
                if (r != FAT_OK) {
                    return r;
                }
                if (v != 0) {
                    break;
                }
                if (FatSet(f, c, c + 1) != FAT_OK || FatSet(f, c + 1, EndMark(f)) != FAT_OK) {
                    return FAT_ERR_IO;
                }
                c++;
                need--;
                w->left += f->spc;
                f->allocHint = c + 1;
            }
            w->last = c;
        }
        n = sectors < w->left ? sectors : w->left;
        r = DevWrite(f, w->lba, n, data);
        if (r != FAT_OK) {
            return r;
        }
        w->lba += n;
        w->left -= n;
        data += n * FAT_SECTOR;
        sectors -= n;
    }
    return FAT_OK;
}

void FatWriteAbort(Fat *f, FatWriter *w) {
    if (w->first != 0) {
        FreeChain(f, w->first);
        Sync(f);
    }
    w->first = 0;
}

int FatWriteEnd(Fat *f, FatWriter *w, u32 dir, const char *name, u32 size) {
    u8 n[11];
    FatEntry e, slot;
    Dir end;
    u32 old = 0;
    int r = FatName(n, name);

    if (r == FAT_OK) {
        r = FatFlush(f);
    }
    if (r == FAT_OK) {
        r = Locate(f, dir, n, &e, &slot, &end);
    }
    if (r == FAT_OK) {
        old = e.cluster;
        if (e.attr & FAT_ATTR_DIR) {
            r = FAT_ERR_EXISTS;
        } else {
            e.cluster = w->first;
            e.size = size;
            r = PutEntry(f, &e);
        }
    } else if (r == FAT_ERR_NOTFOUND) {
        r = AddEntry(f, &slot, &end, n, 0x20, w->first, size);
    }
    if (r != FAT_OK) {
        /* The entry does not name the new clusters. */
        FatWriteAbort(f, w);
        return r;
    }
    /* The entry names the new clusters from here on. An error below must never free them: at worst, clusters leak. */
    w->first = 0;
    if (old != 0) {
        r = FreeChain(f, old);
    }
    if (r == FAT_OK) {
        r = Sync(f);
    }
    return r;
}

int FatOpen(Fat *f, const FatEntry *e, FatReader *r) {
    r->cluster = e->cluster;
    r->inCluster = 0;
    return e->cluster == 0 || ValidCluster(f, e->cluster) ? FAT_OK : FAT_ERR_BAD;
}

int FatRead(Fat *f, FatReader *r, u8 *buf, u32 sectors) {
    while (sectors > 0) {
        u32 c, inC, n = 0, lba;
        int res;

        if (r->cluster == 0) {
            return FAT_ERR_BAD;
        }
        if (r->inCluster == f->spc) {
            u32 next;

            res = NextCluster(f, r->cluster, &next);
            if (res != FAT_OK) {
                return res;
            }
            if (next == 0) {
                return FAT_ERR_BAD;
            }
            r->cluster = next;
            r->inCluster = 0;
        }
        c = r->cluster;
        inC = r->inCluster;
        lba = ClusterLba(f, c) + inC;
        for (;;) {
            u32 take = f->spc - inC;
            u32 next;

            if (take > sectors - n) {
                take = sectors - n;
            }
            n += take;
            inC += take;
            if (n == sectors || inC < f->spc) {
                break;
            }
            /* A run goes on into the next cluster when that one lies right behind. */
            res = NextCluster(f, c, &next);
            if (res != FAT_OK) {
                return res;
            }
            if (next != c + 1) {
                break;
            }
            c = next;
            inC = 0;
        }
        r->cluster = c;
        r->inCluster = inC;
        res = DevRead(f, lba, n, buf);
        if (res != FAT_OK) {
            return res;
        }
        buf += n * FAT_SECTOR;
        sectors -= n;
    }
    return FAT_OK;
}

int FatDelete(Fat *f, u32 dir, const char *name) {
    u8 n[11];
    FatEntry e;
    Dir end;
    int r = FatName(n, name);

    if (r == FAT_OK) {
        r = Locate(f, dir, n, &e, NULL, &end);
    }
    if (r == FAT_OK && (e.attr & FAT_ATTR_DIR)) {
        r = FAT_ERR_EXISTS;
    }
    if (r == FAT_OK) {
        r = LoadData(f, e.sector);
    }
    if (r == FAT_OK) {
        f->dataBuf[e.offset] = 0xE5;
        r = StoreData(f);
    }
    if (r == FAT_OK && e.cluster != 0) {
        r = FreeChain(f, e.cluster);
    }
    if (r == FAT_OK) {
        r = Sync(f);
    }
    return r;
}
