#ifndef PRACTICE_SD_H
#define PRACTICE_SD_H

#include "fat.h"
#include "types.h"

/* The SD card of a flash cart, as 512-byte sectors (docs/build.md, "Savestates on the SD card"). The code finds the
 * cart by its identification register, and does nothing on a cart that does not answer. Only the SummerCart64 has a
 * driver. The test build (make testrom, with PRACTICE_TEST_MAILBOX defined for sd.c) has a second device, the
 * mailbox: the practice code asks for sectors in a block of RAM, and the test harness answers from a disk image.
 * The harness chooses it by setting sdBackend. The ROM that players get has neither the mailbox nor sdBackend. */
enum {
    SD_OK = 0,
    SD_ERR_NO_CART = -1, /* no flash cart that has a driver answers */
    SD_ERR_NO_CARD = -2, /* the cart has no card in its slot */
    SD_ERR_IO = -3,      /* the card or the cart failed a command */
    SD_ERR_TIMEOUT = -4,
    SD_ERR_WRITEBACK = -5, /* the cart still has a save to write back to the card, and the card cannot be shared */
    SD_ERR_BUFFER = -6,    /* a test read left the cart's data buffer as it was: no sector is written */
};

#ifdef PRACTICE_TEST_MAILBOX
enum {
    SD_BACKEND_AUTO,    /* look for a cart */
    SD_BACKEND_MAILBOX, /* the test harness */
};
extern u8 sdBackend;
#endif

/* The most sectors that one command moves: the SummerCart64's buffer holds 8 KB. */
#define SD_BURST 16

/* Finds the cart and starts the card. Returns SD_OK or an error. Every SdOpen that returned SD_OK needs an SdClose.
 * Nothing is written to the card. */
int SdOpen(void);
void SdClose(void);

/* The device for the file system code, after SdOpen. */
const FatDev *SdDevice(void);
/* The name of the cart that answered ("SC64"), or "none". */
const char *SdName(void);

/* The sectors moved since boot (the tests read them). */
extern u32 sdSectorsRead;
extern u32 sdSectorsWritten;
/* The ticks that the code waited for the cart (or the mailbox) to finish a command, since boot. */
extern u32 sdWaitTicks;

#endif
