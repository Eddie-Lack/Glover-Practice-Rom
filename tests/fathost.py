#!/usr/bin/env python3
"""Builds tests/fathost.c (the FAT code, practice/src/fat.c, for the host) and runs its scenarios on disk images: FAT32
and FAT16, with and without a partition table, made by mkfs.fat when it is installed and by tests/fatimage.py's
formatter otherwise. After each scenario the image is read by tests/fatimage.py's reader (which shares no code with
fat.c): every file must hold what the scenario wrote, the folder must list no more, no cluster may be lost, and
fsck.fat, if installed, must find nothing wrong."""
import os
import shutil
import subprocess
import sys
import tempfile

import fatimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(ROOT, "build", "tests", "fathost")

def case(scenario, mb, fat_type, spc, partition=True, own=False, prep=None, first=0, scratch=None):
    return dict(scenario=scenario, mb=mb, fat_type=fat_type, spc=spc, partition=partition, own=own, prep=prep, first=first,
                scratch=scratch)


# The images that cases start from (the PREP functions below): odd directories that are there before the code runs.
CASES = [
    case("basic", 64, 32, 1),
    case("basic", 128, 32, 2, partition=False),
    case("basic", 32, 16, 2),
    case("basic", 64, 32, 1, own=True),
    case("basic", 32, 16, 2, partition=False, own=True),
    case("full", 40, 32, 1),
    case("full", 20, 16, 1),
    case("fragment", 40, 32, 1),
    case("fragment", 20, 16, 1, partition=False),
    case("powerfail", 40, 32, 1),
    case("powerfail", 20, 16, 2),
    case("powerfail", 40, 32, 1, own=True),
    # Clusters of 4 to 32 KB. The images of the larger ones are sparse files.
    case("basic", 24, 16, 8, partition=False),
    case("basic", 48, 16, 16, partition=False),
    case("basic", 90, 16, 32, partition=False),
    case("basic", 150, 16, 64, partition=False),
    case("basic", 300, 32, 8, partition=False),
    case("basic", 2200, 32, 64, partition=False),
    case("basic", 2200, 32, 64, partition=False, own=True),
    # No scratch memory, and an odd amount of it.
    case("basic", 48, 16, 16, scratch=0),
    case("basic", 48, 16, 16, scratch=3),
    # Directories and volumes as other programs leave them.
    case("basic", 64, 32, 1, prep="populated"),
    case("fragment", 32, 16, 2, prep="populated"),
    case("basic", 64, 32, 1, prep="populated", own=True),
    case("dirfull", 32, 16, 2, prep="fullroot"),
    case("fragment", 32, 16, 2, prep="fullroot_with_folder"),
    case("basic", 64, 32, 1, prep="fullroot_clusters"),
    case("fragment", 64, 32, 1, prep="fullroot_clusters_with_folder"),
    case("basic", 64, 32, 1, first=8),
    case("basic", 32, 16, 2, first=8),
    case("basic", 64, 32, 1, prep="fsinfo_used"),
    case("basic", 64, 32, 1, prep="fsinfo_beyond"),
    case("basic", 64, 32, 1, prep="fsinfo_populated", own=True),
    case("badfolder", 32, 16, 2, prep="cluster0_folder"),
    case("badfolder", 64, 32, 1, prep="cluster0_folder"),
    case("loopdir", 32, 16, 2, prep="loop_folder"),
    case("loopdir", 64, 32, 1, prep="loop_folder"),
    case("basic", 40, 32, 1, prep="active_fat", own=True),
    case("fragment", 40, 32, 1, prep="active_fat"),
]


def prep_populated(b, pre):
    """Long names, deleted entries first, a volume label, a folder with a file, files of several clusters."""
    b.add_label("MYCARD")
    for i in range(3):
        b.add_file(0, f"GONE{i}.TXT", fathost_data(50 + i, 2000), long_name=f"Deleted file number {i}.txt", deleted=True)
    for i in range(6):
        name = f"DOC{i}.TXT"
        b.add_file(0, name, pattern(100 + i, 3000 + 1700 * i), long_name=f"A longer file name, number {i}.txt")
        pre[name] = (100 + i, 3000 + 1700 * i)
    saves = b.add_folder(0, "SAVES")
    b.add_file(saves, "KEEP.BIN", pattern(77, 70000), long_name="Keep this one.bin")
    pre["SAVES/KEEP.BIN"] = (77, 70000)


def fathost_data(seed, size):
    return pattern(seed, size)


def _fill_root(b):
    b.fill_directory(0)


def prep_fullroot(b, pre):
    """A FAT16 root directory with every slot in use, and no folder GLOVERPR."""
    b.add_file(0, "KEEP.BIN", pattern(5, 9000))
    pre["KEEP.BIN"] = (5, 9000)
    _fill_root(b)


def prep_fullroot_with_folder(b, pre):
    """The same, with GLOVERPR there and holding an old file: saving still works."""
    folder = b.add_folder(0, "GLOVERPR")
    b.add_file(folder, "STATE001.GST", pattern(1, 30000))
    b.add_file(0, "KEEP.BIN", pattern(5, 9000))
    pre["KEEP.BIN"] = (5, 9000)
    _fill_root(b)


def prep_fullroot_clusters(b, pre):
    """A FAT32 root directory of several clusters whose last cluster is full: the folder needs a new cluster."""
    for i in range(40):
        b.add_file(0, f"KEEP{i:02d}.BIN", pattern(200 + i, 100 + i))
        pre[f"KEEP{i:02d}.BIN"] = (200 + i, 100 + i)
    _fill_root(b)


def prep_fullroot_clusters_with_folder(b, pre):
    prep_fullroot_clusters(b, pre)


def prep_fsinfo_used(b, pre):
    """Files in the first clusters, and an FSInfo hint that points into them."""
    for i in range(20):
        b.add_file(0, f"USED{i:02d}.BIN", pattern(300 + i, 20000 + 500 * i))
        pre[f"USED{i:02d}.BIN"] = (300 + i, 20000 + 500 * i)
    b.set_fsinfo(next_free=10, free_count=7)


def prep_fsinfo_beyond(b, pre):
    """An FSInfo hint that is not a cluster of the volume."""
    b.add_file(0, "USED.BIN", pattern(9, 5000))
    pre["USED.BIN"] = (9, 5000)
    b.set_fsinfo(next_free=0x0FFFFFF0, free_count=0xFFFFFFFF)


def prep_fsinfo_populated(b, pre):
    """Scattered files, deleted files between them, and the hint in the middle."""
    for i in range(30):
        b.add_file(0, f"SC{i:02d}.BIN", pattern(400 + i, 4000 * (i % 5 + 1)), scatter=i % 3)
        pre[f"SC{i:02d}.BIN"] = (400 + i, 4000 * (i % 5 + 1))
    for i in range(10):
        b.add_file(0, f"DEL{i}.BIN", pattern(450 + i, 12000), deleted=True)
    b.set_fsinfo(next_free=b.next_free // 2, free_count=100)


def prep_cluster0_folder(b, pre):
    """A folder entry named GLOVERPR whose first cluster is 0."""
    b.add_entries(0, [b.entry(fatimage.short_name("GLOVERPR"), 0x10, 0, 0)])


def prep_loop_folder(b, pre):
    """GLOVERPR with two clusters that are full of entries, the second leading back to the first."""
    folder = b.add_folder(0, "GLOVERPR")
    b.fill_directory(folder, prefix="A")
    b.add_file(folder, "X.DAT", b"x")
    b.fill_directory(folder, prefix="B")
    chain = b._dir_slots(folder)[1]
    b.put(chain[-1], chain[0])


def prep_active_fat(b, pre):
    """FAT32 whose second FAT is the only one in use (ExtFlags). The first FAT is stale."""
    for i in range(12):
        b.add_file(0, f"FILE{i:02d}.BIN", pattern(500 + i, 15000 + 900 * i))
        pre[f"FILE{i:02d}.BIN"] = (500 + i, 15000 + 900 * i)
    b.set_ext_flags(1)


PREPS = {name[5:]: fn for name, fn in globals().items() if name.startswith("prep_")}
# fsck.fat reads the first FAT of a volume whose FATs are not mirrored, and calls the stale one wrong.
NO_FSCK = {"active_fat"}
# Scenarios on a volume that is damaged on purpose: only the program's own checks count.
DAMAGED = {"loopdir", "badfolder"}

_tables = {}


def pattern(seed, size):
    """The content that fathost.c's Pat() gives: byte i is seed*31 + 7*i + 13*(i>>8) + 5*(i>>16), modulo 256."""
    base = bytes((7 * r) & 255 for r in range(256))
    out = bytearray()
    for j in range(-(-size // 256)):
        add = (seed * 31 + 13 * j + 5 * (j >> 8)) & 255
        if add not in _tables:
            _tables[add] = bytes((x + add) & 255 for x in range(256))
        out += base.translate(_tables[add])
    return bytes(out[:size])


def build():
    os.makedirs(os.path.dirname(EXE), exist_ok=True)
    subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-fsanitize=address,undefined", "-I" + os.path.join(ROOT, "practice", "include"),
                    "-o", EXE, os.path.join(HERE, "fathost.c"), os.path.join(ROOT, "practice", "src", "fat.c")], check=True)


def describe(c):
    return (f"{c['scenario']} FAT{c['fat_type']} {c['mb']}MB spc{c['spc']}{' mbr' if c['partition'] else ''}"
            f"{' second partition' if c['first'] else ''}{' own' if c['own'] else ''}"
            f"{' ' + c['prep'] if c['prep'] else ''}{'' if c['scratch'] is None else ' scratch ' + str(c['scratch'])}")


def run_case(c, folder):
    scenario = c["scenario"]
    label = describe(c)
    path = os.path.join(folder, "disk.img")
    if os.path.exists(path):
        os.remove(path)
    start = fatimage.format_image(path, c["mb"], c["fat_type"], c["spc"], c["partition"], tool=not c["own"],
                                  first_partition_mb=c["first"])
    pre = {}
    stale = None
    label_before = None
    if c["prep"]:
        b = fatimage.Builder(path)
        try:
            PREPS[c["prep"]](b, pre)
        finally:
            b.close()
        probe = fatimage.Image(path)
        label_before = probe.label()
        probe.close()
        if c["prep"] == "populated" and label_before is None:
            raise AssertionError("the prepared volume has no label")
        if c["prep"] == "active_fat":
            probe = fatimage.Image(path)
            stale = probe.fats[0]
            probe.close()
    env = dict(os.environ)
    if c["scratch"] is not None:
        env["FATHOST_SCRATCH"] = str(c["scratch"])
    proc = subprocess.run([EXE, scenario, path], capture_output=True, text=True, env=env, timeout=300)
    if proc.returncode != 0:
        print("FAIL " + label + ": " + (proc.stderr.strip() or proc.stdout.strip())[-300:])
        return False
    expect = {}
    root_extra = None
    for line in proc.stdout.splitlines():
        parts = line.split()
        if parts[0] == "FILE":
            expect[parts[1]] = (int(parts[2]), int(parts[3]))
        elif parts[0] == "ROOT":
            root_extra = (parts[1].upper(), int(parts[2]))
    if scenario in DAMAGED:
        os.remove(path)
        print(f"ok   {label}: the damage is reported, and nothing hangs")
        return True
    img = fatimage.Image(path)
    try:
        img.check()
        if scenario != "dirfull":
            is_dir, folder_cluster, _ = img.lookup("GLOVERPR")
            assert is_dir
            listing = img.listdir(folder_cluster)
            listing = {n: v for n, v in listing.items() if n not in (".", "..")}
            assert set(listing) == set(expect), f"the folder holds {sorted(set(listing) ^ set(expect))[:5]} too much or too little"
            for name, (seed, size) in expect.items():
                is_dir, first, got = listing[name]
                assert not is_dir and got == size, f"{name}: size {got}, wanted {size}"
                assert img.read(first, size) == pattern(seed, size), f"{name}: wrong content"
        if root_extra:
            assert img.listdir()[root_extra[0]][2] == root_extra[1]
        # What was there before is still there, whole.
        for name, (seed, size) in pre.items():
            is_dir, first, got = img.lookup(*name.split("/"))
            assert not is_dir and got == size, f"{name}: size {got}, wanted {size}"
            assert img.read(first, size) == pattern(seed, size), f"{name} was damaged"
        if label_before is not None:
            assert img.label() == label_before, f"the volume label is {img.label()!r}, not {label_before!r}"
        if stale is not None:
            assert img.fats[0] == stale, "the first FAT changed although the second one is in use"
    except AssertionError as e:
        print("FAIL " + label + ": " + str(e))
        return False
    finally:
        img.close()
    if c["prep"] in NO_FSCK:
        result = None
        note = ", fsck.fat not used (it reads the first FAT)"
    else:
        result = fatimage.fsck(path, start if c["partition"] else 0)
        note = ""
    if result is not None and result[0] != 0:
        print("FAIL " + label + ": fsck.fat: " + result[1].strip()[-400:])
        return False
    os.remove(path)
    print(f"ok   {label}: {len(expect)} files" + ("" if result is None else ", fsck.fat clean") + note)
    return True


# (megabytes, FAT type, sectors in a cluster, partition table, own formatter) for the transient failures
TRANSIENT_CASES = [
    (40, 32, 1, True, False),
    (20, 16, 2, True, False),
    (40, 32, 1, False, True),
]


def run_transient(case, folder):
    """Fails write K once, for every K of a replacement and a new file, and reads the image after each failure with the
    reader: the old file or the new one is whole, the other file is untouched, no file shares a cluster with another,
    no cluster of a file is unmarked. Leaked clusters are allowed."""
    mb, fat_type, spc, partition, own = case
    label = f"transient FAT{fat_type} {mb}MB spc{spc}{' mbr' if partition else ''}{' own' if own else ''}"
    base = os.path.join(folder, "base.img")
    path = os.path.join(folder, "disk.img")
    fatimage.format_image(base, mb, fat_type, spc, partition, tool=not own)
    shutil.copyfile(base, path)
    proc = subprocess.run([EXE, "transient", path, "0"], capture_output=True, text=True)
    if proc.returncode != 0:
        print("FAIL " + label + ": " + (proc.stderr.strip() or proc.stdout.strip())[-300:])
        return False
    total = int(next(l.split()[1] for l in proc.stdout.splitlines() if l.startswith("TOTAL")))
    for k in range(1, total + 1):
        shutil.copyfile(base, path)
        proc = subprocess.run([EXE, "transient", path, str(k)], capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"FAIL {label}, write {k} fails: " + (proc.stderr.strip() or proc.stdout.strip())[-300:])
            return False
        img = fatimage.Image(path)
        try:
            img.check(allow_leaks=True)
            assert not img.shared_clusters(), "two files share a cluster"
            is_dir, folder_cluster, _ = img.lookup("GLOVERPR")
            listing = img.listdir(folder_cluster)
            for line in proc.stdout.splitlines():
                w = line.split()
                if w[0] == "EITHER":
                    assert w[1] in listing, f"{w[1]} is lost"
                    _, first, size = listing[w[1]]
                    ok = [(s, z) for s, z in ((int(w[2]), int(w[3])), (int(w[4]), int(w[5]))) if z == size and img.read(first, size) == pattern(s, z)]
                    assert ok, f"{w[1]} is neither the old file nor the new one"
                elif w[0] == "KEEP":
                    _, first, size = listing[w[1]]
                    assert size == int(w[3]) and img.read(first, size) == pattern(int(w[2]), size), f"{w[1]} changed"
                elif w[0] == "NEW" and w[1] in listing:
                    _, first, size = listing[w[1]]
                    assert size == int(w[3]) and img.read(first, size) == pattern(int(w[2]), size), f"{w[1]} is there but not whole"
        except AssertionError as e:
            print(f"FAIL {label}, write {k} of {total} fails: {e}")
            return False
        finally:
            img.close()
    print(f"ok   {label}: each of {total} writes failed once; no file lost or damaged")
    return True


def run_first_save(folder):
    """The first save to a populated large card: a 2200 MB FAT32 image (a sparse file) with clusters of 4 KB, 2000 small
    files in the root directory and 12 folders of 100 files each, and a file of 1.2 GB that uses the first 300,000 clusters
    (only its FAT chain is written). The save has to find free clusters behind all of them. Prints what the device was
    asked for: the sectors and the commands (a command moves at most 16 sectors)."""
    path = os.path.join(folder, "large.img")
    fatimage.make_large_card(path)
    proc = subprocess.run([EXE, "firstsave", path], capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        print("FAIL first save on a large card: " + (proc.stderr.strip() or proc.stdout.strip())[-300:])
        return False
    img = fatimage.Image(path)
    try:
        is_dir, cluster, _ = img.lookup("GLOVERPR")
        first, size = img.listdir(cluster)["STATE001.GST"][1:]
        assert size == 1210 * 512 - 100 and img.read(first, size) == bytes(size), "the file is wrong"
    except AssertionError as e:
        print("FAIL first save on a large card: " + str(e))
        return False
    finally:
        img.close()
    result = fatimage.fsck(path, 0)
    if result is not None and result[0] != 0:
        print("FAIL first save on a large card: fsck.fat: " + result[1].strip()[-400:])
        return False
    os.remove(path)
    stats = next(l for l in proc.stdout.splitlines() if l.startswith("STATS"))
    print("ok   first save on a populated 2200 MB FAT32 card (4 KB clusters, 3200 files, 300,000 clusters in use): " + stats[6:])
    return True


def main():
    build()
    if not shutil.which("fsck.fat"):
        print("SKIP fsck.fat: not installed. The reader in tests/fatimage.py still checks every image.")
    failed = 0
    with tempfile.TemporaryDirectory() as folder:
        for case in CASES:
            if not run_case(case, folder):
                failed += 1
        for case in TRANSIENT_CASES:
            if not run_transient(case, folder):
                failed += 1
        if not run_first_save(folder):
            failed += 1
    count = len(CASES) + len(TRANSIENT_CASES) + 1
    print(("FAIL" if failed else "PASS") + f" fathost: {count - failed} of {count} cases")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
