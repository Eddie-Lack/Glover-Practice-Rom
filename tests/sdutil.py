"""Shared by the SD card tests (sdcard.py, sdcardload.py, sdcardfull.py, sdcardbig.py): the mailbox that stands in for the
cart, a reader of card files that shares no code with the ROM, and functions that damage a card file on purpose."""
import hashlib
import os
import re
import struct
import threading
import time
import zlib

import fatimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MB_OPEN, MB_READ, MB_WRITE, MB_CLOSE = 1, 2, 3, 4
MB_OK, MB_NO_CARD, MB_FAILED = 0, 1, 2
CARD_NO_CART, CARD_NO_CARD, CARD_NO_FS, CARD_NO_FOLDER, CARD_READY, CARD_ERROR = 1, 2, 3, 4, 5, 6
REQ_CHECK, REQ_SAVE, REQ_LOAD, REQ_DELETE = 1, 2, 3, 4
HEADER = 64


class Mailbox:
    """Answers the ROM's requests for sectors (practice/src/sd.c, SdMailbox) from a disk image. The ROM must be the test
    build (make testrom), and its sdBackend must be 1. The emulator calls the frame callback only when the picture
    changes, and a save to the card stops the picture. A thread of its own looks at the mailbox every half millisecond.
    start() begins it."""

    def __init__(self, sym, path, present=True):
        self.addr = sym("sdMailbox")
        self.path = path
        self.f = open(path, "r+b")
        self.size = os.path.getsize(path) // 512
        self.present = present
        self.opened = False
        self.reads = self.writes = 0   # sectors
        self.read_commands = self.write_commands = 0
        self.opens = 0
        self.errors = []
        self.lock = threading.Lock()
        self.running = False
        self.thread = None
        self.snap = None   # a function (emu, sectors moved so far) that the server calls after each read or write

    def start(self, emu):
        emu.attach_ram()
        self.running = True

        def loop():
            while self.running:
                try:
                    with self.lock:
                        self.serve(emu)
                except Exception as e:  # shown by check()
                    self.errors.append(f"the server stopped: {e!r}")
                    return
                time.sleep(0.0003)

        self.thread = threading.Thread(target=loop, daemon=True)
        self.thread.start()

    def stop(self):
        """Ends the thread. The test calls this before it ends. The thread reads RDRAM, which the emulator frees."""
        self.running = False
        if self.thread is not None:
            self.thread.join()
            self.thread = None

    def serve(self, emu):
        a = self.addr
        command = emu.r32(a)
        if command == 0:
            return
        sector, count = emu.r32(a + 4), emu.r32(a + 8)
        status = MB_OK
        if command == MB_OPEN:
            self.opens += 1
            if self.opened:
                self.errors.append("opened twice")
            status = MB_OK if self.present else MB_NO_CARD
            self.opened = status == MB_OK
        elif command == MB_CLOSE:
            if not self.opened:
                self.errors.append("closed without an open")
            self.opened = False
        elif command in (MB_READ, MB_WRITE):
            if not self.opened:
                self.errors.append("a sector command without an open")
                status = MB_FAILED
            elif not 0 < count <= 16 or sector + count > self.size:
                self.errors.append(f"sectors {sector}+{count} outside the disk")
                status = MB_FAILED
            elif command == MB_READ:
                self.f.seek(sector * 512)
                emu._put(a + 32, self.f.read(count * 512))
                self.reads += count
                self.read_commands += 1
            else:
                self.f.seek(sector * 512)
                self.f.write(emu.read(a + 32, count * 512))
                self.f.flush()
                self.writes += count
                self.write_commands += 1
        else:
            self.errors.append(f"command {command}")
            status = MB_FAILED
        emu.w32(a + 12, status)
        emu.w32(a, 0)
        if self.snap is not None and command in (MB_READ, MB_WRITE):
            self.snap(emu, self.reads + self.writes)

    def check(self, emu):
        if self.errors:
            emu.fail("the mailbox saw: " + "; ".join(self.errors[:3]))


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def state_ranges():
    text = open(os.path.join(ROOT, "build", "state_ranges.h")).read()
    return [(int(a, 16), int(b, 16)) for a, b in re.findall(r"\{ 0x([0-9A-F]+), 0x([0-9A-F]+) \}", text)]


def chunks():
    """(address, size) of every chunk of a state: the ranges cut into pieces of at most 16 KB."""
    out = []
    for lo, hi in state_ranges():
        while lo < hi:
            n = min(0x4000, hi - lo)
            out.append((lo, n))
            lo += n
    return out


def lz_decode(data, pos, n):
    """Decodes a block of N bytes from DATA at POS (practice/include/lz.h). Returns the bytes and the end position."""
    out = bytearray()
    rep = 4
    while True:
        token = data[pos]
        pos += 1
        lit = token >> 6
        if lit == 3:
            while True:
                e = data[pos]
                pos += 1
                lit += e
                if e != 255:
                    break
        assert lit <= n - len(out), "too many literals"
        out += data[pos:pos + lit]
        pos += lit
        if len(out) >= n:
            return bytes(out), pos
        kind = (token >> 4) & 3
        if kind == 0:
            offset = rep
        elif kind == 1:
            offset = data[pos] << 2
            pos += 1
        else:
            offset = (data[pos] << 8 | data[pos + 1]) << 2
            pos += 2
        rep = offset
        length = (token & 15) + 4
        if token & 15 == 15:
            while True:
                e = data[pos]
                pos += 1
                length += e
                if e != 255:
                    break
        assert 0 < offset <= len(out) and length <= n - len(out), "bad match"
        if offset >= length:
            out += out[len(out) - offset:len(out) - offset + length]
        else:
            for _ in range(length):
                out.append(out[-offset])
        if len(out) >= n:
            return bytes(out), pos


def parse_card_file(data):
    """The header, the CRC check and the decoded state of a card file (docs/build.md): (header dict, {address: bytes}).
    Fails with an assertion if anything is wrong."""
    assert data[:4] == b"GPSS", "magic"
    version, header_size = data[5], data[7]
    assert version == 2 and header_size == HEADER
    h = {
        "level": data[8], "chunks": struct.unpack_from(">H", data, 10)[0], "song": struct.unpack_from(">i", data, 12)[0],
        "bytes": struct.unpack_from(">I", data, 16)[0], "layout": struct.unpack_from(">I", data, 20)[0],
        "size": len(data), "frames": struct.unpack_from(">I", data, 24)[0], "runs": struct.unpack_from(">I", data, 28)[0],
    }
    crc = struct.unpack_from(">I", data, len(data) - 4)[0]
    assert zlib.crc32(data[:-4]) == crc, "crc"
    memory = {}
    pos = HEADER
    for addr, n in chunks():
        mode, length = data[pos] >> 6, (data[pos] & 0x3F) << 8 | data[pos + 1]
        if mode == 1:
            block, end = lz_decode(data, pos + 2, n)
            assert end == pos + 2 + length, "a record's length"
            pos = end
        elif mode == 3:
            start = (pos + 2 + 3) & ~3
            block = data[start:start + n]
            pos = start + n
        else:
            raise AssertionError(f"record mode {mode}")
        memory[addr] = block
    # A recording file (RECORD.GRC) has runs of 5 bytes after the chunks: the header says how many (offset 28).
    trailer = 5 * struct.unpack_from(">I", data, 28)[0]
    assert pos == len(data) - 4 - trailer, f"records end at {pos}, the file at {len(data) - 4 - trailer}"
    assert h["chunks"] == len(memory) and h["bytes"] == sum(n for _, n in chunks())
    return h, memory


def card_files(path):
    """{number: (first cluster, size)} of the card files in the image's folder GLOVERPR, read by fatimage.Image, and
    the image."""
    img = fatimage.Image(path)
    try:
        is_dir, cluster, _ = img.lookup("GLOVERPR")
    except KeyError:
        return img, None, {}
    out = {}
    for name, (d, first, size) in img.listdir(cluster).items():
        m = re.fullmatch(r"STATE(\d\d\d)\.GST", name)
        if m and not d:
            out[int(m.group(1))] = (first, size)
    return img, cluster, out


def read_card_file(path, number):
    img, _, files = card_files(path)
    try:
        first, size = files[number]
        return img.read(first, size)
    finally:
        img.close()


def _switch(self, path):
    """Serves another image from now on (a card that was swapped)."""
    with self.lock:
        self.f.close()
        self.path = path
        self.f = open(path, "r+b")
        self.size = os.path.getsize(path) // 512


Mailbox.switch = _switch


def _card_file_place(path, number):
    """(chain of lbas of the file's clusters, the cluster size in bytes, the file's size, the byte offset of its directory
    entry in the image)."""
    img, folder, files = card_files(path)
    try:
        first, size = files[number]
        lba = lambda c: img.data_start + (c - 2) * img.spc  # noqa: E731
        entry_at = None
        for c in img.chain(folder):
            block = img.cluster_data(c)
            for off in range(0, len(block), 32):
                if block[off:off + 11] == b"STATE%03dGST" % number:
                    entry_at = lba(c) * 512 + off
        assert entry_at is not None, "directory entry not found"
        return [lba(c) for c in img.chain(first)], img.spc * 512, size, entry_at
    finally:
        img.close()


def read_card_file_raw(path, number):
    """(the file's bytes, its size) as the directory says."""
    lbas, cluster_bytes, size, _ = _card_file_place(path, number)
    with open(path, "rb") as f:
        data = b""
        for lba in lbas:
            f.seek(lba * 512)
            data += f.read(cluster_bytes)
    return data[:size], size


def write_card_file_raw(path, number, data, size=None):
    """Writes DATA over the file's clusters as they are (the CRC is not touched), and the size into the directory entry
    when SIZE is given."""
    lbas, cluster_bytes, _, entry_at = _card_file_place(path, number)
    with open(path, "r+b") as f:
        for i, lba in enumerate(lbas):
            part = data[i * cluster_bytes:(i + 1) * cluster_bytes]
            if part:
                f.seek(lba * 512)
                f.write(part)
        if size is not None:
            f.seek(entry_at + 28)
            f.write(struct.pack("<I", size))


def edit_card_file(path, number, edit):
    """Changes bytes of card file NUMBER with EDIT(bytearray), keeps the length, and makes the CRC right again. Only the edit
    is wrong then. Returns the old bytes for restore_card_file."""
    old, size = read_card_file_raw(path, number)
    new = bytearray(old)
    edit(new)
    assert len(new) == len(old)
    struct.pack_into(">I", new, len(new) - 4, zlib.crc32(bytes(new[:-4])))
    write_card_file_raw(path, number, bytes(new))
    return old, size


def restore_card_file(path, number, saved):
    old, size = saved
    write_card_file_raw(path, number, old, size)


def read_named_file(path, name):
    """The bytes of the file NAME in the image's folder GLOVERPR, or None."""
    img = fatimage.Image(path)
    try:
        try:
            is_dir, cluster, _ = img.lookup("GLOVERPR")
        except KeyError:
            return None
        entry = img.listdir(cluster).get(name)
        return None if entry is None else img.read(entry[1], entry[2])
    finally:
        img.close()


def named_file_place(path, name):
    """(the first sector of the file's data, its size) in the image."""
    img = fatimage.Image(path)
    try:
        is_dir, cluster, _ = img.lookup("GLOVERPR")
        _, first, size = img.listdir(cluster)[name]
        return img.data_start + (first - 2) * img.spc, size
    finally:
        img.close()
