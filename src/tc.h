/* tc.h — Timecode types, frame rate table, and math
 *
 * Separated from the main generator loop for testability and reuse
 * across the generator, IPC layer, and web interface.
 */

#ifndef TC_H
#define TC_H

#include <stdint.h>

/* ── Timecode value ──────────────────────────────────────────────── */

typedef struct {
    int h, m, s, f;
} TC;

/* ── Frame rate descriptor ───────────────────────────────────────── */

typedef struct {
    const char *name;       /* "24", "25", "29.97", "29.97df", "30", "30df" */
    double      fps;        /* exact frames per second */
    int         ifps;       /* integer ceiling (24, 25, or 30) */
    int         drop;       /* 1 = drop-frame */
    uint64_t    frame_ns;   /* nominal nanoseconds per frame (integer) */
} FrameRate;

/* All supported frame rates (terminated by a NULL-name sentinel). */
extern const FrameRate FRAME_RATES[];
extern const int        N_FRAME_RATES;

/* Look up a frame rate by name string.  Returns NULL if not found. */
const FrameRate *fr_find(const char *name);

/* ── Timecode operations ─────────────────────────────────────────── */

/* Advance by one frame.  Handles drop-frame for 29.97df and 30df. */
void tc_advance(TC *tc, const FrameRate *fr);

/* Parse "HH:MM:SS:FF".  Returns 1 on success, 0 on error. */
int tc_parse(TC *tc, const char *s);

/* Initialise TC from CLOCK_REALTIME (wall clock). */
void tc_from_clock(TC *tc, const FrameRate *fr);

/* Format TC into a static buffer.  Returns pointer to static storage. */
const char *tc_format(const TC *tc);

/* ── User bits ───────────────────────────────────────────────────── */

/* Set the 32-bit user-bits value into the four 8-bit LTC fields.
 * Accepts a hex string (e.g. "0A1B2C3D") or the keyword "date"
 * to encode today's date in BCD (YYMMDD, little-endian groups).
 * Returns 0 on success, -1 on parse error. */
int ub_parse(const char *s, unsigned char *ubg1, unsigned char *ubg2,
             unsigned char *ubg3, unsigned char *ubg4);

#endif /* TC_H */
