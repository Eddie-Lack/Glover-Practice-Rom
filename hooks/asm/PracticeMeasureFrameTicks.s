.include "macro.inc"

# The boot hook. It takes the place of ClearStaticMemory in main.c, which nothing calls, and has the same size,
# 0x74 bytes, so nothing after it moves. MainProc calls it in place of MeasureFrameTicks, once the scheduler and
# the message queues exist and before the game loads anything.
#
# It copies the loader (practice/src/loader/) into the game's heap, which is still unused, and runs it. Without
# the Expansion Pak the loader shows a message and never returns. With it, the loader copies the practice segment
# to 0x80400000. PracticeInit then clears the loader's memory and sets up the practice code.

glabel PracticeMeasureFrameTicks
    addiu   $sp, $sp, -0x18
    sw      $ra, 0x14($sp)
    lui     $a0, %hi(loader_PI_START)
    addiu   $a0, $a0, %lo(loader_PI_START)
    lui     $a1, %hi(loader_VRAM)
    addiu   $a1, $a1, %lo(loader_VRAM)
    lui     $a2, %hi(loader_ROM_SIZE)
    jal     DmaRomToRam
     addiu  $a2, $a2, %lo(loader_ROM_SIZE)
    jal     PracticeLoader
     nop
    jal     PracticeInit
     nop
    jal     MeasureFrameTicks
     nop
    lw      $ra, 0x14($sp)
    jr      $ra
     addiu  $sp, $sp, 0x18
    .fill   (0x74 - (. - PracticeMeasureFrameTicks)) / 4, 4, 0
