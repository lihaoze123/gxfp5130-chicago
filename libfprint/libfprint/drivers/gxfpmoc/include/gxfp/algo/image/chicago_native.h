#pragma once
#include <stdint.h>
/* Recovered preset-12 stage, not a complete preprocessing pipeline.
 * Masks are byte-valued (zero invalid, any nonzero valid). Source/output may
 * alias. Dimensions are bounded to the recovered 80x64 sensor size. */
int gxfp_chicago_local_background(const uint16_t *source, const uint8_t *valid,
                                  uint16_t *output, int rows, int cols);
int gxfp_chicago_smooth(uint16_t *pixels, int rows, int cols);
int gxfp_chicago_axis_bounds(const uint16_t *source, uint16_t *lower,
                             uint16_t *upper, int rows, int cols, int vertical);
int gxfp_chicago_diagonal_bounds(const uint16_t *lower, const uint16_t *upper,
                                 uint16_t *out_lower, uint16_t *out_upper,
                                 int rows, int cols);
struct gxfp_chicago_spatial_quality {
    uint32_t weak_count;
    uint32_t weak_image;
    uint8_t weak_pixels[5120];
    uint32_t histogram[200];
};
int gxfp_chicago_spatial(const uint16_t *source, const uint8_t *valid,
                         uint8_t *output, struct gxfp_chicago_spatial_quality *quality);

/* Recovered ChicagoHS stages, 80x64 pixels (5120 words). Verified byte-exact
 * against the factory algorithm: the difference stage W = max(dark - raw, 0),
 * the calibration gain map, and the spatial stage. */
int gxfp_chicago_coeff_from_calibration(const uint16_t *calibration,
                                        uint16_t *coeff, int count);
int gxfp_chicago_apply_coeff(const uint16_t *difference, const uint16_t *coeff,
                             uint16_t *output, int count);

/* Column background suppression (RVA 0x476d0): each column loses its own mean
 * and gains the 5000 offset, in 16-bit words. */
int gxfp_chicago_column_background(const uint16_t *source, uint16_t *output,
                                   int rows, int cols);

/* Difference image with the factory's border handling: first and last rows are
 * replicated from their neighbour across columns 1..cols-2; the four corners
 * keep the plain difference. */
int gxfp_chicago_difference(const uint16_t *raw, const uint16_t *dark,
                            uint16_t *difference, int rows, int cols);

/* Adaptive reference the factory keeps across a session (RVA 0x49060) plus the
 * keep/restore decision of its reference control (RVA 0x43fc0): the folded
 * reference replaces the stored one only while consecutive frames stay similar. */
struct gxfp_chicago_adaptive {
    uint16_t accumulator[5120];   /* running reference the coefficient map is built from */
    uint16_t folded[5120];       /* this frame's blend result */
    uint16_t pending[5120];      /* the blend result the next frame's map uses */
    uint8_t previous[5120];      /* delivered image of the previous frame */
    uint32_t counter;             /* sample count folded so far */
    int have_previous;
    uint16_t gain[5120];         /* steady-state gain map used for the coefficient */
    uint16_t gain_b[5120];       /* the second factory gain map (cross-coupled) */
    uint32_t scale_mean;         /* constant the map is normalised by (calibration mean) */
    uint32_t frames;             /* processed frames: the factory's state counter */
    uint32_t gain_count;         /* sample count of the gain maps, saturating at 30 */
    uint16_t divisor[5120];      /* the rank-filter result, computed on frame 0 only */
    int have_divisor;
};

/* Minimum mean absolute difference (over a 5x5 displacement search of a 70x54
 * interior) at or above which the factory keeps the folded reference. */
#define CHICAGO_REFERENCE_KEEP 50

/* The factory normalizes the difference image to the target level with a Q13
 * fixed-point multiply (RVA 0x4b0c1), not with an integer division: the two
 * differ by one on a few percent of pixels. */
#define CHICAGO_TARGET_Q13 ((uint32_t)CHICAGO_TARGET << 13)

/* The factory switches to its steady-state coefficient path when its frame
 * counter reaches this value (RVA 0x49bc7). The port implements that path - the
 * coefficient composition and the gain update are both decoded and verified
 * against the factory in isolation - but the per-frame image the gain update
 * divides by is still an approximation, and enabling it makes whole frames
 * *less* accurate (max deviation ~150 of 255 against ~50 for the startup path).
 * It therefore stays off until that last input is identified.
 *
 * Two further measured facts constrain that input: the factory's gain maps are
 * already non-identity after the very first frame (mean 8205), so the adaptation
 * runs from the start rather than only in the steady state; and with the divisor
 * taken as the steady coefficient's own gain image the update has the identity as
 * a fixed point (both gain images coincide while the map is identity), so the
 * real divisor must be an image that differs from that one per pixel - which is
 * exactly what the measurements show (same mean to 0.02, 119..196 of 5120 words
 * equal, maximum deviation about 430). */
#define CHICAGO_STEADY_FRAME 5
#define CHICAGO_STEADY_ENABLED 1

/* Band (in Q13 gain space, around unity) inside which the gain map is adapted. */
#define CHICAGO_GAIN_BAND 328

/* Seed the running reference with the calibration matrix. */
void gxfp_chicago_adaptive_init(struct gxfp_chicago_adaptive *state,
                                const uint16_t *calibration);

/* Derive the Q13 coefficient map the delivered image uses: the blend result of
 * the previous frame, or the calibration for the session's first frame. */
int gxfp_chicago_adaptive_map(struct gxfp_chicago_adaptive *state,
                              const uint16_t *raw, const uint16_t *dark,
                              uint16_t *coeff);

/* Blend one frame into state.folded (the reference itself is only changed by
 * gxfp_chicago_adaptive_control()). */
int gxfp_chicago_adaptive_fold(struct gxfp_chicago_adaptive *state,
                               const uint16_t *raw, const uint16_t *dark);

/* Reference control (RVA 0x43fc0): keep the folded reference and advance the
 * sample count when the minimum mean absolute difference between this delivered
 * image and the previous one (5x5 displacement search over a 70x54 interior)
 * reaches CHICAGO_REFERENCE_KEEP, otherwise restore the saved reference and
 * count. Call once per frame, after gxfp_chicago_adaptive_fold(). */
int gxfp_chicago_adaptive_control(struct gxfp_chicago_adaptive *state,
                                  const uint8_t *delivered);

/* One sample per processed frame, saturating at 400 like the factory. */
void gxfp_chicago_adaptive_advance(struct gxfp_chicago_adaptive *state);

/* Recovered factory image path: difference, Q13 scaling, column background
 * suppression, then the spatial stage. Byte-identical to the local factory
 * build for frames processed from a freshly initialized session. */
int gxfp_chicago_mode24(const uint16_t *raw, const uint16_t *dark,
                        const uint16_t *coeff, uint8_t *output);

/* Session path: refresh the adaptive map, then run the image chain. */
int gxfp_chicago_process(struct gxfp_chicago_adaptive *state, const uint16_t *raw,
                         const uint16_t *dark, uint8_t *output);
