/* The SD card of a flash cart (sd.h).
 *
 * SummerCart64: the commands and registers are those of the cart's public documentation (its N64 command table, its
 * memory map and its USB interface page for the SD operations). The registers are at 0x1FFF0000 on the cart bus and
 * start locked. A write of two values to the KEY register opens them, and the IDENTIFIER register then reads "SCv2".
 * Another cart does not answer to that. This code writes the three KEY values and reads one register on any cart,
 * and then stops. It does so on the first use of the card, never at boot.
 *
 * Each command is made under libultra's PI lock, which the game's own DMAs from the cartridge take too. */
#include "game.h"
#include "savestate.h"
#include "sd.h"

u32 sdSectorsRead;
u32 sdSectorsWritten;
u32 sdWaitTicks;

#ifdef PRACTICE_TEST_MAILBOX
u8 sdBackend;

/* ---- The test mailbox ----------------------------------------------------------------------------------------- */

/* The game writes a request and sets command last. The harness answers, sets status, then clears command. The game
 * reads and writes the block through the uncached alias of its address. tests/sdcard.py has the layout. */
enum {
    MB_NONE,
    MB_OPEN,
    MB_READ,
    MB_WRITE,
    MB_CLOSE,
};
enum {
    MB_OK,
    MB_NO_CARD,
    MB_FAILED,
};

/* The block holds 16 sectors, as many as one SummerCart64 command moves. The command counts then match a cart's. */
#define MAILBOX_BURST SD_BURST

typedef struct SdMailbox {
    u32 command;
    u32 sector;
    u32 count;
    u32 status;
    u32 requests; /* the requests made, for the tests */
    u32 pad[3];
    u8 data[MAILBOX_BURST * FAT_SECTOR];
} SdMailbox;

/* At TEST_MAILBOX (savestate.h): the test link defines the symbol. */
extern SdMailbox sdMailbox;
_Static_assert(sizeof(SdMailbox) <= TEST_MAILBOX_SIZE, "the mailbox fits its memory");
_Static_assert(TEST_MAILBOX + TEST_MAILBOX_SIZE == STATE_SCRATCH, "the mailbox lies between the pool and the scratch");
_Static_assert(__builtin_offsetof(SdMailbox, data) == 32, "tests/sdcard.py has the offsets");

#define UNCACHED(p) ((volatile u32 *)((u32)(p) | 0xA0000000))
#define MAILBOX_TIMEOUT_TICKS (20 * 46875000u)

static int MailboxCall(u32 command, u32 sector, u32 count) {
    volatile u32 *mb = UNCACHED(&sdMailbox);
    u32 start = osGetCount();

    mb[1] = sector;
    mb[2] = count;
    mb[3] = 0xFFFFFFFF;
    mb[4]++;
    mb[0] = command;
    while (mb[0] != MB_NONE) {
        if (osGetCount() - start > MAILBOX_TIMEOUT_TICKS) {
            mb[0] = MB_NONE;
            sdWaitTicks += osGetCount() - start;
            return SD_ERR_TIMEOUT;
        }
    }
    sdWaitTicks += osGetCount() - start;
    if (mb[3] == MB_NO_CARD) {
        return SD_ERR_NO_CARD;
    }
    return mb[3] == MB_OK ? SD_OK : SD_ERR_IO;
}

/* Copies WORDS words between the mailbox's block and a buffer. */
static void MailboxCopyOut(u8 *dst, u32 bytes) {
    const volatile u32 *src = UNCACHED(sdMailbox.data);

    for (u32 i = 0; i < bytes / 4; i++) {
        u32 w = src[i];

        dst[4 * i] = (u8)(w >> 24);
        dst[4 * i + 1] = (u8)(w >> 16);
        dst[4 * i + 2] = (u8)(w >> 8);
        dst[4 * i + 3] = (u8)w;
    }
}

static void MailboxCopyIn(const u8 *src, u32 bytes) {
    volatile u32 *dst = UNCACHED(sdMailbox.data);

    for (u32 i = 0; i < bytes / 4; i++) {
        dst[i] = (u32)src[4 * i] << 24 | (u32)src[4 * i + 1] << 16 | (u32)src[4 * i + 2] << 8 | src[4 * i + 3];
    }
}

static int MailboxRead(void *ctx, u32 lba, u32 count, u8 *buf) {
    (void)ctx;
    while (count > 0) {
        u32 n = count < MAILBOX_BURST ? count : MAILBOX_BURST;

        if (MailboxCall(MB_READ, lba, n) != SD_OK) {
            return 1;
        }
        MailboxCopyOut(buf, n * FAT_SECTOR);
        sdSectorsRead += n;
        lba += n;
        buf += n * FAT_SECTOR;
        count -= n;
    }
    return 0;
}

static int MailboxWrite(void *ctx, u32 lba, u32 count, const u8 *buf) {
    (void)ctx;
    while (count > 0) {
        u32 n = count < MAILBOX_BURST ? count : MAILBOX_BURST;

        MailboxCopyIn(buf, n * FAT_SECTOR);
        if (MailboxCall(MB_WRITE, lba, n) != SD_OK) {
            return 1;
        }
        sdSectorsWritten += n;
        lba += n;
        buf += n * FAT_SECTOR;
        count -= n;
    }
    return 0;
}
#endif /* PRACTICE_TEST_MAILBOX */

/* ---- The SummerCart64 ----------------------------------------------------------------------------------------- */

/* The cart bus addresses in the uncached segment (0xA0000000 plus the physical address). */
#define SC64_SCR 0xBFFF0000u
#define SC64_DATA0 0xBFFF0004u
#define SC64_DATA1 0xBFFF0008u
#define SC64_IDENTIFIER 0xBFFF000Cu
#define SC64_KEY 0xBFFF0010u
#define SC64_BUFFER 0xBFFE0000u /* the 8 KB data buffer */
#define PI_STATUS_REG 0xA4600010u

/* The address that SD_READ and SD_WRITE take for the data buffer. The N64 command table (docs/02_n64_commands.md) calls
 * the argument "pi_address", and the memory map (01_memory_map.md) puts the buffer at 0x1FFE0000 on the PI bus and at
 * 0x05000000 in the cart's own map. The USB table (03_usb_interface.md) calls the same argument "address" without
 * saying which map; that page serves the PC side, and the cart's own map is the one it uses everywhere. The command
 * from the console is the one in the first table, so the PI address is tried first. If a test read leaves the buffer
 * as it was, the other address is tried, and the one that works is kept (Sc64CheckBuffer). */
#define SC64_BUFFER_PI 0x1FFE0000u
#define SC64_BUFFER_INTERNAL 0x05000000u

#define SC64_IDENTIFIER_VALUE 0x53437632u /* "SCv2" */
#define SC64_KEY_UNLOCK_1 0x5F554E4Cu
#define SC64_KEY_UNLOCK_2 0x4F434B5Fu
#define SC64_KEY_LOCK 0xFFFFFFFFu
#define SC64_KEY_RESET 0u

#define SC64_CMD_BUSY 0x80000000u
#define SC64_CMD_ERROR 0x40000000u

#define SC64_CMD_SD_CARD_OP 'i'
#define SC64_CMD_SD_SECTOR_SET 'I'
#define SC64_CMD_SD_READ 's'
#define SC64_CMD_SD_WRITE 'S'
#define SC64_CMD_WRITEBACK_PENDING 'w'

#define SC64_SD_INIT 0
#define SC64_SD_DEINIT 1
#define SC64_SD_STATUS 2
#define SC64_SD_BYTE_SWAP_ON 4
#define SC64_SD_BYTE_SWAP_OFF 5
#define SC64_SD_STATUS_INSERTED 1
#define SC64_SD_STATUS_BYTE_SWAP 0x10

/* A command can take long: the card initialises, and a write waits for the card. */
#define SC64_TIMEOUT_TICKS (10 * 46875000u)
/* How long a lock waits for a command that is still running. */
#define SC64_LOCK_WAIT_TICKS 46875000u

static u32 bufferAddress = SC64_BUFFER_PI;
static u8 byteSwapWasOn;

static u32 PiRead(u32 addr) {
    while (*(volatile u32 *)PI_STATUS_REG & 3) {
    }
    return *(volatile u32 *)addr;
}

static void PiWrite(u32 addr, u32 value) {
    while (*(volatile u32 *)PI_STATUS_REG & 3) {
    }
    *(volatile u32 *)addr = value;
}

/* Runs one command with the PI lock held. The results go to *R0 (DATA0) and *R1 (DATA1), when not NULL. Returns SD_OK,
 * SD_ERR_IO (the cart set its error bit) or SD_ERR_TIMEOUT. A command that timed out may still be running: the next
 * command waits for the busy bit first, and writes no register while it is set. */
static int Sc64Command(u32 command, u32 arg0, u32 arg1, u32 *r0, u32 *r1) {
    u32 start = osGetCount();
    u32 status;

    while ((status = PiRead(SC64_SCR)) & SC64_CMD_BUSY) {
        if (osGetCount() - start > SC64_TIMEOUT_TICKS) {
            sdWaitTicks += osGetCount() - start;
            return SD_ERR_TIMEOUT;
        }
    }
    PiWrite(SC64_DATA0, arg0);
    PiWrite(SC64_DATA1, arg1);
    PiWrite(SC64_SCR, command);
    while ((status = PiRead(SC64_SCR)) & SC64_CMD_BUSY) {
        if (osGetCount() - start > SC64_TIMEOUT_TICKS) {
            sdWaitTicks += osGetCount() - start;
            return SD_ERR_TIMEOUT;
        }
    }
    sdWaitTicks += osGetCount() - start;
    if (status & SC64_CMD_ERROR) {
        return SD_ERR_IO;
    }
    if (r0 != NULL) {
        *r0 = PiRead(SC64_DATA0);
    }
    if (r1 != NULL) {
        *r1 = PiRead(SC64_DATA1);
    }
    return SD_OK;
}

static int Sc64Probe(void) {
    PiWrite(SC64_KEY, SC64_KEY_RESET);
    PiWrite(SC64_KEY, SC64_KEY_UNLOCK_1);
    PiWrite(SC64_KEY, SC64_KEY_UNLOCK_2);
    return PiRead(SC64_IDENTIFIER) == SC64_IDENTIFIER_VALUE;
}

/* Locks the registers again, unless a command is still running: the lock would cut it off. They then stay open. */
static void Sc64Lock(void) {
    u32 start = osGetCount();

    while (PiRead(SC64_SCR) & SC64_CMD_BUSY) {
        if (osGetCount() - start > SC64_LOCK_WAIT_TICKS) {
            return;
        }
    }
    PiWrite(SC64_KEY, SC64_KEY_LOCK);
    while (*(volatile u32 *)PI_STATUS_REG & 3) {
    }
}

static u32 GuardWord(u32 i) {
    return 0xA5C3961Bu ^ (i * 0x9E3779B1u);
}

/* Reads sector 0 into the data buffer, which holds a pattern before, and requires the buffer to have changed. The
 * command's address argument is not certain (see SC64_BUFFER_PI), and a read that went to another place would leave
 * the buffer as it was. A write that did the same would write the buffer's old content to the card. So nothing
 * writes before this has passed. */
static int Sc64CheckBuffer(void) {
    static const u32 addresses[2] = { SC64_BUFFER_PI, SC64_BUFFER_INTERNAL };

    for (int a = 0; a < 2; a++) {
        int r;

        for (u32 i = 0; i < FAT_SECTOR / 4; i++) {
            PiWrite(SC64_BUFFER + 4 * i, GuardWord(i));
        }
        r = Sc64Command(SC64_CMD_SD_SECTOR_SET, 0, 0, NULL, NULL);
        if (r == SD_OK) {
            r = Sc64Command(SC64_CMD_SD_READ, addresses[a], 1, NULL, NULL);
        }
        if (r == SD_ERR_TIMEOUT) {
            return r;
        }
        for (u32 i = 0; r == SD_OK && i < FAT_SECTOR / 4; i++) {
            if (PiRead(SC64_BUFFER + 4 * i) != GuardWord(i)) {
                bufferAddress = addresses[a];
                return SD_OK;
            }
        }
    }
    return SD_ERR_BUFFER;
}

static int Sc64Open(void) {
    u32 status = 0, pending = 0;
    int initialised = 0;
    int r;

    byteSwapWasOn = 0;
    __osPiGetAccess();
    if (!Sc64Probe()) {
        __osPiRelAccess();
        return SD_ERR_NO_CART;
    }
    /* The cart writes an EEPROM save back to the card on its own, and the card cannot be shared. */
    r = Sc64Command(SC64_CMD_WRITEBACK_PENDING, 0, 0, &pending, NULL);
    if (r == SD_OK && pending != 0) {
        r = SD_ERR_WRITEBACK;
    }
    if (r == SD_OK) {
        r = Sc64Command(SC64_CMD_SD_CARD_OP, 0, SC64_SD_STATUS, NULL, &status);
    }
    if (r == SD_OK && !(status & SC64_SD_STATUS_INSERTED)) {
        r = SD_ERR_NO_CARD;
    }
    if (r == SD_OK) {
        initialised = 1;
        r = Sc64Command(SC64_CMD_SD_CARD_OP, 0, SC64_SD_INIT, NULL, NULL);
    }
    if (r == SD_OK) {
        r = Sc64Command(SC64_CMD_SD_CARD_OP, 0, SC64_SD_STATUS, NULL, &status);
    }
    if (r == SD_OK && (status & SC64_SD_STATUS_BYTE_SWAP)) {
        /* Sectors arrive as the bytes of the card. SdClose sets the swap on again. */
        r = Sc64Command(SC64_CMD_SD_CARD_OP, 0, SC64_SD_BYTE_SWAP_OFF, NULL, NULL);
        byteSwapWasOn = r == SD_OK;
    }
    if (r == SD_OK) {
        r = Sc64CheckBuffer();
    }
    if (r != SD_OK) {
        if (initialised) {
            if (byteSwapWasOn) {
                Sc64Command(SC64_CMD_SD_CARD_OP, 0, SC64_SD_BYTE_SWAP_ON, NULL, NULL);
            }
            Sc64Command(SC64_CMD_SD_CARD_OP, 0, SC64_SD_DEINIT, NULL, NULL);
        }
        Sc64Lock();
    }
    __osPiRelAccess();
    return r;
}

static void Sc64Close(void) {
    __osPiGetAccess();
    if (byteSwapWasOn) {
        Sc64Command(SC64_CMD_SD_CARD_OP, 0, SC64_SD_BYTE_SWAP_ON, NULL, NULL);
        byteSwapWasOn = 0;
    }
    Sc64Command(SC64_CMD_SD_CARD_OP, 0, SC64_SD_DEINIT, NULL, NULL);
    Sc64Lock();
    __osPiRelAccess();
}

static int Sc64Read(void *ctx, u32 lba, u32 count, u8 *buf) {
    (void)ctx;
    while (count > 0) {
        u32 n = count < SD_BURST ? count : SD_BURST;
        int r;

        __osPiGetAccess();
        r = Sc64Command(SC64_CMD_SD_SECTOR_SET, lba, 0, NULL, NULL);
        if (r == SD_OK) {
            r = Sc64Command(SC64_CMD_SD_READ, bufferAddress, n, NULL, NULL);
        }
        for (u32 i = 0; r == SD_OK && i < n * FAT_SECTOR / 4; i++) {
            u32 w = PiRead(SC64_BUFFER + 4 * i);

            buf[4 * i] = (u8)(w >> 24);
            buf[4 * i + 1] = (u8)(w >> 16);
            buf[4 * i + 2] = (u8)(w >> 8);
            buf[4 * i + 3] = (u8)w;
        }
        __osPiRelAccess();
        if (r != SD_OK) {
            return 1;
        }
        sdSectorsRead += n;
        lba += n;
        buf += n * FAT_SECTOR;
        count -= n;
    }
    return 0;
}

static int Sc64Write(void *ctx, u32 lba, u32 count, const u8 *buf) {
    (void)ctx;
    while (count > 0) {
        u32 n = count < SD_BURST ? count : SD_BURST;
        int r;

        __osPiGetAccess();
        for (u32 i = 0; i < n * FAT_SECTOR / 4; i++) {
            PiWrite(SC64_BUFFER + 4 * i, (u32)buf[4 * i] << 24 | (u32)buf[4 * i + 1] << 16 | (u32)buf[4 * i + 2] << 8 | buf[4 * i + 3]);
        }
        r = Sc64Command(SC64_CMD_SD_SECTOR_SET, lba, 0, NULL, NULL);
        if (r == SD_OK) {
            r = Sc64Command(SC64_CMD_SD_WRITE, bufferAddress, n, NULL, NULL);
        }
        __osPiRelAccess();
        if (r != SD_OK) {
            return 1;
        }
        sdSectorsWritten += n;
        lba += n;
        buf += n * FAT_SECTOR;
        count -= n;
    }
    return 0;
}

/* ---- The choice of device ------------------------------------------------------------------------------------- */

static const FatDev sc64Device = { Sc64Read, Sc64Write, NULL };
static const char *cartName = "none";

#ifdef PRACTICE_TEST_MAILBOX
static const FatDev mailboxDevice = { MailboxRead, MailboxWrite, NULL };
static u8 openBackend;

int SdOpen(void) {
    int r;

    openBackend = sdBackend;
    if (openBackend == SD_BACKEND_MAILBOX) {
        r = MailboxCall(MB_OPEN, 0, 0);
        cartName = r == SD_OK || r == SD_ERR_NO_CARD ? "mailbox" : "none";
        return r;
    }
    r = Sc64Open();
    cartName = r == SD_ERR_NO_CART ? "none" : "SC64";
    return r;
}

void SdClose(void) {
    if (openBackend == SD_BACKEND_MAILBOX) {
        MailboxCall(MB_CLOSE, 0, 0);
    } else {
        Sc64Close();
    }
}

const FatDev *SdDevice(void) {
    return openBackend == SD_BACKEND_MAILBOX ? &mailboxDevice : &sc64Device;
}
#else
int SdOpen(void) {
    int r = Sc64Open();

    cartName = r == SD_ERR_NO_CART ? "none" : "SC64";
    return r;
}

void SdClose(void) {
    Sc64Close();
}

const FatDev *SdDevice(void) {
    return &sc64Device;
}
#endif

const char *SdName(void) {
    return cartName;
}
