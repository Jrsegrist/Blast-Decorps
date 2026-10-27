# What splat's disassembly (asm/, asm/nonmatchings/) needs from us: glabel,
# a word-aligned global label at the start of every function and data item.
.macro glabel name
    .balign 4
    .globl \name
\name:
.endm
