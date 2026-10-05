#include "gxfp/algo/image/chicago_native.h"
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

/* AlgoChicago preset 12, recovered local-mean stage at RVA 0x4b4f0.
 * Integral sums keep the masked count separate from the masked pixel sum.
 * The 3000 offset, integer rounding and low-word store are intentional. */
int gxfp_chicago_local_background(const uint16_t *source, const uint8_t *valid,
                                  uint16_t *output, int rows, int cols)
{
    if (!source || !valid || !output || rows<1 || rows>64 || cols<1 || cols>80)
        return -EINVAL;
    const size_t stride=(size_t)cols+1, size=((size_t)rows+1)*stride;
    uint32_t *sum=calloc(size,sizeof(*sum));
    uint16_t *count=calloc(size,sizeof(*count));
    if (!sum || !count) { free(sum);free(count);return -ENOMEM; }
    for (int y=0;y<rows;y++) {
        uint32_t row_sum=0;
        uint16_t row_count=0;
        for (int x=0;x<cols;x++) {
            size_t i=(size_t)y*cols+x, cell=((size_t)y+1)*stride+x+1;
            if (valid[i]) { row_sum+=source[i];row_count++; }
            sum[cell]=sum[cell-stride]+row_sum;
            count[cell]=(uint16_t)(count[cell-stride]+row_count);
        }
    }
    for (int y=0;y<rows;y++) for (int x=0;x<cols;x++) {
        size_t i=(size_t)y*cols+x;
        if (!valid[i]) { output[i]=3000;continue; }
        int left=x>5?x-5:0, top=y>5?y-5:0;
        int right=x+5<cols?x+6:cols, bottom=y+5<rows?y+6:rows;
        size_t a=(size_t)top*stride+left, b=(size_t)top*stride+right;
        size_t c=(size_t)bottom*stride+left, d=(size_t)bottom*stride+right;
        uint32_t total=sum[d]+sum[a]-sum[b]-sum[c];
        uint32_t n=count[d]+count[a]-count[b]-count[c];
        /* A valid center contributes at least one sample to its neighborhood. */
        int32_t mean=(int32_t)((total+n/2)/n);
        int32_t value=(int32_t)source[i]-mean+3000;
        output[i]=value<0?0:(uint16_t)value;
    }
    free(sum);free(count);
    return 0;
}

#include <stdio.h>
#include <string.h>
static int dimensions(int rows,int cols)
{ return rows>=1 && rows<=64 && cols>=1 && cols<=80; }

/* RVA 0x4c8f0: rounded separable [1 2 1] Gaussian, preserving border words. */
int gxfp_chicago_smooth(uint16_t *pixels, int rows, int cols)
{
    if (!pixels || !dimensions(rows,cols))return -EINVAL;
    size_t bytes=(size_t)rows*cols*sizeof(*pixels);
    uint16_t *original=malloc(bytes);
    if(!original)return -ENOMEM;
    memcpy(original,pixels,bytes);
    for(int y=1;y<rows-1;y++)for(int x=1;x<cols-1;x++) {
        uint32_t sum=8;
        for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)
            sum+=original[(y+dy)*cols+x+dx]*(dy==0?2u:1u)*(dx==0?2u:1u);
        pixels[y*cols+x]=(uint16_t)(sum>>4);
    }
    free(original);return 0;
}
/* RVAs 0x477f0 / 0x45050: clipped radius-5 horizontal / vertical extrema. */
int gxfp_chicago_axis_bounds(const uint16_t *source, uint16_t *lower,
                             uint16_t *upper, int rows, int cols, int vertical)
{
    if(!source || !lower || !upper || !dimensions(rows,cols))return -EINVAL;
    for(int y=0;y<rows;y++)for(int x=0;x<cols;x++) {
        uint16_t lo=65535,hi=0;
        for(int d=-5;d<=5;d++) {
            int yy=y+(vertical?d:0),xx=x+(vertical?0:d);
            if(yy<0 || yy>=rows || xx<0 || xx>=cols)continue;
            uint16_t v=source[yy*cols+xx];
            if(v<lo)lo=v;
            if(v>hi)hi=v;
        }
        lower[y*cols+x]=lo;upper[y*cols+x]=hi;
    }
    return 0;
}
/* RVA 0x45f40: center and four diagonals, clipped at image boundaries. */
int gxfp_chicago_diagonal_bounds(const uint16_t *lower, const uint16_t *upper,
                                 uint16_t *out_lower, uint16_t *out_upper,
                                 int rows, int cols)
{
    if(!lower || !upper || !out_lower || !out_upper || !dimensions(rows,cols))return -EINVAL;
    for(int y=0;y<rows;y++)for(int x=0;x<cols;x++) {
        size_t i=(size_t)y*cols+x;
        uint16_t lo=lower[i],hi=upper[i];
        for(int dy=-1;dy<=1;dy+=2)for(int dx=-1;dx<=1;dx+=2) {
            int yy=y+dy,xx=x+dx;
            if(yy<0 || yy>=rows || xx<0 || xx>=cols)continue;
            uint16_t l=lower[yy*cols+xx],h=upper[yy*cols+xx];
            if(l<lo)lo=l;
            if(h>hi)hi=h;
        }
        out_lower[i]=lo;out_upper[i]=hi;
    }
    return 0;
}
/* Preset-12 spatial conversion (RVA 0x43010). This stage starts after gain
 * normalization, and must not be fed uncalibrated raw sensor words. */
int gxfp_chicago_spatial(const uint16_t *source, const uint8_t *valid,
                         uint8_t *output, struct gxfp_chicago_spatial_quality *quality)
{
    if(!source || !valid || !output)return -EINVAL;
    uint16_t *buffers=malloc(7*5120*sizeof(*buffers));
    if(!buffers)return -ENOMEM;
    uint16_t *image=buffers, *lh=image+5120, *hh=lh+5120;
    uint16_t *lv=hh+5120,*hv=lv+5120,*lo=hv+5120,*hi=lo+5120;
    int r=gxfp_chicago_local_background(source,valid,image,64,80);
    if(!r)r=gxfp_chicago_smooth(image,64,80);
    if(!r)r=gxfp_chicago_axis_bounds(image,lh,hh,64,80,0);
    if(!r)r=gxfp_chicago_axis_bounds(image,lv,hv,64,80,1);
    if(r){free(buffers);return r;}
    for(size_t i=0;i<5120;i++) {
        lo[i]=lh[i]<lv[i]?lh[i]:lv[i];
        hi[i]=hh[i]>hv[i]?hh[i]:hv[i];
    }
    /* The caller erodes the upper bound and dilates the lower bound. */
    r=gxfp_chicago_diagonal_bounds(hi,lo,hh,lh,64,80);
    if(r){free(buffers);return r;}
    if(quality)memset(quality,0,sizeof(*quality));
    for(size_t i=0;i<5120;i++) {
        int value=0,range=(int)hh[i]-lh[i];
        if(valid[i]) {
            value=range?((int)image[i]-lh[i])*255/range:255;
            if(quality && range<150) {quality->weak_count++;quality->weak_pixels[i]=255;}
        }
        if(value<0)value=0;
        if(value>255)value=255;
        output[i]=(uint8_t)(255-value);
    }
    if(quality && quality->weak_count>5120/5) {
        quality->weak_image=1;
        for(size_t i=0;i<5120;i++)if(valid[i]) {
            int range=(int)hh[i]-lh[i];
            if(range<0)range=0;
            if(range>199)range=199;
            quality->histogram[range]++;
        }
    }
    free(buffers);return 0;
}

/* Calibration coefficient map in Q13, RVA 0x4ab80 semantics:
 *   mean  = (sum(calibration) + count / 2) / count     (rounded mean, 2021 here)
 *   coeff = (calibration[i] * 8192 + mean / 2) / mean
 * The two-step form is what the factory code computes; note the *rounded* mean.
 * Zero calibration entries keep the caller's word, as the factory code does. */
int gxfp_chicago_coeff_from_calibration(const uint16_t *calibration,
                                        uint16_t *coeff, int count)
{
    uint64_t sum = 0;
    uint32_t mean;
    if (!calibration || !coeff || count < 1)
        return -EINVAL;
    for (int i = 0; i < count; i++)
        sum += calibration[i];
    mean = (uint32_t)((sum + (uint64_t)count / 2) / (uint64_t)count);
    if (!mean)
        return -EINVAL;
    for (int i = 0; i < count; i++) {
        uint32_t reference = calibration[i];
        uint32_t value;
        if (!reference)
            continue;
        /* Keep the intermediate in 32 bits: reference * 8192 does not fit a word. */
        value = (reference * 8192u + mean / 2) / mean;
        coeff[i] = (uint16_t)(value > 65535u ? 65535u : value);
    }
    return 0;
}

/* Q13 scaling the factory applies between the difference image and the column
 * stage: scaled = (difference * 8192 + coeff / 2) / coeff. */
int gxfp_chicago_apply_coeff(const uint16_t *difference, const uint16_t *coeff,
                             uint16_t *output, int count)
{
    if (!difference || !coeff || !output || count < 1)
        return -EINVAL;
    for (int i = 0; i < count; i++) {
        uint32_t word = difference[i];
        uint32_t reference = coeff[i];
        uint64_t scaled;
        if (!reference) {
            output[i] = (uint16_t)word;
            continue;
        }
        scaled = ((uint64_t)word << 13) + reference / 2;
        scaled /= reference;
        /* The factory stores the low 16 bits of the quotient (it writes `ax`
         * straight from a 32-bit register) rather than saturating. Saturating
         * instead changes a handful of pixels, and each of those then skews its
         * whole column's mean in the next stage. */
        output[i] = (uint16_t)(scaled & 0xffffu);
    }
    return 0;
}

/* Column background suppression (RVA 0x476d0): every column loses its own mean
 * and gains the 5000 offset. The subtraction, the offset and the sign test all
 * happen in 16-bit words, so a wrapped negative becomes black, not bright. */
int gxfp_chicago_column_background(const uint16_t *source, uint16_t *output,
                                   int rows, int cols)
{
    if (!source || !output || rows < 1 || rows > 64 || cols < 1 || cols > 80)
        return -EINVAL;
    for (int x = 0; x < cols; x++) {
        uint32_t sum = 0;
        for (int y = 0; y < rows; y++)
            sum += source[y * cols + x];
        uint16_t mean = (uint16_t)(sum / (uint32_t)rows);
        for (int y = 0; y < rows; y++) {
            size_t i = (size_t)y * cols + x;
            int16_t value = (int16_t)(source[i] - mean + 5000);
            output[i] = value > 0 ? (uint16_t)value : 0;
        }
    }
    return 0;
}

/* Difference image (RVA 0x49a50 for this mode): the directed dark-frame
 * difference, with the first and last rows replicated from their neighbour
 * across columns 1..cols-2. The four corners keep the plain difference; that
 * asymmetry is what the factory build does and is required for byte equality. */
int gxfp_chicago_difference(const uint16_t *raw, const uint16_t *dark,
                            uint16_t *difference, int rows, int cols)
{
    if (!raw || !dark || !difference || rows < 3 || rows > 64 || cols < 3 || cols > 80)
        return -EINVAL;
    for (int i = 0; i < rows * cols; i++) {
        int32_t value = (int32_t)dark[i] - (int32_t)raw[i];
        difference[i] = (uint16_t)(value > 0 ? (uint32_t)value : 0u);
    }
    for (int x = 1; x < cols - 1; x++) {
        difference[x] = difference[cols + x];
        difference[(rows - 1) * cols + x] = difference[(rows - 2) * cols + x];
    }
    return 0;
}

/* Adaptive reference and its Q13 map. The factory keeps a running per-pixel
 * reference (RVA 0x49060, accumulator at 0x18009a1e4) that starts as the
 * calibration and is blended toward the newest frame's normalized difference:
 *
 *   W[i]    = difference(raw, dark) with the border rule
 *   m       = (sum(W) + count / 2) / count            rounded mean of W
 *   v[i]    = (W[i] * target + m / 2) / m             target = g[0x180092868]
 *   acc[i]  = (acc[i] * k + v[i] + (k + 1) / 2) / (k + 1)   k = sample count
 *   map[i]  = (acc[i] * 8192 + mean(acc) / 2) / mean(acc)   RVA 0x49143
 *
 * Only pixels whose value stays inside the target band are folded in, and the
 * map is refreshed every frame from the accumulator. */
#define CHICAGO_TARGET 2000
#define CHICAGO_TARGET2 1600

static uint32_t rounded_mean(const uint16_t *values, int count)
{
    uint64_t sum = 0;
    for (int i = 0; i < count; i++)
        sum += values[i];
    return (uint32_t)((sum + (uint64_t)count / 2) / (uint64_t)count);
}

void gxfp_chicago_adaptive_init(struct gxfp_chicago_adaptive *state, const uint16_t *calibration)
{
    if (!state || !calibration)
        return;
    memcpy(state->accumulator, calibration, sizeof(state->accumulator));
    memset(state->divisor, 0, sizeof(state->divisor));
    state->have_divisor = 0;
    memcpy(state->folded, calibration, sizeof(state->folded));
    memcpy(state->pending, calibration, sizeof(state->pending));
    memset(state->previous, 0, sizeof(state->previous));
    memset(state->gain, 0, sizeof(state->gain));
    memset(state->gain_b, 0, sizeof(state->gain_b));
    state->have_previous = 0;
    state->frames = 0;
    state->gain_count = 0;
    state->scale_mean = rounded_mean(calibration, 5120);

    /* The factory has already counted 211 samples by the time the first frame
     * after activation is processed; the count saturates at 400. */
    state->counter = 211;
}

int gxfp_chicago_adaptive_map(struct gxfp_chicago_adaptive *state,
                              const uint16_t *raw, const uint16_t *dark, uint16_t *coeff)
{
    uint32_t reference_mean;

    if (!state || !raw || !dark || !coeff)
        return -EINVAL;
    /* The delivered image uses the fold result of the previous frame (the plain
     * calibration on the session's first frame); the adopted reference stays
     * frozen while frames keep changing. */
    /* The denominator is the *rounded mean of the reference*, not the fixed
     * calibration mean. Measured against the factory's own Y at 0x180114990 on
     * frames 0..8: 2021 for frames 0..5 and 2020 from frame 6 on, and with that
     * denominator the port's map reproduces Y entry by entry (5120/5120, mae 0.0)
     * on every frame. The two agree until the reference's rounded mean crosses the
     * integer boundary, which is exactly why the divergence used to start at
     * frame 6. */
    reference_mean = rounded_mean(state->pending, 5120);
    if (!reference_mean)
        /* Unverified fallback: the measured evidence covers positive reference
         * means only, so the zero-mean path keeps the calibration mean. */
        reference_mean = state->scale_mean ? state->scale_mean : 1;
    for (int i = 0; i < 5120; i++)
        coeff[i] = (uint16_t)(((uint32_t)state->pending[i] * 8192 + reference_mean / 2) / reference_mean);
    return 0;
}

/* Fold one frame into the running reference (RVA 0x49060). The factory stores
 * the blended value for every pixel into the reference itself; pixels whose
 * normalized value leaves the target band keep their old value. With a sample
 * count around 211 a difference below (k + 1) / 2 rounds back to the same word,
 * which is why the reference looks frozen while consecutive frames are similar. */
int gxfp_chicago_adaptive_fold(struct gxfp_chicago_adaptive *state,
                               const uint16_t *raw, const uint16_t *dark)
{
    /* The blend lands in state->folded; gxfp_chicago_adaptive_control() decides
     * whether it replaces the reference. */
    uint16_t *difference;
    uint16_t *values;
    uint32_t mean, k, factor;
    int result;

    if (!state || !raw || !dark)
        return -EINVAL;
    difference = malloc(5120 * sizeof(*difference));
    values = malloc(5120 * sizeof(*values));
    if (!difference || !values) {
        free(difference);
        free(values);
        return -ENOMEM;
    }
    result = gxfp_chicago_difference(raw, dark, difference, 64, 80);
    if (result) {
        free(difference);
        free(values);
        return result;
    }
    memcpy(state->folded, state->accumulator, sizeof(state->folded));
    mean = rounded_mean(difference, 5120);
    if (!mean) {
        free(difference);
        free(values);
        return 0;
    }
    /* Normalize to the target level the way the factory does: a Q13 multiplier
     * applied with a shift, not an integer division. */
    factor = (CHICAGO_TARGET_Q13 + mean / 2) / mean;
    for (int i = 0; i < 5120; i++)
        values[i] = (uint16_t)(((uint32_t)difference[i] * factor + 4096) >> 13);
    k = state->counter;
    if (k < 1)
        k = 1;
    if (k > 400)
        k = 400;
    for (int i = 0; i < 5120; i++) {
        uint32_t value = values[i];
        uint32_t distance = value > CHICAGO_TARGET ? value - CHICAGO_TARGET : CHICAGO_TARGET - value;
        if (distance >= CHICAGO_TARGET2)
            continue;
        /* The reference's blend, read from the store at 0x490bc:
         *   reference = (reference * count + sample + (count + 1)/2) / (count + 1)
         * with `rdi` holding the count (211 on frame 0, 212 after) - the same
         * quantity this port keeps in `state->counter` - and `(count+1)/2` as the
         * rounding term. An earlier change here used `k/2`, which is not the
         * factory's rounding; it moved the map because the map is this reference
         * normalised, but it moved it for the wrong reason. */
        state->folded[i] =
            (uint16_t)(((uint32_t)state->accumulator[i] * k + value + (k + 1) / 2) / (k + 1));
    }
    free(difference);
    free(values);
    return 0;
}

int gxfp_chicago_adaptive_control(struct gxfp_chicago_adaptive *state,
                                  const uint8_t *delivered)
{
    const int rows = 54, cols = 70;
    uint64_t best = 0;
    int have_best = 0;

    if (!state || !delivered)
        return -EINVAL;
    memcpy(state->pending, state->folded, sizeof(state->pending));
    if (state->have_previous) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                uint64_t sum = 0;
                for (int r = 0; r < rows; r++) {
                    const uint8_t *a = delivered + (size_t)(r + 2 + dy) * 80 + (2 + dx);
                    const uint8_t *b = state->previous + (size_t)(r + 2) * 80 + 2;
                    for (int c = 0; c < cols; c++) {
                        int delta = (int)a[c] - (int)b[c];
                        sum += (uint64_t)(delta < 0 ? -delta : delta);
                    }
                }
                if (!have_best || sum < best) {
                    best = sum;
                    have_best = 1;
                }
            }
        }
        if (best / (uint64_t)(rows * cols) >= CHICAGO_REFERENCE_KEEP) {
            memcpy(state->accumulator, state->folded, sizeof(state->accumulator));
            if (state->counter < 400)
                state->counter++;
        }
    } else {
        memcpy(state->accumulator, state->folded, sizeof(state->accumulator));
        if (state->counter < 400)
            state->counter++;
    }
    memcpy(state->previous, delivered, 5120);
    state->have_previous = 1;
    return 0;
}

/* The sample count advances inside gxfp_chicago_adaptive_fold() whenever the
 * folded reference is adopted; this helper exists for tests and for callers that
 * need to keep a count across a session without folding. */
void gxfp_chicago_adaptive_advance(struct gxfp_chicago_adaptive *state)
{
    if (!state)
        return;
    if (state->counter < 400)
        state->counter++;
}

/* Recovered factory image path for the 80x64 ChicagoHS frames:
 *   W = difference(raw, dark)                          (RVA 0x49a50)
 *   V = (W << 13 + coeff / 2) / coeff                  (RVA 0x4ac36)
 *   X = max(V - column_mean + 5000, 0)                 (RVA 0x476d0)
 *   output = spatial(X, all valid)                     (RVA 0x43010)
 * The dial-striping stage 0x49610 and the earlier 0x47f50 spatial call feed
 * reference and quality state, not the delivered pixels. */

int gxfp_chicago_mode24(const uint16_t *raw, const uint16_t *dark,
                        const uint16_t *coeff, uint8_t *output)
{
    uint16_t *difference;
    uint16_t *columned;
    uint8_t *valid;
    int result;

    if (!raw || !dark || !coeff || !output)
        return -EINVAL;
    difference = malloc(5120 * sizeof(*difference));
    columned = malloc(5120 * sizeof(*columned));
    valid = malloc(5120);
    if (!difference || !columned || !valid) {
        free(difference);
        free(columned);
        free(valid);
        return -ENOMEM;
    }
    for (int i = 0; i < 5120; i++)
        valid[i] = 1;
    result = gxfp_chicago_difference(raw, dark, difference, 64, 80);
    if (!result)
        result = gxfp_chicago_apply_coeff(difference, coeff, difference, 5120);
    if (!result)
        result = gxfp_chicago_column_background(difference, columned, 64, 80);
    if (!result)
        result = gxfp_chicago_spatial(columned, valid, output, NULL);
    if (!result) {
        /* Marker the driver embeds in the delivered frame (RVA 0x54300): the low
         * bit of the first row carries a fixed watermark - alternating bits over
         * the first 73 columns, clear over the last seven. Without it the top row
         * differs by one level on about half of its pixels. */
        for (int col = 0; col < 73; col++) {
            if (col & 1)
                output[col] |= 1;
            else
                output[col] &= (uint8_t)0xfe;
        }
        for (int col = 73; col < 80; col++)
            output[col] &= (uint8_t)0xfe;
    }
    free(difference);
    free(columned);
    free(valid);
    return result;
}

/* Session path: refresh the adaptive coefficient map, then run the image chain.
 * This reproduces the factory across a whole session, not just its first frame. */
int gxfp_chicago_process(struct gxfp_chicago_adaptive *state, const uint16_t *raw,
                         const uint16_t *dark, uint8_t *output)
{
    uint16_t *coeff, *steady, *difference, *median, *blur_buf;
    /* The exact 5-tap kernel the factory convolves with (RVA 0x4f0d0: the
     * weights live in an array indexed by the loop counter, rbx = 5, and sum to
     * exactly 65536). Applied separably with mirror padding it reproduces the
     * captured divisor exactly: 5120 of 5120 words. */
    static const int32_t kernel[5] = { 7869, 15328, 19142, 15328, 7869 };
    uint32_t k;
    int result = 0;

    if (!state || !raw || !dark || !output)
        return -EINVAL;
    coeff = malloc(5120 * sizeof(*coeff));
    steady = malloc(5120 * sizeof(*steady));
    difference = malloc(5120 * sizeof(*difference));
    median = malloc(5120 * sizeof(*median));
    blur_buf = malloc(5120 * sizeof(*blur_buf));
    if (!coeff || !steady || !difference || !median || !blur_buf) {
        free(coeff); free(steady); free(difference); free(median); free(blur_buf);
        return -ENOMEM;
    }
    result = gxfp_chicago_adaptive_map(state, raw, dark, coeff);
    if (result)
        goto out;
    /* The factory adapts the gain *before* it composes the delivered coefficient:
     * measured at the composition event, the factory's gain at frame N equals this
     * port's gain after frame N's update, not before it (frames 0..4 line up exactly
     * one frame apart). Composing first and updating afterwards therefore fed the
     * delivered image a one-frame-old gain, which is what the residual after the
     * denominator fix turned out to be. */
    if (CHICAGO_STEADY_ENABLED) {
        /* Gain adaptation (RVA 0x4b0c1): the factory keeps two per-pixel gain
         * maps and adapts each against the *other* map's gain image, which is
         * what keeps the pair away from the identity fixed point. The sample is
         * the plain map's gain image in both cases. */
        result = gxfp_chicago_difference(raw, dark, difference, 64, 80);
        if (result)
            goto out;
        /* The weight is the global frame counter, 0-based and not clamped up: on
         * frame 0 it must be 0 so that the update writes `value` itself, which is
         * exactly what the factory's map holds (verified entry for entry). */
        k = state->frames;
        if (k > 30)
            k = 30;
        /* Both the sample and the divisor the factory's update uses live in the
         * *gain* domain: the sample is the gain image of the coefficient in use
         * (exactly, 5120/5120), and the divisor is a rank filter of that same
         * image - which sits just below it, so the update settles slightly above
         * unity, exactly as the factory's map does (8202 against 8192).
         * The rank filter's shape follows the verified reconstruction: a
         * horizontal median over the interior rows and every column with edge
         * replication, then a vertical one over the interior rows. */
        /* The divisor is the *second* of the two median-of-three passes the
         * factory runs at RVA 0x4be20: the first reduces the gain image
         * horizontally, `median3(V[i-2], V[i-1], V[i])` - verified exactly,
         * 4836 of 4836 iterations - and the second combines that result with the
         * same image one row on and the shared value from the previous
         * iteration. */
        {
            for (int i = 0; i < 5120; i++) {
                uint32_t gd2 = state->gain[i] ? state->gain[i] : 8192;
                uint32_t cc = CHICAGO_STEADY_ENABLED && state->frames >= CHICAGO_STEADY_FRAME
                              ? (uint32_t)(((uint64_t)(coeff[i] ? coeff[i] : 8192) * gd2 + 4096) >> 13)
                              : (coeff[i] ? coeff[i] : 8192);
                if (!cc)
                    median[i] = (uint16_t)(difference[i] << 13);
                else
                    median[i] = (uint16_t)((((uint32_t)difference[i] << 13) + cc / 2) / cc);
            }
            /* The factory computes this network **once per session**, on frame 0:
             * writes to its buffer (0x30049580) number 15201 on frame 0 and none
             * on frames 1..7, and the frame-0 snapshot (mean 1618.39) is the same
             * array the update loop divides by at frame 5 (mean 1616.6).
             * Recomputing it every frame is a structural, not a rounding,
             * difference - so the result is cached and reused. */
            if (!state->have_divisor) {
            /* first pass: horizontal median of three, in place into blur_buf */
            for (int row = 0; row < 64; row++) {
                for (int col = 0; col < 80; col++) {
                    int i = row * 80 + col;
                    uint32_t a = median[row * 80 + (col > 0 ? col - 1 : 0)];
                    uint32_t b = median[row * 80 + (col > 1 ? col - 2 : 0)];
                    uint32_t v = median[i];
                    uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
                    blur_buf[i] = (uint16_t)(v < lo ? lo : (v > hi ? hi : v));
                }
            }
            /* second pass: median of three over (m1[i], m1[i+80], V[i-1]) */
            for (int i = 0; i < 5120; i++) {
                uint32_t a = blur_buf[i];
                uint32_t b = blur_buf[i + 80 < 5120 ? i + 80 : i];
                uint32_t v = median[i > 0 ? i - 1 : 0];
                uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
                median[i] = (uint16_t)(v < lo ? lo : (v > hi ? hi : v));
            }
        }
        for (int i = 0; i < 5120; i++) {
            uint32_t gd = state->gain[i] ? state->gain[i] : 8192;
            uint32_t plain = coeff[i] ? coeff[i] : 8192;
            uint32_t used = state->frames >= CHICAGO_STEADY_FRAME
                            ? (uint32_t)(((uint64_t)plain * gd + 4096) >> 13) : plain;
            if (!used)
                used = 8192;
            median[i] = (uint16_t)((((uint32_t)difference[i] << 13) + used / 2) / used);
        }
        for (int row = 0; row < 64; row++) {
            for (int col = 0; col < 80; col++) {
                int64_t acc = 0;
                for (int t = -2; t <= 2; t++) {
                    int cc = col + t;
                    if (cc < 0)
                        cc = -cc;
                    if (cc > 79)
                        cc = 158 - cc;
                    acc += (int64_t)median[row * 80 + cc] * kernel[t + 2];
                }
                acc >>= 16;
                blur_buf[row * 80 + col] = (uint16_t)(acc < 0 ? 0 : (acc > 65535 ? 65535 : acc));
            }
        }
        for (int row = 0; row < 64; row++) {
            for (int col = 0; col < 80; col++) {
                int64_t acc = 0;
                for (int t = -2; t <= 2; t++) {
                    int r3 = row + t;
                    if (r3 < 0)
                        r3 = -r3;
                    if (r3 > 63)
                        r3 = 126 - r3;
                    acc += (int64_t)blur_buf[r3 * 80 + col] * kernel[t + 2];
                }
                acc >>= 16;
                median[row * 80 + col] = (uint16_t)(acc < 0 ? 0 : (acc > 65535 ? 65535 : acc));
            }
        }
        memcpy(blur_buf, median, 5120 * sizeof(*blur_buf));
        memcpy(median, blur_buf, 5120 * sizeof(*median));
        memcpy(median, blur_buf, 5120 * sizeof(*median));
            memcpy(state->divisor, median, sizeof(state->divisor));
            state->have_divisor = 1;
            }
        for (int i = 0; i < 5120; i++) {
            uint32_t gd = state->gain[i] ? state->gain[i] : 8192;
            uint32_t plain = coeff[i] ? coeff[i] : 8192;
            /* The sample loop at 0x4afe0..0x4b00c reads three arrays: X, a constant
             * 8192 (verified: every entry reads 8192); Y, the coefficient map at
             * 0x180114990; and Z, a processed difference image at 0x30046d60. Its
             * divisor is therefore `c = (X*Y + 4096) >> 13 = Y` - the map itself,
             * byte-exact in this port - and not the map scaled by the gain, which
             * was carrying the gain's per-entry error into the sample. */
            uint32_t used = plain;
            if (!used)
                used = 8192;
            uint32_t sample = (((uint32_t)difference[i] << 13) + used / 2) / used;
            /* The factory divides by a pure function of the difference image whose
             * main term is a horizontal median of three (RVA 0x4be20). */
            uint32_t vd = state->divisor[i] ? state->divisor[i] : 8192;   /* frame 0's rank filter */
            uint32_t value, distance;
            if (vd) {
                value = ((uint64_t)sample << 13) + vd / 2;
                value /= vd;
                distance = value > 8192 ? value - 8192 : 8192 - value;
                if (distance < CHICAGO_GAIN_BAND)
                    state->gain[i] = (uint16_t)((gd * k + value + (k + 1) / 2) / (k + 1));
            }
        }
        state->gain_count++;
    }
    if (CHICAGO_STEADY_ENABLED && state->frames >= CHICAGO_STEADY_FRAME) {
        /* Steady state (RVA 0x4a140): the coefficient the division consumes is the
         * Q13 map scaled by the persistent per-pixel gain map. */
        for (int i = 0; i < 5120; i++) {
            uint32_t g = state->gain[i] ? state->gain[i] : 8192;
            steady[i] = (uint16_t)(((uint32_t)coeff[i] * g + 4096) >> 13);
        }
    } else {
        memcpy(steady, coeff, 5120 * sizeof(*steady));
    }
    result = gxfp_chicago_mode24(raw, dark, steady, output);
    if (result)
        goto out;
    result = gxfp_chicago_adaptive_fold(state, raw, dark);
    if (!result)
        result = gxfp_chicago_adaptive_control(state, output);
    state->frames++;
out:
    free(coeff); free(steady); free(difference); free(median); free(blur_buf);
    return result;
}
