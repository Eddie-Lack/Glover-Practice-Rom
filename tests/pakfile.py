"""Controller Pak files for the tests, from the public layout of the pak's file system (practice/include/cpak.h has
the description). A .mpk file of the emulator holds four paks of 32 KB, one for each controller. The functions here
work on the 32 KB of one pak, a bytearray.

This is a second, independent reading of the layout: the driver in the ROM and these functions must agree, and the
emulator's own format of a new pak must pass the checks here."""
import struct

PAGE = 256
PAGES = 128
FIRST_DATA = 5
NOTES = 16
PAK_SIZE = 0x8000
FREE = 3
LAST = 1
NOTE_CODE = b"GPRC"
NOTE_COMPANY = b"GP"
NOTE_NAME = "GLOVER PRACTICE"
RUN_SIZE = 5


class PakError(Exception):
    pass


def id_valid(block):
    words = struct.unpack(">16H", block)
    s = sum(words[:14]) & 0xFFFF
    return words[14] == s and words[15] == (0xFFF2 - s) & 0xFFFF


def make_id(seed=1):
    """An ID block: a serial number, device 1, one bank, version 0."""
    b = bytearray(32)
    for i in range(0x18):
        b[i] = (seed * 37 + i * 91 + 13) & 0xFF
    b[0x18:0x1A] = b"\x00\x01"
    b[0x1A] = 1
    s = sum(struct.unpack(">14H", bytes(b[:0x1C]))) & 0xFFFF
    b[0x1C:0x20] = struct.pack(">HH", s, (0xFFF2 - s) & 0xFFFF)
    return bytes(b)


def index_sum(pak, base=PAGE):
    return sum(pak[base + p * 2 + 1] for p in range(FIRST_DATA, PAGES)) & 0xFF


def format_pak(pak=None):
    """Formats a pak: four ID blocks, two index tables of free pages, an empty note table."""
    pak = pak if pak is not None else bytearray(PAK_SIZE)
    pak[:] = bytes(PAK_SIZE)
    for off in (0x20, 0x60, 0x80, 0xC0):
        pak[off:off + 32] = make_id()
    for base in (PAGE, 2 * PAGE):
        for p in range(FIRST_DATA, PAGES):
            pak[base + p * 2:base + p * 2 + 2] = struct.pack(">H", FREE)
        pak[base + 1] = index_sum(pak, base)
    return pak


def check_format(pak):
    """Raises PakError unless the pak has a valid ID area and two equal, valid index tables."""
    for off in (0x20, 0x60, 0x80, 0xC0):
        if not id_valid(bytes(pak[off:off + 32])):
            raise PakError(f"the ID block at {off:#x} is not valid")
    if pak[0x20 + 0x1A] != 1:
        raise PakError("the pak does not have one bank")
    for base in (PAGE, 2 * PAGE):
        if pak[base + 1] != index_sum(pak, base):
            raise PakError(f"the index table at {base:#x} has a wrong sum")
    if pak[PAGE:2 * PAGE] != pak[2 * PAGE:3 * PAGE]:
        raise PakError("the index table and its backup differ")


def index_entry(pak, page):
    return struct.unpack(">H", pak[PAGE + page * 2:PAGE + page * 2 + 2])[0]


def free_pages(pak):
    return sum(index_entry(pak, p) == FREE for p in range(FIRST_DATA, PAGES))


def note_entries(pak):
    """The used entries of the note table: slot -> (game code, company code, first page, status, name bytes)."""
    out = {}
    for s in range(NOTES):
        e = bytes(pak[3 * PAGE + s * 32:3 * PAGE + s * 32 + 32])
        code, company = e[0:4], e[4:6]
        if any(code) and any(company):
            out[s] = (code, company, struct.unpack(">H", e[6:8])[0], e[8], e[0x10:0x20])
    return out


def chain(pak, first):
    """The pages of a note from its first page, following the index table."""
    pages = []
    p = first
    while p != LAST:
        if not FIRST_DATA <= p < PAGES or p in pages:
            raise PakError(f"the chain from page {first} is broken at {p}")
        pages.append(p)
        p = index_entry(pak, p)
    return pages


def note_data(pak, first):
    return b"".join(bytes(pak[p * PAGE:(p + 1) * PAGE]) for p in chain(pak, first))


def decode_name(raw):
    out = ""
    for c in raw:
        if c == 0:
            break
        out += " " if c == 0x0F else chr(ord("0") + c - 0x10) if 0x10 <= c <= 0x19 else chr(ord("A") + c - 0x1A)
    return out


def encode_name(text):
    raw = bytearray(16)
    for i, ch in enumerate(text):
        raw[i] = 0x0F if ch == " " else 0x10 + ord(ch) - ord("0") if ch.isdigit() else 0x1A + ord(ch) - ord("A")
    return bytes(raw)


def add_note(pak, code, company, name, data):
    """Adds a note of the pages that DATA needs, taken from the free pages in a scattered order (every other one,
    to make the chain jump). Returns its slot."""
    n = max(1, (len(data) + PAGE - 1) // PAGE)
    free = [p for p in range(FIRST_DATA, PAGES) if index_entry(pak, p) == FREE]
    pages = free[::2][:n] if len(free[::2]) >= n else free[:n]
    slot = next(s for s in range(NOTES) if not any(pak[3 * PAGE + s * 32:3 * PAGE + s * 32 + 4]))
    for i, p in enumerate(pages):
        chunk = data[i * PAGE:(i + 1) * PAGE].ljust(PAGE, b"\0")
        pak[p * PAGE:(p + 1) * PAGE] = chunk
        nxt = pages[i + 1] if i + 1 < len(pages) else LAST
        for base in (PAGE, 2 * PAGE):
            pak[base + p * 2:base + p * 2 + 2] = struct.pack(">H", nxt)
    for base in (PAGE, 2 * PAGE):
        pak[base + 1] = index_sum(pak, base)
    entry = bytearray(32)
    entry[0:4], entry[4:6] = code, company
    entry[6:8] = struct.pack(">H", pages[0])
    entry[8] = 2
    entry[0x10:0x20] = encode_name(name)
    pak[3 * PAGE + slot * 32:3 * PAGE + slot * 32 + 32] = entry
    return slot


def decode_practice(data):
    """Decodes the practice note's data (cpak.h): returns a dict, or raises PakError."""
    if data[:4] != NOTE_CODE:
        raise PakError("the note's magic is wrong")
    version, parts, used, stored = struct.unpack(">BBHI", data[4:12])
    settings_size, show_game, trainers, best_count = struct.unpack(">HBBH", data[0x0C:0x12])
    frames, runs = struct.unpack(">II", data[0x14:0x1C])
    body = bytearray(data[:used])
    body[8:12] = bytes(4)
    s = int.from_bytes(NOTE_CODE, "big")
    for b in body:
        s = (((s << 1) | (s >> 31)) & 0xFFFFFFFF) ^ b
    if s != stored:
        raise PakError("the note's sum is wrong")
    at = 0x20
    settings = data[at:at + settings_size]
    at += settings_size
    bests = list(struct.unpack(f">{best_count}H", data[at:at + best_count * 2]))
    at += best_count * 2
    rec = []
    if parts & 4:
        for r in range(runs):
            button, x, y, n = struct.unpack(">Hbb", data[at:at + 4]) + (data[at + 4],)
            rec += [(button, x, y)] * n
            at += RUN_SIZE
        if len(rec) != frames:
            raise PakError("the recording has the wrong number of frames")
    if at > used:
        raise PakError("the note's parts are longer than its length")
    return {"version": version, "parts": parts, "used": used, "settings": bytes(settings), "showGame": show_game,
            "trainers": trainers, "bests": bests, "recording": rec}


def read_pak(path, port=0):
    data = bytearray(open(path, "rb").read())
    return bytearray(data[port * PAK_SIZE:(port + 1) * PAK_SIZE])


def write_pak(path, pak, port=0):
    """Writes PAK as controller PORT's pak of the file at PATH, which becomes 4 paks long."""
    try:
        data = bytearray(open(path, "rb").read())
    except FileNotFoundError:
        data = bytearray()
    data += bytes(4 * PAK_SIZE - len(data))
    data[port * PAK_SIZE:(port + 1) * PAK_SIZE] = pak
    open(path, "wb").write(data)
