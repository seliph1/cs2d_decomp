/*
 * Dedicated server link stubs: the shared game modules call into the client's sound engine, which the
 * dedicated server does not have (cs2d_dedicated.exe plays no sound). Linked into cs2d_server.exe only.
 */
#include "cs2d_sound.h"

int cs2d_sound_play_at(const char *path, float x, float y, float vol, int flags)
{
    (void)path; (void)x; (void)y; (void)vol; (void)flags;
    return 0;
}
