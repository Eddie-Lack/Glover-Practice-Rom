#ifndef PRACTICE_LZ_H
#define PRACTICE_LZ_H

#include "types.h"

/* A byte-oriented LZ compressor and decompressor of the practice code's own design, for savestates (savestate.c).
 *
 * A block is at most LZ_BLOCK_MAX bytes, starts on a word and has a length that is a multiple of 4. It is a list
 * of sequences. A sequence is a token byte, then the literals it names, then (unless the block ends there) a
 * match: a copy of earlier bytes of the same block.
 *
 *   token bits 7-6  literal count: 0, 1, 2, or 3 for "3 plus the extension bytes"
 *   token bits 5-4  offset: 0 the offset of the last match (never used in the first match of a block),
 *                   1 one byte, 2 two bytes (high byte first); the offset is a count of words. 3 is not used
 *   token bits 3-0  match length minus 4: 0 to 14, or 15 for "15 plus the extension bytes"
 *   extension       bytes that are added up; a byte of 255 means that another follows
 *
 * The bytes of a sequence are: token, literal extension, literals, offset, match extension. The decoder stops
 * when the block's length is reached, after the literals or after a match. Offsets are multiples of 4, so a
 * match is found only at word positions and copies a word at a time. */
#define LZ_BLOCK_MAX 0x4000
/* The number of this stream format. A card file records it (card.c); a change to the format raises it. */
#define LZ_FORMAT 1

/* The compressor's work memory: a hash table of word positions, LZ_WORK_ENTRIES 16-bit entries. */
#define LZ_BUCKET_BITS 10
#define LZ_WAYS 2
#define LZ_WORK_ENTRIES ((1 << LZ_BUCKET_BITS) * LZ_WAYS)

/* Compresses the N bytes at IN (word-aligned, N a multiple of 4, at most LZ_BLOCK_MAX) to OUT. Returns the end of
 * the output, or NULL if it would pass LIMIT (an output that ends exactly at LIMIT is given). */
u8 *LzCompress(u8 *out, const u8 *limit, const u8 *in, u32 n, u16 *work);

/* Decompresses a block of N bytes from IN to OUT. Returns the end of the input, or NULL if the stream asks for more
 * literals or a longer match than the block has room for, or for a match from before the block's start. A stream
 * that ends early is not noticed: the decoder does not know where the input ends. */
const u8 *LzDecompress(u8 *out, u32 n, const u8 *in);

/* A 64-bit hash of WORDS words at P, for telling whether two blocks are equal without comparing them: the plain
 * sum of the words in OUT[0], and in OUT[1] a hash that mixes xor, rotation and addition. A change that keeps the
 * sum (a word up by one, the next down by two, the next up by one) changes the second. */
void LzHash(const u32 *p, u32 words, u32 *out);

#endif
