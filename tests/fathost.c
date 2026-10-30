/* A host test of the FAT code (practice/src/fat.c): tests/fathost.py makes a disk image and runs this on it, then
 * reads the result with its own reader and with fsck.fat if that is installed.
 *
 *   fathost SCENARIO IMAGE [K]
 *
 * The scenarios:
 *   basic      a folder, 100 files of many sizes, replaced files, deleted files, a file made again
 *   full       files until the disk is full; a replacement that does not fit leaves the old file whole
 *   fragment   a disk whose free clusters are scattered; a large file over many pieces; a file read back
 *   powerfail  the device stops at each write of a replacement and of a new file: the old file, or the new one,
 *              is whole after every stop
 *   dirfull    the root directory has no room for the folder, and nothing is left behind
 *   badfolder  a folder entry with cluster 0 is refused
 *   loopdir    a folder whose clusters loop is called damaged
 *   firstsave  the first save to a card (folder and one file) and the commands it took
 *   transient  only write number K fails (fathost.py tries every K): the old file or the new one is whole, and
 *              nothing else changed
 * The image is held in memory and written back at the end. A scenario prints "FILE name seed size" for every file
 * that the disk must hold at the end. The Python reader checks them. It reads no game data. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fat.h"

static unsigned char *image;
static u32 imageSectors;
static long writes, failAt, failOnly;
/* What the device was asked for: sectors, and commands (a call of up to 16 sectors is one SummerCart64 command). */
static unsigned long readSectors, writeSectors, readCommands, writeCommands;

static int ImgRead(void *ctx, u32 lba, u32 n, u8 *buf) {
    (void)ctx;
    if ((unsigned long long)lba + n > imageSectors) {
        fprintf(stderr, "read outside the disk: %u + %u\n", lba, n);
        exit(2);
    }
    readSectors += n;
    readCommands += (n + 15) / 16;
    memcpy(buf, image + (size_t)lba * 512, (size_t)n * 512);
    return 0;
}

static int ImgWrite(void *ctx, u32 lba, u32 n, const u8 *buf) {
    (void)ctx;
    if ((unsigned long long)lba + n > imageSectors) {
        fprintf(stderr, "write outside the disk: %u + %u\n", lba, n);
        exit(2);
    }
    writes++;
    if (failAt != 0 && writes > failAt) {
        return 1;
    }
    if (failOnly != 0 && writes == failOnly) {
        return 1;
    }
    writeSectors += n;
    writeCommands += (n + 15) / 16;
    memcpy(image + (size_t)lba * 512, buf, (size_t)n * 512);
    return 0;
}

static const FatDev dev = { ImgRead, ImgWrite, NULL };
/* The memory that the file system may use to write a cluster of zeros: FATHOST_SCRATCH sectors (0 for none, 16 if not
 * set). */
static u8 scratchMemory[16 * 512] __attribute__((aligned(4)));
static u8 *scratch = scratchMemory;
static u32 scratchSectors = 16;
static Fat fat;

static void Die(const char *what, int code) {
    fprintf(stderr, "FAIL: %s (%d)\n", what, code);
    exit(1);
}

#define CHECK(call, what) \
    do { \
        int r_ = (call); \
        if (r_ != 0) Die(what, r_); \
    } while (0)

static unsigned long long lcg = 99;

static u32 Rnd(void) {
    lcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL;
    return (u32)(lcg >> 33);
}

/* The content of a file: byte I of the file with SEED. */
static u8 Pat(u32 seed, u32 i) {
    return (u8)(seed * 31u + i * 7u + (i >> 8) * 13u + (i >> 16) * 5u);
}

static void Name(char *out, int n) {
    sprintf(out, "STATE%03d.GST", n);
}

/* Writes a file in pieces of 1 to 40 sectors. Returns the error. */
static int WriteFile(u32 dir, const char *name, u32 seed, u32 size) {
    FatWriter w;
    static u8 buf[40 * 512];
    u32 sectors = (size + 511) / 512, done = 0;
    int r = 0;

    FatWriteBegin(&fat, &w);
    while (done < sectors) {
        u32 n = 1 + Rnd() % 40;

        if (n > sectors - done) {
            n = sectors - done;
        }
        for (u32 i = 0; i < n * 512; i++) {
            u32 at = done * 512 + i;

            buf[i] = at < size ? Pat(seed, at) : 0;
        }
        r = FatWrite(&fat, &w, buf, n);
        if (r != 0) {
            FatWriteAbort(&fat, &w);
            return r;
        }
        done += n;
    }
    return FatWriteEnd(&fat, &w, dir, name, size);
}

/* Reads a file back in pieces and compares. Returns 0 if it is there and right, FAT_ERR_NOTFOUND, or 1 for a
 * difference. */
static int ReadFile(u32 dir, const char *name, u32 seed, u32 size) {
    FatEntry e;
    FatReader rd;
    static u8 buf[33 * 512];
    u32 sectors, done = 0;
    int r = FatFind(&fat, dir, name, &e);

    if (r != 0) {
        return r;
    }
    if (e.size != size) {
        fprintf(stderr, "%s: size %u, wanted %u\n", name, e.size, size);
        return 1;
    }
    sectors = (size + 511) / 512;
    CHECK(FatOpen(&fat, &e, &rd), "open");
    while (done < sectors) {
        u32 n = 1 + Rnd() % 33;

        if (n > sectors - done) {
            n = sectors - done;
        }
        r = FatRead(&fat, &rd, buf, n);
        if (r != 0) {
            fprintf(stderr, "%s: read error %d\n", name, r);
            return 1;
        }
        for (u32 i = 0; i < n * 512 && done * 512 + i < size; i++) {
            if (buf[i] != Pat(seed, done * 512 + i)) {
                fprintf(stderr, "%s: byte %u differs\n", name, done * 512 + i);
                return 1;
            }
        }
        done += n;
    }
    return 0;
}

/* The files that the disk must hold at the end. */
typedef struct Want {
    char name[16];
    u32 seed, size;
    int present;
} Want;

static Want want[200];
static int wants;
static u32 folder;

static Want *Slot(const char *name) {
    for (int i = 0; i < wants; i++) {
        if (strcmp(want[i].name, name) == 0) {
            return &want[i];
        }
    }
    strcpy(want[wants].name, name);
    return &want[wants++];
}

static void Put(const char *name, u32 seed, u32 size) {
    Want *w = Slot(name);

    CHECK(WriteFile(folder, name, seed, size), name);
    w->seed = seed;
    w->size = size;
    w->present = 1;
}

static void Remove(const char *name) {
    CHECK(FatDelete(&fat, folder, name), name);
    Slot(name)->present = 0;
    if (FatFind(&fat, folder, name, &(FatEntry){ 0 }) != FAT_ERR_NOTFOUND) {
        Die("a deleted file is still found", 0);
    }
}

static int count;

static int CountFn(void *ctx, const FatEntry *e) {
    (void)ctx;
    (void)e;
    count++;
    return 0;
}

static void VerifyAll(void) {
    int present = 0;

    for (int i = 0; i < wants; i++) {
        if (!want[i].present) {
            continue;
        }
        present++;
        if (ReadFile(folder, want[i].name, want[i].seed, want[i].size) != 0) {
            Die(want[i].name, 0);
        }
    }
    count = 0;
    CHECK(FatList(&fat, folder, CountFn, NULL), "list");
    if (count != present) {
        fprintf(stderr, "the folder lists %d files, %d wanted\n", count, present);
        Die("list", 0);
    }
}

static void Report(void) {
    for (int i = 0; i < wants; i++) {
        if (want[i].present) {
            printf("FILE %s %u %u\n", want[i].name, want[i].seed, want[i].size);
        }
    }
}

static void Mount(void) {
    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
    CHECK(FatFolder(&fat, 0, "GLOVERPR", 1, &folder), "folder");
}

static void Basic(void) {
    char name[16];
    u32 again;

    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
    if (FatFolder(&fat, 0, "GLOVERPR", 0, &folder) != FAT_ERR_NOTFOUND) {
        Die("a folder that is not there was found", 0);
    }
    CHECK(FatFolder(&fat, 0, "GLOVERPR", 1, &folder), "folder");
    CHECK(FatFolder(&fat, 0, "gloverpr", 0, &again), "folder again");
    if (again != folder) {
        Die("the folder moved", 0);
    }
    for (int n = 1; n <= 100; n++) {
        u32 size = n == 13 ? 0 : (u32)((n * 7919) % 300000) + (n % 7 == 0 ? 600000 : 1);

        Name(name, n);
        Put(name, (u32)n, size);
    }
    VerifyAll();
    /* Larger, smaller and empty replacements. */
    Put("STATE005.GST", 205, 700001);
    Put("STATE050.GST", 250, 3);
    Put("STATE077.GST", 277, 0);
    Put("STATE013.GST", 213, 100000);
    for (int n = 10; n <= 100; n += 10) {
        Name(name, n);
        Remove(name);
    }
    VerifyAll();
    Put("STATE020.GST", 320, 123457);
    Put("STATE030.GST", 330, 512);
    /* A file in the root directory, beside the folder. */
    {
        FatWriter w;
        static u8 sector[512];

        FatWriteBegin(&fat, &w);
        memset(sector, 'x', sizeof(sector));
        CHECK(FatWrite(&fat, &w, sector, 1), "root write");
        CHECK(FatWriteEnd(&fat, &w, 0, "readme.txt", 300), "root end");
    }
    VerifyAll();
    /* A name that is a folder is not a file. */
    if (FatDelete(&fat, 0, "GLOVERPR") != FAT_ERR_EXISTS) {
        Die("a folder was deleted as a file", 0);
    }
    if (FatName((u8[11]){ 0 }, "toolongname.gst") != FAT_ERR_NAME) {
        Die("a long name was taken", 0);
    }
    Report();
    printf("ROOT readme.txt 300\n");
}

static void Full(void) {
    char name[16];
    int made = 0;
    int r;

    Mount();
    for (int n = 1;; n++) {
        Name(name, n);
        r = WriteFile(folder, name, (u32)n, 1000000);
        if (r != 0) {
            break;
        }
        Slot(name)->seed = (u32)n;
        Slot(name)->size = 1000000;
        Slot(name)->present = 1;
        made++;
        if (n >= 190) {
            Die("the disk did not fill", 0);
        }
    }
    if (r != FAT_ERR_FULL) {
        Die("the write stopped for another reason", r);
    }
    fprintf(stderr, "full after %d files of 1 MB\n", made);
    VerifyAll();
    /* A replacement that does not fit: the old file stays. */
    Name(name, 1);
    r = WriteFile(folder, name, 777, 90000000);
    if (r != FAT_ERR_FULL) {
        Die("the large replacement did not stop", r);
    }
    VerifyAll();
    /* A replacement needs room for the new file while the old one still stands. */
    Remove("STATE002.GST");
    Remove("STATE003.GST");
    Put("STATE001.GST", 901, 1500000);
    Put("STATE002.GST", 902, 400000);
    VerifyAll();
    Report();
}

static void Fragment(void) {
    char name[16];

    Mount();
    /* Small files side by side, then every other one deleted: the free clusters are scattered. */
    for (int n = 1; n <= 150; n++) {
        Name(name, n);
        Put(name, (u32)n, 1024 + (u32)n * 5);
    }
    for (int n = 1; n <= 150; n += 2) {
        Name(name, n);
        Remove(name);
    }
    for (int n = 1; n <= 150; n += 2) {
        Name(name, n);
        Put(name, 1000u + (u32)n, 1500 + (u32)n * 3);
    }
    for (int n = 2; n <= 150; n += 4) {
        Name(name, n);
        Remove(name);
    }
    /* One file over all the scattered clusters. */
    Put("BIG.BIN", 4242, 3000000);
    VerifyAll();
    Put("BIG.BIN", 4243, 150000);
    Put("STATE002.GST", 5002, 2200000);
    VerifyAll();
    Report();
}

/* Counts how many writes a call makes, with failAt set to 0. */
static long Writes(void) {
    return writes;
}

static void PowerFail(void) {
    unsigned char *snapshot;
    FatEntry e;
    long total;
    int stops = 0;

    Mount();
    Put("STATE001.GST", 1, 40 * 512 + 17);
    Put("STATE002.GST", 2, 100);
    snapshot = malloc((size_t)imageSectors * 512);
    memcpy(snapshot, image, (size_t)imageSectors * 512);
    /* How many writes a replacement makes. */
    failAt = 0;
    writes = 0;
    CHECK(WriteFile(folder, "STATE001.GST", 7, 70 * 512 + 3), "replacement");
    total = Writes();
    for (long k = 1; k <= total + 1; k++) {
        int r;

        memcpy(image, snapshot, (size_t)imageSectors * 512);
        memset(&fat, 0, sizeof(fat));
        CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
        CHECK(FatFolder(&fat, 0, "GLOVERPR", 0, &folder), "folder");
        writes = 0;
        failAt = k;
        r = WriteFile(folder, "STATE001.GST", 7, 70 * 512 + 3);
        failAt = 0;
        /* Whatever happened, a new mount sees the old file or the new one, whole. */
        memset(&fat, 0, sizeof(fat));
        CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount after a stop");
        CHECK(FatFolder(&fat, 0, "GLOVERPR", 0, &folder), "folder after a stop");
        if (r == 0) {
            if (ReadFile(folder, "STATE001.GST", 7, 70 * 512 + 3) != 0) {
                Die("the replacement that returned 0 is not whole", (int)k);
            }
        } else {
            stops++;
            if (ReadFile(folder, "STATE001.GST", 1, 40 * 512 + 17) != 0 &&
                ReadFile(folder, "STATE001.GST", 7, 70 * 512 + 3) != 0) {
                Die("after a stop the file is neither the old one nor the new one", (int)k);
            }
        }
        if (ReadFile(folder, "STATE002.GST", 2, 100) != 0) {
            Die("another file was damaged", (int)k);
        }
    }
    /* A new file: absent, or whole. */
    memcpy(image, snapshot, (size_t)imageSectors * 512);
    memset(&fat, 0, sizeof(fat));
    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
    CHECK(FatFolder(&fat, 0, "GLOVERPR", 0, &folder), "folder");
    writes = 0;
    CHECK(WriteFile(folder, "STATE003.GST", 3, 60 * 512), "new file");
    total = Writes();
    for (long k = 1; k <= total + 1; k++) {
        int r;

        memcpy(image, snapshot, (size_t)imageSectors * 512);
        memset(&fat, 0, sizeof(fat));
        CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
        CHECK(FatFolder(&fat, 0, "GLOVERPR", 0, &folder), "folder");
        writes = 0;
        failAt = k;
        r = WriteFile(folder, "STATE003.GST", 3, 60 * 512);
        failAt = 0;
        memset(&fat, 0, sizeof(fat));
        CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount after a stop");
        CHECK(FatFolder(&fat, 0, "GLOVERPR", 0, &folder), "folder after a stop");
        r = FatFind(&fat, folder, "STATE003.GST", &e) == 0 ? ReadFile(folder, "STATE003.GST", 3, 60 * 512) : 0;
        if (r != 0) {
            Die("a new file is there but not whole", (int)k);
        }
    }
    fprintf(stderr, "stopped %d times in a replacement of %ld writes\n", stops, total);
    /* Leave the image as the snapshot, with the first replacement done, for the reader. The stops above leave lost
     * clusters, as a power cut does. */
    memcpy(image, snapshot, (size_t)imageSectors * 512);
    free(snapshot);
    memset(&fat, 0, sizeof(fat));
    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
    CHECK(FatFolder(&fat, 0, "GLOVERPR", 1, &folder), "folder");
    Put("STATE001.GST", 7, 70 * 512 + 3);
    Put("STATE002.GST", 2, 100);
    Report();
}

/* One transient failure: write number K (from 1) fails and every other write works. The scenario does a replacement
 * and a new file on a disk that holds two files, and prints for each file what the disk may hold afterwards:
 * "EITHER name seed1 size1 seed2 size2" (the old file or the new one, whole), "NEW name seed size" (absent or
 * whole), "KEEP name seed size" (untouched). K = 0 fails nothing and prints "TOTAL n", the writes of the two steps. */
static void Transient(long k) {
    long total = 0;

    Mount();
    Put("STATE001.GST", 1, 40 * 512 + 17);
    Put("STATE002.GST", 2, 100);
    writes = 0;
    failOnly = k;
    (void)WriteFile(folder, "STATE001.GST", 7, 70 * 512 + 3);
    (void)WriteFile(folder, "STATE003.GST", 3, 60 * 512);
    failOnly = 0;
    total = writes;
    printf("TOTAL %ld\n", total);
    printf("EITHER STATE001.GST 1 %u 7 %u\n", 40 * 512 + 17, 70 * 512 + 3);
    printf("KEEP STATE002.GST 2 100\n");
    printf("NEW STATE003.GST 3 %u\n", 60 * 512);
    /* The same disk read by this code after a new mount. */
    memset(&fat, 0, sizeof(fat));
    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount after the failure");
    CHECK(FatFolder(&fat, 0, "GLOVERPR", 0, &folder), "folder after the failure");
    if (ReadFile(folder, "STATE001.GST", 1, 40 * 512 + 17) != 0 && ReadFile(folder, "STATE001.GST", 7, 70 * 512 + 3) != 0) {
        Die("the file is lost or damaged after one failed write", (int)k);
    }
    if (ReadFile(folder, "STATE002.GST", 2, 100) != 0) {
        Die("another file was damaged", (int)k);
    }
}

/* The root directory has no room for the folder: FatFolder says so, and nothing is left behind (the Python reader looks
 * for lost clusters). */
static void DirFull(void) {
    u32 again;

    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
    if (FatFolder(&fat, 0, "GLOVERPR", 0, &again) != FAT_ERR_NOTFOUND) {
        Die("the folder is there", 0);
    }
    if (FatFolder(&fat, 0, "GLOVERPR", 1, &again) != FAT_ERR_DIRFULL) {
        Die("a full root directory did not say so", 0);
    }
    if (FatFolder(&fat, 0, "GLOVERPR", 0, &again) != FAT_ERR_NOTFOUND) {
        Die("the folder is there after the failure", 0);
    }
    /* A file in the root directory fails the same way. */
    {
        FatWriter w;
        static u8 sector[512];

        FatWriteBegin(&fat, &w);
        CHECK(FatWrite(&fat, &w, sector, 1), "write");
        if (FatWriteEnd(&fat, &w, 0, "NEW.TXT", 100) != FAT_ERR_DIRFULL) {
            Die("a file in a full root directory did not say so", 0);
        }
    }
}

/* A folder entry whose first cluster is 0 is damaged: it must not be taken for the root directory. */
static void BadFolder(void) {
    u32 again;

    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
    if (FatFolder(&fat, 0, "GLOVERPR", 0, &again) != FAT_ERR_BAD) {
        Die("a folder with cluster 0 was not refused", 0);
    }
    if (FatFolder(&fat, 0, "GLOVERPR", 1, &again) != FAT_ERR_BAD) {
        Die("a folder with cluster 0 was not refused (make)", 0);
    }
}

/* A folder whose chain of clusters loops, with no free entry and no end: a search and a list say it is damaged. */
static void LoopDir(void) {
    FatEntry e;

    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
    CHECK(FatFolder(&fat, 0, "GLOVERPR", 0, &folder), "folder");
    if (FatFind(&fat, folder, "STATE001.GST", &e) != FAT_ERR_BAD) {
        Die("a looping folder was searched to its end", 0);
    }
    if (FatList(&fat, folder, CountFn, NULL) != FAT_ERR_BAD) {
        Die("a looping folder was listed to its end", 0);
    }
    {
        FatWriter w;
        static u8 sector[512];

        FatWriteBegin(&fat, &w);
        CHECK(FatWrite(&fat, &w, sector, 1), "write");
        if (FatWriteEnd(&fat, &w, folder, "STATE001.GST", 100) != FAT_ERR_BAD) {
            Die("a file was written into a looping folder", 0);
        }
    }
}

/* The first save to a card as the card code makes it: the folder, then a file of 619 KB in pieces of 19 sectors (the
 * card stage's writes). Prints what the device was asked for. */
static void FirstSave(void) {
    FatWriter w;
    static u8 buf[19 * 512];
    u32 sectors = 1210, done = 0;

    CHECK(FatMount(&fat, &dev, scratch, scratchSectors), "mount");
    CHECK(FatFolder(&fat, 0, "GLOVERPR", 1, &folder), "folder");
    FatWriteBegin(&fat, &w);
    while (done < sectors) {
        u32 n = sectors - done < 19 ? sectors - done : 19;

        CHECK(FatWrite(&fat, &w, buf, n), "write");
        done += n;
    }
    CHECK(FatWriteEnd(&fat, &w, folder, "STATE001.GST", sectors * 512 - 100), "end");
    printf("STATS reads %lu sectors in %lu commands, writes %lu sectors in %lu commands\n", readSectors, readCommands,
           writeSectors, writeCommands);
}

int main(int argc, char **argv) {
    int fd;
    off_t size;

    if (argc < 3) {
        fprintf(stderr, "usage: fathost SCENARIO IMAGE [K]\n");
        return 2;
    }
    /* The image is mapped. A sparse image of several gigabytes then costs only the sectors that are touched. */
    fd = open(argv[2], O_RDWR);
    if (fd < 0) {
        perror(argv[2]);
        return 2;
    }
    size = lseek(fd, 0, SEEK_END);
    image = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (image == MAP_FAILED) {
        perror("mmap");
        return 2;
    }
    imageSectors = (u32)(size / 512);
    if (getenv("FATHOST_SCRATCH") != NULL) {
        scratchSectors = (u32)atoi(getenv("FATHOST_SCRATCH"));
        scratch = scratchSectors == 0 ? NULL : scratchMemory;
    }
    if (strcmp(argv[1], "basic") == 0) {
        Basic();
    } else if (strcmp(argv[1], "full") == 0) {
        Full();
    } else if (strcmp(argv[1], "fragment") == 0) {
        Fragment();
    } else if (strcmp(argv[1], "powerfail") == 0) {
        PowerFail();
    } else if (strcmp(argv[1], "dirfull") == 0) {
        DirFull();
    } else if (strcmp(argv[1], "badfolder") == 0) {
        BadFolder();
    } else if (strcmp(argv[1], "loopdir") == 0) {
        LoopDir();
    } else if (strcmp(argv[1], "firstsave") == 0) {
        FirstSave();
    } else if (strcmp(argv[1], "transient") == 0) {
        Transient(argc > 3 ? atol(argv[3]) : 0);
    } else {
        fprintf(stderr, "no scenario %s\n", argv[1]);
        return 2;
    }
    if (msync(image, (size_t)size, MS_SYNC) != 0) {
        perror("msync");
        return 2;
    }
    munmap(image, (size_t)size);
    close(fd);
    printf("OK %s\n", argv[1]);
    return 0;
}
