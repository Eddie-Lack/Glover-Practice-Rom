"""Disk images for the FAT tests (fathost.py, sdcard.py and the other SD card tests): a formatter (mkfs.fat if it is
installed, else a small one of our own), a builder that puts files and odd directory entries into an image, and a
reader written from the FAT specification. The reader shares no code with practice/src/fat.c. FAT16 and FAT32 with
512-byte sectors and 8.3 names."""
import os
import shutil
import struct
import subprocess

SECTOR = 512


def _mbr(total, start, kind, before=0):
    """An MBR with the FAT volume in its first slot, or, if BEFORE is more than 0, in its second slot, behind a
    Linux partition of BEFORE sectors that starts at 2048."""
    sector = bytearray(SECTOR)
    if before:
        sector[446:446 + 16] = struct.pack("<BBBBBBBBII", 0, 0xFE, 0xFF, 0xFF, 0x83, 0xFE, 0xFF, 0xFF, 2048, before)
        sector[462:462 + 16] = struct.pack("<BBBBBBBBII", 0, 0xFE, 0xFF, 0xFF, kind, 0xFE, 0xFF, 0xFF, start, total - start)
    else:
        sector[446:446 + 16] = struct.pack("<BBBBBBBBII", 0, 0xFE, 0xFF, 0xFF, kind, 0xFE, 0xFF, 0xFF, start, total - start)
    sector[510:512] = b"\x55\xaa"
    return bytes(sector)


def format_image(path, megabytes, fat_type, spc, partition=True, tool=True, first_partition_mb=0):
    """Makes an image of MEGABYTES with one FAT volume. FAT_TYPE is 16 or 32, SPC the sectors in a cluster. With
    PARTITION the volume starts at sector 2048 behind an MBR. With FIRST_PARTITION_MB it is the second partition, behind
    a Linux partition of that size. TOOL=False forces our own formatter. Returns the volume's first sector."""
    total = megabytes * 2048
    before = first_partition_mb * 2048
    start = (2048 + before if before else 2048) if partition else 0
    with open(path, "wb") as f:
        f.truncate(total * SECTOR)
    if tool and shutil.which("mkfs.fat"):
        if partition:
            with open(path, "r+b") as f:
                f.write(_mbr(total, start, 0x0C if fat_type == 32 else 0x06, before))
        cmd = ["mkfs.fat", "-F", str(fat_type), "-s", str(spc), "--invariant", "-n", "PRACTICE"]
        if partition:
            cmd += [f"--offset={start}"]
        cmd += [path, str((total - start) // 2)]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return start
    volume = total - start
    if fat_type == 32:
        reserved, root_entries = 32, 0
        clusters = (volume - reserved) // spc
        fat_sectors = 1
        for _ in range(8):
            clusters = (volume - reserved - 2 * fat_sectors) // spc
            fat_sectors = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
        if clusters < 65525:
            raise ValueError("too few clusters for FAT32")
    else:
        reserved, root_entries = 1, 512
        root_sectors = root_entries * 32 // SECTOR
        fat_sectors = 1
        for _ in range(8):
            clusters = (volume - reserved - root_sectors - 2 * fat_sectors) // spc
            fat_sectors = ((clusters + 2) * 2 + SECTOR - 1) // SECTOR
        if not 4085 <= clusters < 65525:
            raise ValueError("wrong cluster count for FAT16")
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xeb\x58\x90" if fat_type == 32 else b"\xeb\x3c\x90"
    boot[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, spc, reserved, 2, root_entries,
                     volume if volume < 65536 and fat_type == 16 else 0, 0xF8,
                     fat_sectors if fat_type == 16 else 0, 63, 255, start, 0 if (volume < 65536 and fat_type == 16) else volume)
    if fat_type == 32:
        struct.pack_into("<IHHIHH12sBBBI11s8s", boot, 36, fat_sectors, 0, 0, 2, 1, 6, b"", 0x80, 0, 0x29, 0x1234ABCD,
                         b"NO NAME    ", b"FAT32   ")
    else:
        struct.pack_into("<BBBI11s8s", boot, 36, 0x80, 0, 0x29, 0x1234ABCD, b"NO NAME    ", b"FAT16   ")
    boot[510:512] = b"\x55\xaa"
    with open(path, "r+b") as f:
        if partition:
            f.write(_mbr(total, start, 0x0C if fat_type == 32 else 0x06, before))
        f.seek(start * SECTOR)
        f.write(boot)
        fat = bytearray(fat_sectors * SECTOR)
        if fat_type == 32:
            struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0x0FFFFFFF, 0x0FFFFFFF)
            info = bytearray(SECTOR)
            struct.pack_into("<I", info, 0, 0x41615252)
            struct.pack_into("<IIII", info, 484, 0x61417272, 0xFFFFFFFF, 0xFFFFFFFF, 0)
            info[508:512] = b"\x00\x00\x55\xaa"
            f.seek((start + 1) * SECTOR)
            f.write(info)
            f.seek((start + 6) * SECTOR)
            f.write(boot)
            f.seek((start + 7) * SECTOR)
            f.write(info)
        else:
            struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
        for k in range(2):
            f.seek((start + reserved + k * fat_sectors) * SECTOR)
            f.write(fat)
    return start


class Image:
    """A reader written from the FAT specification. It never writes."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        sector0 = self.sector(0)
        if sector0[510:512] != b"\x55\xaa":
            raise ValueError("no boot signature")
        start = 0
        if struct.unpack_from("<H", sector0, 11)[0] != SECTOR or sector0[0] not in (0xEB, 0xE9):
            for i in range(4):
                kind, lba = sector0[446 + 16 * i + 4], struct.unpack_from("<I", sector0, 446 + 16 * i + 8)[0]
                if kind in (4, 6, 0xB, 0xC, 0xE) and lba:
                    start = lba
                    break
            else:
                raise ValueError("no FAT partition")
        b = self.sector(start)
        (bps, self.spc, reserved, nfats, root_entries, total16) = struct.unpack_from("<HBHBHH", b, 11)
        fat16 = struct.unpack_from("<H", b, 22)[0]
        total32, fat32 = struct.unpack_from("<I", b, 32)[0], struct.unpack_from("<I", b, 36)[0]
        assert bps == SECTOR
        self.fat_sectors = fat16 or fat32
        total = total16 or total32
        root_sectors = (root_entries * 32 + SECTOR - 1) // SECTOR
        self.fat_start = start + reserved
        self.root_start = self.fat_start + nfats * self.fat_sectors
        self.data_start = self.root_start + root_sectors
        self.clusters = (total - (self.data_start - start)) // self.spc
        self.kind = 16 if self.clusters < 65525 else 32
        self.root_cluster = struct.unpack_from("<I", b, 44)[0] if self.kind == 32 else 0
        self.root_sectors = root_sectors
        self.nfats = nfats
        self.volume = start
        # FAT32 ExtFlags: with bit 7 set the FAT is not mirrored and only the FAT of bits 0 to 3 is in use.
        ext = struct.unpack_from("<H", b, 40)[0] if self.kind == 32 else 0
        self.mirrored = not ext & 0x80
        self.active = 0 if self.mirrored else ext & 0x0F
        self.fats = [self.raw(self.fat_start + k * self.fat_sectors, self.fat_sectors) for k in range(nfats)]
        self.fat = self.fats[self.active]

    def raw(self, lba, n):
        self.f.seek(lba * SECTOR)
        return self.f.read(n * SECTOR)

    def sector(self, lba):
        return self.raw(lba, 1)

    def entry(self, c):
        if self.kind == 32:
            return struct.unpack_from("<I", self.fat, c * 4)[0] & 0x0FFFFFFF
        return struct.unpack_from("<H", self.fat, c * 2)[0]

    def is_end(self, v):
        return v >= (0x0FFFFFF8 if self.kind == 32 else 0xFFF8)

    def chain(self, c):
        out = []
        seen = set()
        while c >= 2 and not self.is_end(c):
            assert c <= self.clusters + 1, f"cluster {c} out of range"
            assert c not in seen, "the chain loops"
            seen.add(c)
            out.append(c)
            c = self.entry(c)
        return out

    def cluster_data(self, c):
        return self.raw(self.data_start + (c - 2) * self.spc, self.spc)

    def dir_bytes(self, cluster):
        if cluster == 0 and self.kind == 16:
            return self.raw(self.root_start, self.root_sectors)
        return b"".join(self.cluster_data(c) for c in self.chain(cluster or self.root_cluster))

    def listdir(self, cluster=0):
        """{name: (is_dir, first cluster, size)} for the short names of a directory."""
        data = self.dir_bytes(cluster)
        out = {}
        for off in range(0, len(data), 32):
            e = data[off:off + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F or e[11] & 0x08:
                continue
            name = e[0:8].decode("latin1").rstrip() + ("." + e[8:11].decode("latin1").rstrip() if e[8:11] != b"   " else "")
            first = struct.unpack_from("<H", e, 26)[0] | (struct.unpack_from("<H", e, 20)[0] << 16 if self.kind == 32 else 0)
            out[name] = (bool(e[11] & 0x10), first, struct.unpack_from("<I", e, 28)[0])
        return out

    def label(self):
        """The volume label in the root directory, or None."""
        data = self.dir_bytes(0)
        for off in range(0, len(data), 32):
            e = data[off:off + 32]
            if e[0] == 0:
                break
            if e[0] != 0xE5 and (e[11] & 0x0F) != 0x0F and e[11] & 0x08:
                return e[0:11].decode("latin1").rstrip()
        return None

    def read(self, first, size):
        data = b"".join(self.cluster_data(c) for c in self.chain(first))
        assert len(data) >= size, "the chain is shorter than the file"
        assert size == 0 or len(self.chain(first)) == -(-size // (self.spc * SECTOR)), "the chain has the wrong length"
        return data[:size]

    def lookup(self, *names):
        cluster = 0
        for name in names[:-1]:
            is_dir, cluster, _ = self.listdir(cluster)[name]
            assert is_dir
        return self.listdir(cluster)[names[-1]] if names else (True, 0, 0)

    def used_clusters(self):
        return sum(1 for c in range(2, self.clusters + 2) if self.entry(c) != 0)

    def referenced_clusters(self, cluster=0, seen=None):
        """Every cluster that a file or folder reaches from the root: the set must equal the used ones."""
        seen = set() if seen is None else seen
        if cluster:
            seen.update(self.chain(cluster))
        elif self.kind == 32:
            seen.update(self.chain(self.root_cluster))
        for name, (is_dir, first, _) in self.listdir(cluster).items():
            if name in (".", ".."):
                continue
            if is_dir:
                self.referenced_clusters(first, seen)
            else:
                seen.update(self.chain(first))
        return seen

    def check(self, allow_leaks=False):
        """Fails on a damaged volume: the two FATs differ, a cluster is used but unreachable, or a cluster is reached
        but not marked. ALLOW_LEAKS accepts the first two (the state after a failed write)."""
        used = {c for c in range(2, self.clusters + 2) if self.entry(c) != 0}
        reached = self.referenced_clusters()
        assert reached <= used, f"{len(reached - used)} clusters of files are not marked as used"
        if not allow_leaks:
            assert not self.mirrored or all(f == self.fats[0] for f in self.fats), "the FAT copies differ"
            assert used == reached, f"{len(used - reached)} lost clusters"

    def shared_clusters(self, cluster=0):
        """The clusters that two chains of the tree reach."""
        seen, shared = set(), set()

        def walk(c):
            chain = self.chain(c) if c else (self.chain(self.root_cluster) if self.kind == 32 else [])
            for x in chain:
                (shared if x in seen else seen).add(x)
            for name, (is_dir, first, _) in self.listdir(c).items():
                if name in (".", ".."):
                    continue
                if is_dir:
                    walk(first)
                elif first:
                    for x in self.chain(first):
                        (shared if x in seen else seen).add(x)

        walk(cluster)
        return shared

    def close(self):
        self.f.close()


def fsck(path, offset_sectors=0):
    """Runs fsck.fat on the image (read only) if it is installed. Returns None if not, else (exit code, output).
    fsck.fat needs the volume to start at the image's start. A partitioned image is cut first."""
    tool = shutil.which("fsck.fat")
    if tool is None:
        return None
    target = path
    if offset_sectors:
        target = path + ".volume"
        with open(path, "rb") as src, open(target, "wb") as dst:
            src.seek(offset_sectors * SECTOR)
            shutil.copyfileobj(src, dst)
    proc = subprocess.run([tool, "-n", "-v", target], capture_output=True, text=True)
    if target != path:
        os.remove(target)
    return proc.returncode, proc.stdout + proc.stderr


def _short_checksum(short11):
    total = 0
    for b in short11:
        total = (((total & 1) << 7) + (total >> 1) + b) & 0xFF
    return total


def lfn_entries(long_name, short11):
    """The long-name entries (attribute 0x0F) for LONG_NAME in front of the entry of SHORT11, last part first."""
    chars = [ord(c) for c in long_name] + [0]
    while len(chars) % 13:
        chars.append(0xFFFF)
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    check = _short_checksum(short11)
    out = []
    for i, part in enumerate(parts):
        e = bytearray(32)
        e[0] = (i + 1) | (0x40 if i == len(parts) - 1 else 0)
        struct.pack_into("<5H", e, 1, *part[0:5])
        e[11] = 0x0F
        e[13] = check
        struct.pack_into("<6H", e, 14, *part[5:11])
        struct.pack_into("<2H", e, 28, *part[11:13])
        out.append(bytes(e))
    return out[::-1]


def short_name(name):
    """The 11 bytes of an 8.3 name ("STATE001.GST")."""
    base, _, ext = name.partition(".")
    return (base.upper().ljust(8) + ext.upper().ljust(3)).encode("latin1")


class Builder:
    """Puts files, folders and odd directory entries into a formatted image, before the code under test runs: files in
    any folder, long-name entries, deleted entries, a volume label, a directory that fills its last cluster, the FSInfo
    block's fields and the FAT32 ExtFlags. It writes the FAT to every copy at close()."""

    def __init__(self, path):
        probe = Image(path)
        self.kind, self.spc = probe.kind, probe.spc
        self.fat_start, self.fat_sectors, self.nfats = probe.fat_start, probe.fat_sectors, probe.nfats
        self.root_start, self.root_sectors, self.data_start = probe.root_start, probe.root_sectors, probe.data_start
        self.root_cluster, self.clusters, self.volume = probe.root_cluster, probe.clusters, probe.volume
        self.fat = bytearray(probe.fat)
        probe.close()
        self.f = open(path, "r+b")
        self.next_free = 3
        self.cluster_bytes = self.spc * SECTOR

    # -- the FAT
    def get(self, c):
        if self.kind == 32:
            return struct.unpack_from("<I", self.fat, c * 4)[0] & 0x0FFFFFFF
        return struct.unpack_from("<H", self.fat, c * 2)[0]

    def put(self, c, v):
        if self.kind == 32:
            old = struct.unpack_from("<I", self.fat, c * 4)[0]
            struct.pack_into("<I", self.fat, c * 4, (old & 0xF0000000) | v)
        else:
            struct.pack_into("<H", self.fat, c * 2, v)

    def end(self):
        return 0x0FFFFFFF if self.kind == 32 else 0xFFFF

    def alloc(self, count, scatter=0):
        """A chain of COUNT clusters, from the first free one. With SCATTER, every SCATTER-th free cluster is skipped."""
        taken = []
        c = self.next_free
        while len(taken) < count:
            assert c <= self.clusters + 1, "the image is full"
            if self.get(c) == 0:
                taken.append(c)
                if scatter:
                    c += scatter
            c += 1
        for a, b in zip(taken, taken[1:]):
            self.put(a, b)
        self.put(taken[-1], self.end())
        self.next_free = taken[-1] + 1 if not scatter else self.next_free
        return taken

    def cluster_lba(self, c):
        return self.data_start + (c - 2) * self.spc

    def write(self, lba, data):
        self.f.seek(lba * SECTOR)
        self.f.write(data)

    def write_chain(self, chain, data):
        for i, c in enumerate(chain):
            part = data[i * self.cluster_bytes:(i + 1) * self.cluster_bytes]
            self.write(self.cluster_lba(c), part.ljust(self.cluster_bytes, b"\0"))

    # -- directories
    def _dir_slots(self, cluster):
        """(lba, offset) of every entry slot of a directory, and the chain of its clusters."""
        if cluster == 0 and self.kind == 16:
            return [(self.root_start + i // 16, (i % 16) * 32) for i in range(self.root_sectors * 16)], []
        chain = []
        c = cluster or self.root_cluster
        while c < (0x0FFFFFF8 if self.kind == 32 else 0xFFF8):
            chain.append(c)
            c = self.get(c)
        return [(self.cluster_lba(c) + k, o) for c in chain for k in range(self.spc) for o in range(0, SECTOR, 32)], chain

    def _read_entry(self, lba, off):
        self.f.seek(lba * SECTOR + off)
        return self.f.read(32)

    def add_entries(self, cluster, entries):
        """Puts the 32-byte ENTRIES (a long name and its short entry) in the first run of free slots that is long enough,
        and makes the directory one cluster longer if it has none."""
        while True:
            slots, chain = self._dir_slots(cluster)
            run = []
            for lba, off in slots:
                first = self._read_entry(lba, off)[0]
                if first in (0, 0xE5):
                    run.append((lba, off))
                    if len(run) == len(entries):
                        break
                else:
                    run = []
            if len(run) == len(entries):
                for (lba, off), e in zip(run, entries):
                    self.f.seek(lba * SECTOR + off)
                    self.f.write(e)
                return
            if not chain:
                raise ValueError("the fixed root directory is full")
            new = self.alloc(1)[0]
            self.put(chain[-1], new)
            self.write_chain([new], b"")

    def entry(self, name11, attr, first, size, deleted=False):
        e = bytearray(32)
        e[0:11] = name11
        e[11] = attr
        struct.pack_into("<H", e, 20, first >> 16)
        struct.pack_into("<HHH", e, 22, 0x6000, 0x5A21, first & 0xFFFF)
        struct.pack_into("<I", e, 28, size)
        if deleted:
            e[0] = 0xE5
        return bytes(e)

    def add_file(self, cluster, name, data, long_name=None, deleted=False, scatter=0):
        short = short_name(name)
        chain = self.alloc(max(1, -(-len(data) // self.cluster_bytes)), scatter) if data else []
        if data:
            self.write_chain(chain, data)
        entries = [self.entry(short, 0x20, chain[0] if chain else 0, len(data), deleted)]
        if long_name:
            lfn = lfn_entries(long_name, short)
            if deleted:
                lfn = [bytes([0xE5]) + e[1:] for e in lfn]
            entries = lfn + entries
        if deleted and chain:
            for c in chain:      # the clusters of a deleted file are free
                self.put(c, 0)
        self.add_entries(cluster, entries)

    def add_folder(self, cluster, name):
        c = self.alloc(1)[0]
        self.write_chain([c], b"")
        self.write(self.cluster_lba(c), (self.entry(b".          ", 0x10, c, 0) + self.entry(b"..         ", 0x10, cluster, 0)).ljust(SECTOR, b"\0"))
        self.add_entries(cluster, [self.entry(short_name(name), 0x10, c, 0)])
        return c

    def add_label(self, label):
        """A volume label in the root directory and in the boot sector, unless the formatter made one."""
        slots, _ = self._dir_slots(0)
        for lba, off in slots:
            e = self._read_entry(lba, off)
            if e[0] != 0xE5 and e[0] != 0 and (e[11] & 0x0F) != 0x0F and e[11] & 0x08:
                return
        text = label.upper().ljust(11).encode("latin1")
        self.add_entries(0, [self.entry(text, 0x08, 0, 0)])
        for sector in (self.volume, self.volume + 6) if self.kind == 32 else (self.volume,):
            self.f.seek(sector * SECTOR)
            boot = bytearray(self.f.read(SECTOR))
            boot[(71 if self.kind == 32 else 43):(71 if self.kind == 32 else 43) + 11] = text
            self.write(sector, bytes(boot))

    def fill_directory(self, cluster, to_multiple_of_cluster=True, prefix="F"):
        """Fills the directory with small files until every slot of its clusters is in use."""
        i = 0
        while True:
            slots, chain = self._dir_slots(cluster)
            free = sum(1 for lba, off in slots if self._read_entry(lba, off)[0] in (0, 0xE5))
            if free == 0:
                return
            self.add_file(cluster, f"{prefix}{i:05d}.DAT", bytes([i & 255]) * 10)
            i += 1

    def set_fsinfo(self, next_free, free_count=1234):
        sector = self.volume + 1
        self.f.seek(sector * SECTOR)
        info = bytearray(self.f.read(SECTOR))
        assert info[0:4] == b"RRaA", "no FSInfo block"
        struct.pack_into("<II", info, 488, free_count, next_free)
        self.write(sector, bytes(info))

    def set_ext_flags(self, active):
        """FAT32: the FAT is not mirrored and FAT number ACTIVE is in use. The other copies are made stale: zeros."""
        self.f.seek(self.volume * SECTOR)
        boot = bytearray(self.f.read(SECTOR))
        struct.pack_into("<H", boot, 40, 0x80 | active)
        self.write(self.volume, bytes(boot))
        self.write(self.volume + 6, bytes(boot))
        self.ext_active = active

    def close(self):
        ext = getattr(self, "ext_active", None)
        for k in range(self.nfats):
            if ext is not None and k != ext:
                stale = bytearray(len(self.fat))
                stale[0:12] = self.fat[0:12]
                self.write(self.fat_start + k * self.fat_sectors, bytes(stale))
            else:
                self.write(self.fat_start + k * self.fat_sectors, bytes(self.fat))
        self.f.close()


def make_large_card(path):
    """A populated large card, for the time of a first save: a 2200 MB FAT32 image (a sparse file) with clusters of 4 KB,
    2000 files with long names in the root directory, 12 folders of 100 files each, and a file of 1.2 GB that uses the first
    300,000 clusters (only its FAT chain is written). A save has to find free clusters behind all of them."""
    if os.path.exists(path):
        os.remove(path)
    format_image(path, 2200, 32, 8, partition=False, tool=True)
    b = Builder(path)
    try:
        for i in range(2000):
            b.add_file(0, f"F{i:05d}.DAT", bytes([i & 255]) * 700, long_name=f"File number {i}.dat")
        for k in range(12):
            d = b.add_folder(0, f"DIR{k:02d}")
            for i in range(100):
                b.add_file(d, f"G{i:05d}.DAT", bytes([i]) * 300)
        chain = b.alloc(300000)
        b.add_entries(0, [b.entry(short_name("HUGE.BIN"), 0x20, chain[0], 300000 * b.cluster_bytes)])
    finally:
        b.close()
