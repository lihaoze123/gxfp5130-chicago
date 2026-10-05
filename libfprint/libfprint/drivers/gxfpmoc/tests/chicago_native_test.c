#include "gxfp/algo/image/chicago_native.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static void test_calibration_and_mode24(void);
static void test_adaptive_reference(void);

int main(void)
{
    test_calibration_and_mode24();
    test_adaptive_reference();
    uint16_t source[5120],words[5120];
    uint8_t mask[5120],output[5120];
    struct gxfp_chicago_spatial_quality q;
    for(int i=0;i<5120;i++)source[i]=6123;
    memset(mask,255,sizeof(mask));
    assert(gxfp_chicago_spatial(source,mask,output,&q)==0);
    assert(q.weak_count==5120 && q.weak_image==1 && q.histogram[0]==5120);
    for(int i=0;i<5120;i++)assert(output[i]==0 && q.weak_pixels[i]==255);
    memset(mask,0,sizeof(mask));
    mask[1000]=2; /* nonbinary masks are valid */
    assert(gxfp_chicago_spatial(source,mask,output,&q)==0);
    assert(q.weak_count==1 && q.weak_image==0);
    for(int i=0;i<5120;i++)assert(output[i]==(i==1000?0:255));
    mask[1000]=0;
    assert(gxfp_chicago_spatial(source,mask,output,&q)==0);
    assert(q.weak_count==0 && q.weak_image==0);
    for(int i=0;i<5120;i++)assert(output[i]==255);
    memset(source,0,sizeof(source));source[0]=65535;
    memset(mask,1,sizeof(mask));
    assert(gxfp_chicago_local_background(source,mask,words,64,80)==0);
    assert(words[0]==1179); /* rounded 36-sample mean, then low-word store */
    assert(gxfp_chicago_local_background(source,mask,source,64,80)==0);
    assert(memcmp(source,words,sizeof(words))==0);
    assert(gxfp_chicago_local_background(source,mask,words,65,80)==-EINVAL);
    assert(gxfp_chicago_local_background(source,mask,words,64,81)==-EINVAL);
    assert(gxfp_chicago_smooth(NULL,64,80)==-EINVAL);
    puts("native spatial invariants: flat, invalid, sparse, overflow, in-place, bounds PASS");
    return 0;
}
/* Calibration coefficient map, the Q13 scaling, the column stage and the
 * recovered mode-24 path. */
static void test_calibration_and_mode24(void)
{
    uint16_t calib[5120], coeff[5120], raw[5120], dark[5120], scaled[5120];
    uint8_t output[5120];
    for (int i = 0; i < 5120; i++) {
        calib[i] = 2000;
        raw[i] = 1200;
        dark[i] = 2800;
        coeff[i] = 0xdead;
    }
    /* A uniform calibration has a rounded mean of 2000 and gives unity gain. */
    assert(gxfp_chicago_coeff_from_calibration(calib, coeff, 5120) == 0);
    for (int i = 0; i < 5120; i++)
        assert(coeff[i] == 8192);
    /* Zero entries keep the caller's word, as the factory code does. */
    calib[0] = 0;
    coeff[0] = 1234;
    assert(gxfp_chicago_coeff_from_calibration(calib, coeff, 5120) == 0);
    assert(coeff[0] == 1234);
    /* Large calibration values clamp instead of wrapping. */
    calib[0] = 65535;
    assert(gxfp_chicago_coeff_from_calibration(calib, coeff, 5120) == 0);
    assert(coeff[0] == 65535);
    assert(gxfp_chicago_coeff_from_calibration(NULL, coeff, 5120) == -EINVAL);
    assert(gxfp_chicago_coeff_from_calibration(calib, coeff, 0) == -EINVAL);
    for (int i = 0; i < 5120; i++) { calib[i] = 0; }
    assert(gxfp_chicago_coeff_from_calibration(calib, coeff, 5120) == -EINVAL);
    /* Unity coefficient means the scaling is the identity. */
    for (int i = 0; i < 5120; i++) {
        calib[i] = 2000;
        scaled[i] = 0;
    }
    assert(gxfp_chicago_coeff_from_calibration(calib, coeff, 5120) == 0);
    for (int i = 0; i < 5120; i++) raw[i] = (uint16_t)(i % 4096);
    assert(gxfp_chicago_apply_coeff(raw, coeff, scaled, 5120) == 0);
    for (int i = 0; i < 5120; i++) assert(scaled[i] == raw[i]);
    /* A zero coefficient passes the word through. */
    coeff[0] = 0;
    assert(gxfp_chicago_apply_coeff(raw, coeff, scaled, 5120) == 0);
    assert(scaled[0] == raw[0]);
    assert(gxfp_chicago_apply_coeff(NULL, coeff, scaled, 5120) == -EINVAL);
    assert(gxfp_chicago_apply_coeff(raw, coeff, scaled, 0) == -EINVAL);

    /* The factory writes the low 16 bits of the quotient rather than saturating,
     * and passes the difference through untouched where the coefficient is zero.
     * Saturating instead changes a handful of pixels in the steady state, and each
     * of those skews its whole column in the next stage. */
    {
        uint16_t one[3] = { 65535, 4096, 1234 };
        uint16_t zero[3] = { 0, 0, 0 };
        uint16_t unit[3] = { 1, 8192, 8192 };
        uint16_t got[3] = { 0, 0, 0 };
        assert(gxfp_chicago_apply_coeff(one, unit, got, 3) == 0);
        assert(got[0] == (uint16_t)(((65535u << 13) / 1u) & 0xffffu));
        assert(got[1] == (uint16_t)((((4096u << 13) + 4096u) / 8192u) & 0xffffu));
        assert(gxfp_chicago_apply_coeff(one, zero, got, 3) == 0);
        assert(got[0] == 65535 && got[1] == 4096 && got[2] == 1234);
    }
    /* Column background suppression: each column loses its own mean and gains
     * 5000, and anything that would go non-positive becomes black. */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 80; x++)
            raw[y * 80 + x] = (uint16_t)(1000 + x + (y == 0 ? 640 : 0));
    assert(gxfp_chicago_column_background(raw, scaled, 64, 80) == 0);
    /* Column x has mean 1010 + x, so row 0 keeps its 640 extra counts. */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 80; x++)
            assert(scaled[y * 80 + x] == (y == 0 ? 5630 : 4990));
    assert(gxfp_chicago_column_background(NULL, scaled, 64, 80) == -EINVAL);
    assert(gxfp_chicago_column_background(raw, scaled, 65, 80) == -EINVAL);
    /* Dark-equal frames carry no signal, so the delivered image is uniform. */
    for (int i = 0; i < 5120; i++) { calib[i] = 2000; raw[i] = dark[i]; }
    assert(gxfp_chicago_coeff_from_calibration(calib, coeff, 5120) == 0);
    assert(gxfp_chicago_mode24(raw, dark, coeff, output) == 0);
    for (int i = 80; i < 5120; i++) assert(output[i] == output[80]);
    /* Row 0 carries the driver's marker in the low bits (RVA 0x54230): the high
     * bits stay uniform and the low bits alternate over the first 73 columns. */
    for (int x = 0; x < 80; x++) assert((output[x] >> 1) == (output[80] >> 1));
    for (int x = 73; x < 80; x++) assert((output[x] & 1) == 0);
    for (int x = 0; x < 73; x++) assert((output[x] & 1) == (x & 1));
    /* A frame brighter than the dark reference cannot produce negative signal. */
    for (int i = 0; i < 5120; i++) raw[i] = 3800;
    assert(gxfp_chicago_mode24(raw, dark, coeff, output) == 0);
    for (int i = 80; i < 5120; i++) assert(output[i] == output[80]);
    /* Structured input must survive the whole chain. */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 80; x++)
            raw[y * 80 + x] = (uint16_t)(1200 + 400 * (y % 8 < 4));
    assert(gxfp_chicago_mode24(raw, dark, coeff, output) == 0);
    int distinct = 0;
    for (int i = 1; i < 5120; i++) distinct += output[i] != output[0];
    assert(distinct > 1000);
    assert(gxfp_chicago_mode24(NULL, dark, coeff, output) == -EINVAL);
    assert(gxfp_chicago_mode24(raw, dark, NULL, output) == -EINVAL);
    puts("coefficients, Q13 scaling, column stage and mode-24 path PASS");
}

/* Adaptive reference: seeding, the fold formula, the band test, the reference
 * control and saturation. */
static void test_adaptive_reference(void)
{
    static struct gxfp_chicago_adaptive state;
    uint16_t calib[5120], coeff[5120], raw[5120], dark[5120];
    uint8_t delivered[5120], other[5120];
    for (int i = 0; i < 5120; i++) {
        calib[i] = 2000;
        raw[i] = 1200;
        dark[i] = 2800;
        coeff[i] = 0;
        delivered[i] = 100;
        other[i] = 200;
    }
    gxfp_chicago_adaptive_init(&state, calib);
    assert(state.counter == 211);                 /* the factory's activation count */
    for (int i = 0; i < 5120; i++) assert(state.accumulator[i] == 2000);
    /* The session's first frame runs with the calibration map. */
    assert(gxfp_chicago_adaptive_map(&state, raw, dark, coeff) == 0);
    for (int i = 0; i < 5120; i++) assert(coeff[i] == 8192);
    /* The fold lands in state.folded, not in the reference, and does not count
     * until the reference control accepts it. Here v equals the target, so the
     * blend is a no-op and the reference stays uniform. */
    assert(gxfp_chicago_adaptive_fold(&state, raw, dark) == 0);
    assert(state.counter == 211);
    for (int i = 0; i < 5120; i++) assert(state.accumulator[i] == 2000);
    /* The first delivered image is always accepted. */
    assert(gxfp_chicago_adaptive_control(&state, delivered) == 0);
    assert(state.counter == 212);
    for (int i = 0; i < 5120; i++) assert(state.accumulator[i] == 2000);
    /* The next frame's map comes from that accepted fold result. */
    assert(gxfp_chicago_adaptive_map(&state, raw, dark, coeff) == 0);
    for (int i = 0; i < 5120; i++) assert(coeff[i] == 8192);
    /* An identical delivered image means "no movement": the reference control
     * restores the saved reference and keeps the sample count. */
    assert(gxfp_chicago_adaptive_fold(&state, raw, dark) == 0);
    assert(gxfp_chicago_adaptive_control(&state, delivered) == 0);
    assert(state.counter == 212);
    /* A large change is accepted: the count advances and the reference is kept. */
    assert(gxfp_chicago_adaptive_fold(&state, raw, dark) == 0);
    assert(gxfp_chicago_adaptive_control(&state, other) == 0);
    assert(state.counter == 213);
    /* Out-of-band pixels keep their value: v far from the target. */
    for (int i = 0; i < 5120; i++) raw[i] = 4000;   /* v = 0 -> |v - 2000| >= 1600 */
    assert(gxfp_chicago_adaptive_fold(&state, raw, dark) == 0);
    for (int i = 0; i < 5120; i++) assert(state.folded[i] == state.accumulator[i]);
    /* The count saturates at 400 like the factory. */
    for (int i = 0; i < 400; i++) {
        assert(gxfp_chicago_adaptive_fold(&state, raw, dark) == 0);
        assert(gxfp_chicago_adaptive_control(&state, (i & 1) ? delivered : other) == 0);
    }
    assert(state.counter == 400);
    assert(gxfp_chicago_adaptive_map(NULL, raw, dark, coeff) == -EINVAL);
    assert(gxfp_chicago_adaptive_fold(&state, NULL, dark) == -EINVAL);
    assert(gxfp_chicago_adaptive_control(&state, NULL) == -EINVAL);
    puts("adaptive reference: seeding, fold formula, band, control, saturation PASS");
}
