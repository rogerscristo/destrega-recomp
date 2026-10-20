/* Disc image / data pack access (runtime/disc.c). */
#ifndef DISC_H
#define DISC_H
#include "psx.h"

/* Open a .bin, a .cue (single MODE2/2352 track) or a data pack. */
bool disc_open(const char *path);
void disc_close(void);
u32 disc_sector_count(void);
/* Read one raw 2352-byte sector; zero-fills and returns false out of range. */
bool disc_read_sector(u32 lba, u8 *out);
bool disc_find_file(const char *path, u32 *lba, u32 *size);
u8 *disc_load_file(const char *path, u32 *size_out);
/* NULL if the open disc is the supported US release, else a user-facing message. */
const char *disc_verify(void);

typedef void (*DiscProgressFn)(void *ud, float fraction);
/* Verify `src` (.bin/.cue) and write a compressed data pack to `pak_path`.
 * Returns NULL on success, else a user-facing error message. */
const char *disc_import(const char *src, const char *pak_path, DiscProgressFn progress, void *ud);

#endif
