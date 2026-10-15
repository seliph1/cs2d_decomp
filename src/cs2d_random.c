#include "cs2d_random.h"

/*
 * TRandom.SeedTable / B / aX (CS2D.exe 0x140dde730 / 0x140dde728 / 0x140dde724).
 * GenerateSeed (0x14000fe90) decodes to:
 *   B = abs(seed)
 *   SeedTable[0] = (B + 1) % Range        (Range = 123, 0x140dde720)
 *   aX = SeedTable[0] + 1
 *   for i in 1..999: SeedTable[i] = (B + aX) % Range; aX += SeedTable[i]
 */
static int s_table[CS2D_RANDOM_TABLE];
static int s_b;
static int s_ax;

void cs2d_random_seed(int seed)
{
    int b = (seed < 0) ? -seed : seed;   /* B is stored as abs(seed) (xor/sub on the sign) */
    s_b = b;
    s_table[0] = (b + 1) % CS2D_RANDOM_RANGE;
    s_ax = s_table[0] + 1;
    for (int i = 1; i < CS2D_RANDOM_TABLE; i++) {
        int r = (b + s_ax) % CS2D_RANDOM_RANGE;
        s_ax += r;
        s_table[i] = r;
    }
}

int cs2d_random_table(int index)
{
    if (index < 0 || index >= CS2D_RANDOM_TABLE) return -1;   /* GetNumber: (uint)p1 < 1000 */
    return s_table[index];
}

int cs2d_random_get(int index, int range)
{
    int v = cs2d_random_table(index);
    if (v < 0 || range == 0) return -1;   /* the original would divide by zero for range == 0 */
    return v % range;
}
