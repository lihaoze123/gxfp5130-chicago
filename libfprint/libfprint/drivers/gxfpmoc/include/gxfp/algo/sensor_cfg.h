#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXFP_MILANL_CFG_LEN 0xE0u

struct gxfp_milanl_otp_cfg {
    uint16_t tcode;
    uint16_t fdt_delta;
    uint16_t dac;

    uint8_t has_dac;
    uint8_t has_tcode_delta;
};

int gxfp_milanl_parse_otp(const uint8_t *otp,
                          size_t otp_len,
                          struct gxfp_milanl_otp_cfg *out);

int gxfp_milanl_prepare_cfg_blob(const uint8_t *template_data,
                                 size_t template_len,
                                 uint8_t *out_cfg,
                                 size_t out_cfg_cap);

int gxfp_milanl_apply_otp_patch(uint8_t *cfg,
                                size_t cfg_len,
                                const struct gxfp_milanl_otp_cfg *otp_cfg);

int gxfp_milanl_apply_default_patch(uint8_t *cfg,
                                    size_t cfg_len);

int gxfp_milanl_get_default_cfg_template(const uint8_t **out_template,
                                         size_t *out_len);

/* Returns OTP base 0x32/0x2e, 0 for redundant recovery, or negative errno. */
int gxfp_chicagohs_parse_dac(const uint8_t *otp, size_t len, uint16_t dac[4]);
int gxfp_chicagohs_parse_timing(const uint8_t *otp, size_t len,
                               struct gxfp_milanl_otp_cfg *out);
int gxfp_chicagohs_apply_timing(uint8_t *cfg, size_t len,
                               const struct gxfp_milanl_otp_cfg *timing);

/* ChicagoHS config template: the 0xe0-byte blob from the user's own Windows driver
 * (see scripts/extract-chicagohs-config.py); not shipped with this code. */
int gxfp_chicagohs_prepare_cfg(const uint8_t *template_data, size_t template_len,
                               const uint16_t dac[4], uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif
