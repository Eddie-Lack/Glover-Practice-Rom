"""Controller Pak storage, a pak with two practice notes (cpakcross.py has the idea): a save and a delete refuse and
write nothing."""
import pakfile
from cpakcross import refuses

PAKS = "1"


def prepare(path, args):
    p = pakfile.format_pak()
    data = bytes(range(256)) * 2
    pakfile.add_note(p, pakfile.NOTE_CODE, pakfile.NOTE_COMPANY, pakfile.NOTE_NAME, data)
    pakfile.add_note(p, pakfile.NOTE_CODE, pakfile.NOTE_COMPANY, pakfile.NOTE_NAME, data)
    pakfile.write_pak(path, p)


def run(emu, sym, args):
    yield from refuses(emu, sym, args)
