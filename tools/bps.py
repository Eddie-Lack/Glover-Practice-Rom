#!/usr/bin/env python3
"""Make and apply BPS patches.

    bps.py make SOURCE TARGET PATCH
    bps.py apply SOURCE PATCH TARGET

BPS is the patch format of byuu's beat. A patch holds the sizes of both files, a list of actions that build the
target from the source and from itself, and the CRC32 of the source, the target and the patch. An action is one
of these:

- SourceRead: copy the bytes at the same offset in the source.
- TargetRead: the bytes follow in the patch.
- SourceCopy: copy bytes from anywhere in the source.
- TargetCopy: copy bytes already written to the target (this also repeats a run of bytes).

The maker is greedy. At each place in the target it uses the longest of: the source at the same offset, a match
in the source found through a table of 32-byte blocks, or a repeat of the last target byte. It is fast enough for
the practice ROM, where most bytes are unchanged or moved in large blocks.
"""
import sys
import zlib

MAGIC = b"BPS1"
SOURCE_READ, TARGET_READ, SOURCE_COPY, TARGET_COPY = range(4)
BLOCK = 32
MIN_COPY = 16


def encode_number(n):
    out = bytearray()
    while True:
        x = n & 0x7F
        n >>= 7
        if n == 0:
            out.append(0x80 | x)
            return out
        out.append(x)
        n -= 1


def decode_number(data, pos):
    n, shift = 0, 1
    while True:
        x = data[pos]
        pos += 1
        n += (x & 0x7F) * shift
        if x & 0x80:
            return n, pos
        shift <<= 7
        n += shift


def encode_signed(n):
    return encode_number(abs(n) << 1 | (1 if n < 0 else 0))


def match_length(a, ai, b, bi, limit):
    """The number of equal bytes at a[ai:] and b[bi:], up to LIMIT. Compares in large steps first."""
    n = 0
    step = 4096
    while step:
        while n + step <= limit and a[ai + n:ai + n + step] == b[bi + n:bi + n + step]:
            n += step
        step //= 8 if step > 8 else 2
    return n


def make(source, target):
    index = {}
    for i in range(0, len(source) - BLOCK + 1, BLOCK):
        index.setdefault(source[i:i + BLOCK], i)

    out = bytearray(MAGIC)
    out += encode_number(len(source)) + encode_number(len(target)) + encode_number(0)
    literal = bytearray()
    source_rel = target_rel = 0

    def flush():
        nonlocal literal
        if literal:
            out.extend(encode_number((len(literal) - 1) << 2 | TARGET_READ))
            out.extend(literal)
            literal = bytearray()

    pos = 0
    while pos < len(target):
        left = len(target) - pos
        best, kind, where = 0, None, 0
        if pos < len(source):
            n = match_length(source, pos, target, pos, min(left, len(source) - pos))
            if n >= 4:
                best, kind = n, SOURCE_READ
        if best < 4096:
            # A source block that matches the target here may start at any of the next BLOCK target bytes'
            # alignment; try each offset back to the block start.
            for back in range(BLOCK):
                src = index.get(bytes(target[pos + back:pos + back + BLOCK])) if back <= left - BLOCK else None
                if src is not None and src >= back:
                    s = src - back
                    n = match_length(source, s, target, pos, min(left, len(source) - s))
                    if n > best:
                        best, kind, where = n, SOURCE_COPY, s
                    break
        if pos > 0 and target[pos] == target[pos - 1]:
            n = match_length(target, pos - 1, target, pos, left) if left else 0
            if n > best:
                best, kind, where = n, TARGET_COPY, pos - 1
        if kind is None or (kind != SOURCE_READ and best < MIN_COPY):
            literal.append(target[pos])
            pos += 1
            continue
        flush()
        out += encode_number((best - 1) << 2 | kind)
        if kind == SOURCE_COPY:
            out += encode_signed(where - source_rel)
            source_rel = where + best
        elif kind == TARGET_COPY:
            out += encode_signed(where - target_rel)
            target_rel = where + best
        pos += best
    flush()
    out += zlib.crc32(source).to_bytes(4, "little") + zlib.crc32(target).to_bytes(4, "little")
    out += zlib.crc32(out).to_bytes(4, "little")
    return bytes(out)


def apply(source, patch):
    if patch[:4] != MAGIC:
        raise ValueError("not a BPS patch")
    if zlib.crc32(patch[:-4]) != int.from_bytes(patch[-4:], "little"):
        raise ValueError("the patch is damaged")
    if zlib.crc32(source) != int.from_bytes(patch[-12:-8], "little"):
        raise ValueError("the patch is for a different source file")
    pos = 4
    source_size, pos = decode_number(patch, pos)
    target_size, pos = decode_number(patch, pos)
    meta_size, pos = decode_number(patch, pos)
    pos += meta_size
    if source_size != len(source):
        raise ValueError("the source file has the wrong size")
    target = bytearray()
    source_rel = target_rel = 0
    end = len(patch) - 12
    while pos < end:
        data, pos = decode_number(patch, pos)
        kind, length = data & 3, (data >> 2) + 1
        if kind == SOURCE_READ:
            start = len(target)
            target += source[start:start + length]
        elif kind == TARGET_READ:
            target += patch[pos:pos + length]
            pos += length
        else:
            offset, pos = decode_number(patch, pos)
            offset = -(offset >> 1) if offset & 1 else offset >> 1
            if kind == SOURCE_COPY:
                source_rel += offset
                target += source[source_rel:source_rel + length]
                source_rel += length
            else:
                target_rel += offset
                for _ in range(length):  # byte by byte: the copy may overlap what it writes
                    target.append(target[target_rel])
                    target_rel += 1
    if len(target) != target_size or zlib.crc32(target) != int.from_bytes(patch[-8:-4], "little"):
        raise ValueError("the patched file is wrong")
    return bytes(target)


def main():
    if len(sys.argv) != 5 or sys.argv[1] not in ("make", "apply"):
        sys.exit(__doc__.split("\n\n")[1])
    a, b = open(sys.argv[2], "rb").read(), open(sys.argv[3], "rb").read()
    if sys.argv[1] == "make":
        patch = make(a, b)
        if apply(a, patch) != b:
            sys.exit("bps.py: the patch does not rebuild the target")
        open(sys.argv[4], "wb").write(patch)
        print(f"{sys.argv[4]}: {len(patch)} bytes")
    else:
        open(sys.argv[4], "wb").write(apply(a, b))


if __name__ == "__main__":
    main()
