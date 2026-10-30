#ifndef PRACTICE_FAT_H
#define PRACTICE_FAT_H

#include "types.h"

/* A small FAT16 and FAT32 file system, for the SD card (docs/build.md, "Savestates on the SD card"). It has no
 * dependence on the game or the console: tests/fathost.c builds it for the host.
 *
 * What it does: finds the volume (the first FAT partition of an MBR, or a card with no partition table), finds
 * and makes folders, and finds, writes, reads and deletes files by their short names (8.3, in upper case). It makes
 * no long names and does not read them: an entry with a long name keeps its short name too, and that is the name
 * it finds. A sector is 512 bytes. It does not do FAT12, exFAT or GPT.
 *
 * A directory is named by its first cluster; 0 is the root directory. A file is written to new clusters first. The
 * directory entry changes after that, and the old clusters of a file that is replaced are freed last. A power cut in
 * between leaves the old file whole, and some clusters that no file owns. */
#define FAT_SECTOR 512
/* The sectors of the FAT that are in memory at once: 16, which one SummerCart64 command moves. */
#define FAT_CACHE_SECTORS 16

enum {
    FAT_OK = 0,
    FAT_ERR_IO = -1,          /* the device failed */
    FAT_ERR_NOFS = -2,        /* no FAT volume found */
    FAT_ERR_UNSUPPORTED = -3, /* a FAT volume that this code does not read (FAT12, a sector size other than 512) */
    FAT_ERR_NOTFOUND = -4,
    FAT_ERR_FULL = -5,        /* no free cluster */
    FAT_ERR_DIRFULL = -6,     /* the directory has no room for one more entry (the root of a FAT16 volume) */
    FAT_ERR_BAD = -7,         /* the file system is damaged: a cluster number out of range, a chain that loops */
    FAT_ERR_EXISTS = -8,      /* a name that is a file where a folder is needed, or a folder where a file is */
    FAT_ERR_NAME = -9,        /* not an 8.3 name */
};

/* The device: reads and writes COUNT sectors from sector LBA of the card (not of the volume). Returns 0 if it did. */
typedef struct FatDev {
    int (*read)(void *ctx, u32 lba, u32 count, u8 *buf);
    int (*write)(void *ctx, u32 lba, u32 count, const u8 *buf);
    void *ctx;
} FatDev;

typedef struct Fat {
    FatDev dev;
    u8 type; /* 16 or 32 */
    u8 spc;  /* sectors in a cluster */
    u8 numFats;
    u8 fatDirty;
    u8 fatWrites;        /* the FAT copies that a change is written to: all of them, or the one active FAT */
    u16 rootEntries;     /* FAT16 */
    u32 volume;          /* the volume's first sector */
    u32 fatStart;        /* the first sector of the first FAT */
    u32 fatSectors;      /* the sectors of one FAT */
    u32 rootStart;       /* FAT16: the first sector of the root directory */
    u32 rootSectors;     /* FAT16 */
    u32 rootCluster;     /* FAT32 */
    u32 dataStart;       /* the first sector of cluster 2 */
    u32 clusters;        /* the data clusters: numbers 2 to clusters + 1 */
    u32 allocHint;       /* where the search for a free cluster starts */
    u32 fsInfo;          /* FAT32: the sector of the FSInfo block, or 0 */
    u32 fatGroup;        /* the first FAT sector (counted in the FAT) of the cache, or 0xFFFFFFFF */
    u32 dataSector;      /* the card sector in dataBuf, or 0xFFFFFFFF */
    u8 *scratch;         /* FatMount's SCRATCH: SCRATCH_SECTORS sectors that FatFolder and FatWriteEnd may overwrite */
    u32 scratchSectors;
    u8 fatBuf[FAT_CACHE_SECTORS * FAT_SECTOR];
    u8 dataBuf[FAT_SECTOR];
} Fat;

/* A directory entry that was found, with its place. */
typedef struct FatEntry {
    u8 name[11]; /* the short name: 8 characters and 3, padded with spaces */
    u8 attr;     /* bit 4: a folder */
    u32 cluster; /* the first cluster, 0 for an empty file */
    u32 size;    /* the size in bytes */
    u32 sector;  /* where the entry is: the card's sector and the byte in it. */
    u16 offset;
} FatEntry;

#define FAT_ATTR_DIR 0x10

/* A file being written. */
typedef struct FatWriter {
    u32 first; /* the first cluster, 0 before any data */
    u32 last;  /* the last cluster taken */
    u32 lba;   /* the next sector to write */
    u32 left;  /* the sectors that follow it in the clusters taken */
} FatWriter;

/* A file being read. */
typedef struct FatReader {
    u32 cluster;
    u32 inCluster; /* the sectors of the cluster read so far */
} FatReader;

/* Writes the 11 characters of the short name for NAME ("STATE001.GST", "GLOVERPR") into OUT. Returns 0, or
 * FAT_ERR_NAME. */
int FatName(u8 *out, const char *name);

/* Finds the volume and reads its layout. SCRATCH, if not NULL, is memory of SCRATCH_SECTORS sectors (word-aligned).
 * FatFolder and FatWriteEnd use it to write a new directory cluster in few commands, and overwrite it. The caller
 * has no data in it then. Without it, a cluster is written a sector at a time. */
int FatMount(Fat *f, const FatDev *dev, u8 *scratch, u32 scratch_sectors);

/* Looks for NAME in the directory DIR. Returns FAT_OK or FAT_ERR_NOTFOUND (or an error). */
int FatFind(Fat *f, u32 dir, const char *name, FatEntry *e);

/* Calls FN for each file and folder of DIR (not the entries for "." and "..", deleted entries or long names). It
 * stops, and returns the same value, when FN returns something other than 0. */
typedef int (*FatListFn)(void *ctx, const FatEntry *e);
int FatList(Fat *f, u32 dir, FatListFn fn, void *ctx);

/* Gives the first cluster of the folder NAME in PARENT. Makes the folder if it is not there and MAKE is set. */
int FatFolder(Fat *f, u32 parent, const char *name, int make, u32 *cluster);

/* Writes a file in pieces: Begin, any number of Write calls (a piece is whole sectors, until the last), then End,
 * which names the file and makes it appear (or replaces the file of that name). Abort frees what was written. After
 * an error from Write the caller calls Abort. */
void FatWriteBegin(Fat *f, FatWriter *w);
int FatWrite(Fat *f, FatWriter *w, const u8 *data, u32 sectors);
int FatWriteEnd(Fat *f, FatWriter *w, u32 dir, const char *name, u32 size);
void FatWriteAbort(Fat *f, FatWriter *w);

/* Reads a file in pieces: Open, then Read for whole sectors, in order. A read past the end of the chain is an error. */
int FatOpen(Fat *f, const FatEntry *e, FatReader *r);
int FatRead(Fat *f, FatReader *r, u8 *buf, u32 sectors);

/* Deletes the file NAME in DIR and frees its clusters. */
int FatDelete(Fat *f, u32 dir, const char *name);

#endif
