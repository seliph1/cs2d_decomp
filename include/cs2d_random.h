#ifndef CS2D_RANDOM_H
#define CS2D_RANDOM_H

#include <stdint.h>

/*
 * BlitzMax TRandom (CS2D.exe): the spread the client adds to every shot (Tpl.shoot 0x140209810) is not a
 * per-frame random but a *table* seeded from the server, so every client spreads the same pellets as the
 * server does.
 *
 * Evidence (objdump on CS2D.exe; the Ghidra dump is `docs/dump/cs2d/TRandom.c`):
 *  - GenerateSeed is at 0x14000fe90; its loop ends at `data + 0xfa0` -> the table holds 1000 ints.
 *  - the modulus is the global at 0x140dde720, written to 123 (`mov dword [0x140dde720],0x7b` at
 *    0x14030720e); B / aX are 0x140dde728 / 0x140dde724 and the table pointer 0x140dde730.
 *  - GetNumber (0x14000ff50) is `SeedTable[index] % range`, -1 when index >= 1000.
 *  - the seed itself arrives as an i32 in the join settings [252,6] (0x1402d0560: ReadInt + GenerateSeed).
 */
#define CS2D_RANDOM_TABLE 1000
#define CS2D_RANDOM_RANGE 123

/* TRandom.GenerateSeed(seed): refills the whole table (0x14000fe90) */
void cs2d_random_seed(int seed);

/* SeedTable[index], or -1 when index is outside 0..999 (the original returns -1 there too) */
int  cs2d_random_table(int index);

/* TRandom.GetNumber(index, range) = SeedTable[index] % range (0x14000ff50) */
int  cs2d_random_get(int index, int range);

#endif /* CS2D_RANDOM_H */
