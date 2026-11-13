BASEROM ?= baserom.us.z64
GW      := gloveworld
BUILD   := build
ROM     := $(BUILD)/glover-practice.z64
ELF     := $(BUILD)/glover-practice.elf
LD_SCRIPT := $(BUILD)/practice.ld
BPS     := $(BUILD)/glover-practice.bps
PAGE    := $(BUILD)/glover-practice.html
# The test build: the same ROM, with the SD card's test mailbox in sd.c. Players do not get it (docs/build.md).
TEST_DIR := $(BUILD)/test
TEST_ROM := $(TEST_DIR)/glover-practice-test.z64
TEST_ELF := $(TEST_DIR)/glover-practice-test.elf
TEST_LD  := $(TEST_DIR)/practice.ld
# The practice ROM has the retail ROM's size (docs/build.md, "Layout").
ROM_SIZE := 0x800000

PYTHON  ?= python3
CROSS   ?= mips-linux-gnu-
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
NM      := $(CROSS)nm
VENV    := $(GW)/.venv

# The game's code: gloveworld's compiler and flags, with PRACTICE defined, which turns the hooks on.
GW_CC      := COMPILER_PATH=tools/gcc-2.7.2 tools/gcc-2.7.2/gcc
GW_IINC    := -Iinclude -Iinclude/PR -Isrc -I. -I../hooks/include
GW_DEFINES := -D_LANGUAGE_C -DF3DEX_GBI -D_FINALROM -DNDEBUG -DTARGET_N64 -DPRACTICE
GW_CFLAGS  := -w -G0 -mcpu=r4000 -mips2 -mmips-as -funsigned-char -Wa,-EB,-mips3,-mabi=32,-mgp32,-march=vr4300,-mfp32,-mno-shared,--no-pad-sections,-Iinclude
GW_O0      := decompress eeprom controller main jalloc text font banks sched
ALIGN_FIX  := --set-section-alignment .text=4 --set-section-alignment .data=4 --set-section-alignment .bss=4 --set-section-alignment .rodata=4

# The practice code: a current GCC (docs/plan.md, "Code choice").
CC      := $(CROSS)gcc
CFLAGS  := -std=gnu11 -Os -G0 -march=vr4300 -mfix4300 -mabi=32 -mfp32 -mhard-float -mno-abicalls -fno-pic \
           -mno-shared -mno-check-zero-division -ffreestanding -fno-builtin -fno-common \
           -fno-tree-loop-distribute-patterns -fno-math-errno -fno-stack-protector -fno-asynchronous-unwind-tables \
           -Wall -Wextra -Werror -Ipractice/include -I$(BUILD)

GW_SRCS   := $(wildcard $(GW)/src/*.c)
GW_OBJS   := $(patsubst $(GW)/src/%.c,$(BUILD)/gw/src/%.o,$(GW_SRCS))
HOOKS     := $(sort $(wildcard hooks/*.patch))
HOOK_ASM  := $(wildcard hooks/asm/*.s)
PRACTICE_SRCS := $(sort $(wildcard practice/src/*.c))
PRACTICE_OBJS := $(patsubst practice/src/%.c,$(BUILD)/practice/%.o,$(PRACTICE_SRCS))
# The loader has its own copy of the text code: without the Expansion Pak the practice segment is not loaded.
LOADER_OBJS   := $(patsubst practice/src/loader/%.c,$(BUILD)/loader/%.o,$(sort $(wildcard practice/src/loader/*.c))) \
                 $(BUILD)/loader/text.o $(BUILD)/loader/font.o
ASSETS_BIN    := $(BUILD)/trim/bin/assets.bin
ASSETS_OBJ    := $(BUILD)/assets.o
PRACTICE_HEADERS := $(wildcard practice/include/*.h) $(BUILD)/version.h $(BUILD)/state_ranges.h

# A step that fails (mkrom.py, checklayout.py) removes its output: the next make builds it again.
.DELETE_ON_ERROR:

all: $(ROM)

# Builds gloveworld's toolchain if needed and splits the player's ROM inside the submodule.
setup:
	@test -f "$(BASEROM)" || { echo "make setup: give the path to your Glover (USA) ROM: make setup BASEROM=path/to/glover.z64"; exit 1; }
	@test "$$(sha1sum < "$(BASEROM)" | cut -d' ' -f1)" = "$$(cut -d' ' -f1 $(GW)/NGVE.sha1)" || { echo "make setup: $(BASEROM) is not Glover (USA) in .z64 byte order"; exit 1; }
	@[ "$(abspath $(BASEROM))" = "$(abspath baserom.us.z64)" ] || ln -sf "$(abspath $(BASEROM))" baserom.us.z64
	test -x $(VENV)/bin/python || { $(PYTHON) -m venv $(VENV) && $(VENV)/bin/pip install -q -r $(GW)/requirements.txt; }
	test -x $(GW)/tools/gcc-2.7.2/cc1 || $(MAKE) -C $(GW) toolchain
	PATH="$(abspath $(VENV))/bin:$$PATH" $(MAKE) -C $(GW) setup BASEROM=$(abspath baserom.us.z64)

# gloveworld's own build. It must still match the retail ROM: the practice build uses its objects and ELF.
$(GW)/build/NGVE.elf: FORCE
	$(MAKE) -C $(GW) --no-print-directory

# The game's source, with the hooks applied.
$(BUILD)/gw/.patched: $(GW_SRCS) $(HOOKS)
	rm -rf $(BUILD)/gw/src
	mkdir -p $(BUILD)/gw
	cp -r $(GW)/src $(BUILD)/gw/src
	for p in $(HOOKS); do patch -s -p1 -d $(BUILD)/gw < $$p || exit 1; done
	touch $@

$(BUILD)/gw/src/%.o: $(BUILD)/gw/.patched hooks/include/practice_hooks.h $(HOOK_ASM)
	@echo "CC (2.7.2) $*.c"
	@cd $(GW) && $(GW_CC) -c $(GW_CFLAGS) $(if $(filter $*,$(GW_O0)),-O0,-O2) $(GW_IINC) $(GW_DEFINES) ../$(BUILD)/gw/src/$*.c -o ../$@
	@$(OBJCOPY) $(ALIGN_FIX) $@

# The version comes from the git tags (tools/version.py, docs/release.md).
$(BUILD)/version.h: FORCE tools/version.py
	@mkdir -p $(BUILD)
	@v="$$($(PYTHON) tools/version.py header)"; \
	 [ "$$(cat $@ 2>/dev/null)" = "$$v" ] || echo "$$v" > $@

# The memory a savestate holds, from the game's symbols. mkstate.py rewrites the file only when it changes.
$(BUILD)/state_ranges.h: tools/mkstate.py $(GW)/build/NGVE.elf
	@mkdir -p $(BUILD)
	@$(PYTHON) tools/mkstate.py $(GW)/build/NGVE.elf $@ --nm $(NM)

$(BUILD)/practice/%.o: practice/src/%.c $(PRACTICE_HEADERS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/loader/%.o: practice/src/loader/%.c $(PRACTICE_HEADERS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -DPRACTICE_LOADER $< -o $@

$(BUILD)/loader/%.o: practice/src/%.c $(PRACTICE_HEADERS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -DPRACTICE_LOADER $< -o $@

# gloveworld's assets blob runs to the end of the retail ROM, zero padding included. The practice build links the
# blob without the padding, which holds the loader and the practice segment. objcopy runs in the folder of the
# blob. Its symbols then keep their names (_binary_bin_assets_bin_start).
# Always run: trimassets.py writes the blob only when it changes.
$(ASSETS_BIN): FORCE tools/trimassets.py $(GW)/bin/assets.bin $(GW)/build/NGVE.elf
	@mkdir -p $(dir $@)
	@$(PYTHON) tools/trimassets.py $(GW)/bin/assets.bin $@ $(GW)/build/NGVE.elf --nm $(NM)

$(ASSETS_OBJ): $(ASSETS_BIN)
	cd $(BUILD)/trim && $(OBJCOPY) -I binary -O elf32-tradbigmips -B mips bin/assets.bin ../assets.o

# Always run: the lists of objects may have changed. mkld.py rewrites the script only when it changes.
$(LD_SCRIPT): FORCE tools/mkld.py $(GW)/build/NGVE.elf
	@mkdir -p $(BUILD)
	$(PYTHON) tools/mkld.py $(GW) $@ --rom-size $(ROM_SIZE) --loader $(LOADER_OBJS) --practice $(PRACTICE_OBJS)

$(ELF): $(GW_OBJS) $(ASSETS_OBJ) $(LOADER_OBJS) $(PRACTICE_OBJS) $(LD_SCRIPT)
	$(LD) -T $(LD_SCRIPT) -T $(GW)/undefined_syms_auto.txt -T $(GW)/undefined_funcs_auto.txt -T $(GW)/symbols.ld \
	      -Map $(BUILD)/glover-practice.map --no-check-sections -o $@

# The test build: sd.o is built again with the mailbox, and the rest is shared.
TEST_OBJS := $(filter-out $(BUILD)/practice/sd.o,$(PRACTICE_OBJS)) $(TEST_DIR)/sd.o

$(TEST_DIR)/sd.o: practice/src/sd.c $(PRACTICE_HEADERS)
	@mkdir -p $(dir $@)
	@echo "CC $< (test mailbox)"
	@$(CC) -c $(CFLAGS) -DPRACTICE_TEST_MAILBOX $< -o $@

$(TEST_LD): FORCE tools/mkld.py $(GW)/build/NGVE.elf
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkld.py $(GW) $@ --rom-size $(ROM_SIZE) --loader $(LOADER_OBJS) --practice $(TEST_OBJS)

$(TEST_ELF): $(GW_OBJS) $(ASSETS_OBJ) $(LOADER_OBJS) $(TEST_OBJS) $(TEST_LD)
	$(LD) -T $(TEST_LD) -T $(GW)/undefined_syms_auto.txt -T $(GW)/undefined_funcs_auto.txt -T $(GW)/symbols.ld \
	      -Map $(TEST_DIR)/glover-practice-test.map --no-check-sections --defsym=sdMailbox=0x80748000 -o $@

$(TEST_ROM): $(TEST_ELF) tools/mkrom.py tools/checklayout.py
	$(OBJCOPY) -O binary --pad-to=$(ROM_SIZE) --gap-fill=0x00 $< $@
	$(PYTHON) tools/mkrom.py $@ --size $(ROM_SIZE)
	$(PYTHON) tools/checklayout.py $(GW)/build/NGVE.elf $(TEST_ELF) $(GW)/build/NGVE.z64 $@ --nm $(NM)
	@echo "$@: built"

testrom: $(TEST_ROM)

# The ROM is padded to ROM_SIZE (8 MiB). mkrom.py fails if it is larger.
$(ROM): $(ELF) tools/mkrom.py tools/checklayout.py
	$(OBJCOPY) -O binary --pad-to=$(ROM_SIZE) --gap-fill=0x00 $< $@
	$(PYTHON) tools/mkrom.py $@ --size $(ROM_SIZE)
	$(PYTHON) tools/checklayout.py $(GW)/build/NGVE.elf $(ELF) $(GW)/build/NGVE.z64 $@ --nm $(NM)
	@echo "$@: built"

patch: $(BPS) $(PAGE)

$(BPS): $(ROM) tools/bps.py
	$(PYTHON) tools/bps.py make baserom.us.z64 $(ROM) $@

$(PAGE): $(BPS) tools/patch.html tools/mkpage.py
	$(PYTHON) tools/mkpage.py tools/patch.html $(BPS) $@

# The release (docs/release.md). The check runs first. ALLOW_UNTAGGED=1 waives the version tag for a trial run.
RELEASE_DIR := $(BUILD)/release

release-check:
	$(PYTHON) tools/release.py check $(if $(ALLOW_UNTAGGED),--allow-untagged)

release: release-check
	$(MAKE) patch
	$(PYTHON) tools/checklayout.py $(GW)/build/NGVE.elf $(ELF) $(GW)/build/NGVE.z64 $(ROM) --nm $(NM)
	$(PYTHON) tools/release.py package --rom $(ROM) --bps $(BPS) --page $(PAGE) --base baserom.us.z64 --out $(RELEASE_DIR)

test: $(ROM) $(TEST_ROM)
	$(PYTHON) tests/run.py

clean:
	rm -rf $(BUILD)

FORCE:

.PHONY: all setup patch release release-check test testrom clean FORCE
.SUFFIXES:
MAKEFLAGS += --no-builtin-rules
