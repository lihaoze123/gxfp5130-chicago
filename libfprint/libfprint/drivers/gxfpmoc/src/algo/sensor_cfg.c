#include "gxfp/algo/sensor_cfg.h"

#include <errno.h>
#include <string.h>

enum {
    GXFP_MILANL_OTP_IDX_TCODE = 0x16,
    GXFP_MILANL_OTP_IDX_TCODE_NEG = 0x17,
    GXFP_MILANL_OTP_IDX_DAC = 0x1f,

    GXFP_MILANL_DEFAULT_TCODE = 0x80,
    GXFP_MILANL_DEFAULT_FDT_DELTA = 0x15,

    GXFP_CFG_HEADER_LEN = 0x11,
    GXFP_CFG_CHECKSUM_WORDS = 0x6f,
    GXFP_CFG_CHECKSUM_OFF = 0xde,
};

static uint16_t
gxfp_le16_at(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void
gxfp_cfg_recalc_checksum(uint8_t *cfg,
                         size_t cfg_len)
{
    int16_t sum = (int16_t)0xa5a5;
    size_t i;

    if (!cfg || cfg_len < GXFP_MILANL_CFG_LEN)
        return;

    for (i = 0; i < GXFP_CFG_CHECKSUM_WORDS; i++) {
        size_t off = i * 2u;
        int16_t w = (int16_t)gxfp_le16_at(cfg + off);
        sum = (int16_t)(sum + w);
    }

    sum = (int16_t)(-sum);
    cfg[GXFP_CFG_CHECKSUM_OFF] = (uint8_t)(sum & 0xff);
    cfg[GXFP_CFG_CHECKSUM_OFF + 1] = (uint8_t)(((uint16_t)sum >> 8) & 0xff);
}

static int
gxfp_cfg_patch_reg(uint8_t *cfg,
                   size_t cfg_len,
                   uint16_t reg,
                   uint16_t val,
                   uint8_t section_idx,
                   uint8_t write_mode)
{
    uint8_t section_off;
    uint8_t section_len;
    uint8_t step;

    if (!cfg || cfg_len != GXFP_MILANL_CFG_LEN)
        return -EINVAL;
    if (section_idx > 7)
        return -EINVAL;

    section_off = cfg[1u + ((size_t)section_idx * 2u)];
    section_len = cfg[2u + ((size_t)section_idx * 2u)];
    if ((size_t)section_off + (size_t)section_len > cfg_len)
        return -EBADMSG;

    for (step = 0; step < section_len; step = (uint8_t)(step + 4u)) {
        size_t off = (size_t)section_off + (size_t)step;
        uint16_t cur;

        if (off + 4u > cfg_len)
            return -EBADMSG;

        cur = gxfp_le16_at(cfg + off);
        if (cur != reg)
            continue;

        if (write_mode == 0 || write_mode == 1)
            cfg[off + 2u] = (uint8_t)(val & 0xffu);
        if (write_mode == 0 || write_mode == 2)
            cfg[off + 3u] = (uint8_t)((val >> 8) & 0xffu);

        gxfp_cfg_recalc_checksum(cfg, cfg_len);
        return 0;
    }

    return -ENOENT;
}

static const uint8_t gxfp_milanl_cfg_template[GXFP_MILANL_CFG_LEN] = {
    0x30, 0x11, 0x64, 0x75, 0x00, 0x75, 0x2c, 0xa1, 0x1c, 0xbd, 0x18, 0xd5, 0x00, 0xd5, 0x00, 0xd5,
    0x00, 0xba, 0x00, 0x00, 0x80, 0xca, 0x00, 0x06, 0x00, 0x84, 0x00, 0xbe, 0xb2, 0x86, 0x00, 0xc5,
    0xb9, 0x88, 0x00, 0xb5, 0xad, 0x8a, 0x00, 0x9d, 0x95, 0x8c, 0x00, 0x00, 0xbe, 0x8e, 0x00, 0x00,
    0xc5, 0x90, 0x00, 0x00, 0xb5, 0x92, 0x00, 0x00, 0x9d, 0x94, 0x00, 0x00, 0xaf, 0x96, 0x00, 0x00,
    0xbf, 0x98, 0x00, 0x00, 0xb6, 0x9a, 0x00, 0x00, 0xa7, 0xd2, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00,
    0x00, 0xd6, 0x00, 0x00, 0x00, 0xd8, 0x00, 0x00, 0x00, 0x12, 0x00, 0x03, 0x04, 0xd0, 0x00, 0x00,
    0x00, 0x70, 0x00, 0x00, 0x00, 0x72, 0x00, 0x78, 0x56, 0x74, 0x00, 0x34, 0x12, 0x20, 0x00, 0x10,
    0x40, 0x20, 0x02, 0x08, 0x10, 0x2a, 0x01, 0x82, 0x03, 0x22, 0x00, 0x01, 0x20, 0x24, 0x00, 0x14,
    0x00, 0x80, 0x00, 0x01, 0x04, 0x5c, 0x00, 0x00, 0x01, 0x56, 0x00, 0x0c, 0x24, 0x58, 0x00, 0x05,
    0x00, 0x32, 0x00, 0x08, 0x02, 0x66, 0x00, 0x00, 0x02, 0x7c, 0x00, 0x00, 0x38, 0x82, 0x00, 0x80,
    0x15, 0x2a, 0x01, 0x08, 0x00, 0x5c, 0x00, 0x80, 0x00, 0x54, 0x00, 0x00, 0x01, 0x62, 0x00, 0x38,
    0x04, 0x64, 0x00, 0x10, 0x00, 0x66, 0x00, 0x00, 0x02, 0x7c, 0x00, 0x01, 0x38, 0x2a, 0x01, 0x08,
    0x00, 0x5c, 0x00, 0x80, 0x00, 0x52, 0x00, 0x08, 0x00, 0x54, 0x00, 0x00, 0x01, 0x66, 0x00, 0x00,
    0x02, 0x7c, 0x00, 0x01, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

int gxfp_milanl_parse_otp(const uint8_t *otp,
                          size_t otp_len,
                          struct gxfp_milanl_otp_cfg *out)
{
    uint8_t raw_tcode;

    if (!otp || !out)
        return -EINVAL;
    if (otp_len <= GXFP_MILANL_OTP_IDX_DAC)
        return -EBADMSG;

    memset(out, 0, sizeof(*out));
    out->tcode = GXFP_MILANL_DEFAULT_TCODE;
    out->fdt_delta = GXFP_MILANL_DEFAULT_FDT_DELTA;

    out->dac = (uint16_t)(((uint16_t)otp[GXFP_MILANL_OTP_IDX_DAC] << 4) | 0x0008u);
    out->has_dac = 1;

    raw_tcode = otp[GXFP_MILANL_OTP_IDX_TCODE];
    if (raw_tcode != 0 && (uint8_t)(raw_tcode + otp[GXFP_MILANL_OTP_IDX_TCODE_NEG]) == 0xffu) {
        uint16_t tcode;
        uint32_t fdt_delta;

        tcode = (uint16_t)((((uint16_t)(raw_tcode >> 4)) + 1u) * 0x10u);
        if (tcode != 0) {
            fdt_delta = (((((uint32_t)(raw_tcode & 0x0fu) + 2u) * 0x6400u) / (uint32_t)tcode) / 3u) >> 4;
            out->tcode = tcode;
            out->fdt_delta = (uint16_t)(fdt_delta & 0xffffu);
            out->has_tcode_delta = 1;
        }
    }

    return 0;
}

int gxfp_milanl_prepare_cfg_blob(const uint8_t *template_data,
                                 size_t template_len,
                                 uint8_t *out_cfg,
                                 size_t out_cfg_cap)
{
    if (!template_data || !out_cfg)
        return -EINVAL;
    if (template_len != GXFP_MILANL_CFG_LEN)
        return -EINVAL;
    if (out_cfg_cap < template_len)
        return -EMSGSIZE;

    memcpy(out_cfg, template_data, template_len);
    return (int)template_len;
}

int gxfp_milanl_apply_otp_patch(uint8_t *cfg,
                                size_t cfg_len,
                                const struct gxfp_milanl_otp_cfg *otp_cfg)
{
    int r;

    if (!cfg || !otp_cfg)
        return -EINVAL;
    if (cfg_len != GXFP_MILANL_CFG_LEN)
        return -EINVAL;

    r = gxfp_cfg_patch_reg(cfg, cfg_len, 0x0220u, otp_cfg->dac, 0, 0);
    if (r < 0)
        return r;

    if (otp_cfg->has_tcode_delta) {
        r = gxfp_cfg_patch_reg(cfg, cfg_len, 0x005cu, otp_cfg->tcode, 4, 0);
        if (r < 0)
            return r;

        r = gxfp_cfg_patch_reg(cfg,
                               cfg_len,
                               0x0082u,
                               (uint16_t)(otp_cfg->fdt_delta << 8),
                               2,
                               2);
        if (r < 0)
            return r;
    }

    return 0;
}

int gxfp_milanl_apply_default_patch(uint8_t *cfg,
                                    size_t cfg_len)
{
    struct gxfp_milanl_otp_cfg defaults;

    if (!cfg)
        return -EINVAL;

    memset(&defaults, 0, sizeof(defaults));
    defaults.tcode = GXFP_MILANL_DEFAULT_TCODE;
    defaults.fdt_delta = GXFP_MILANL_DEFAULT_FDT_DELTA;
    defaults.dac = 0x0008u;
    defaults.has_dac = 1;
    defaults.has_tcode_delta = 1;

    return gxfp_milanl_apply_otp_patch(cfg, cfg_len, &defaults);
}

int gxfp_milanl_get_default_cfg_template(const uint8_t **out_template,
                                         size_t *out_len)
{
    if (!out_template || !out_len)
        return -EINVAL;

    *out_template = gxfp_milanl_cfg_template;
    *out_len = sizeof(gxfp_milanl_cfg_template);
    return 0;
}

/* DAC as ChicagoHU_check_otp: reg 0x220 = (dac0 << 4) | 8, regs 0x236/0x238/0x23a = dac1..3. */
int gxfp_chicagohs_prepare_cfg(const uint8_t *template_data, size_t template_len,
                               const uint16_t dac[4], uint8_t *out, size_t out_len)
{
    int r;

    if (!template_data || !dac || !out || out_len != GXFP_MILANL_CFG_LEN)
        return -EINVAL;
    /* 0xe0-byte Goodix config: byte 1 is the first section offset (0x11) */
    if (template_len != GXFP_MILANL_CFG_LEN || template_data[1] != 0x11)
        return -EBADMSG;

    memcpy(out, template_data, GXFP_MILANL_CFG_LEN);
    r = gxfp_cfg_patch_reg(out, out_len, 0x0220u, (uint16_t)((dac[0] << 4) | 0x8u), 0, 0);
    if (r == 0)
        r = gxfp_cfg_patch_reg(out, out_len, 0x0236u, dac[1], 0, 0);
    if (r == 0)
        r = gxfp_cfg_patch_reg(out, out_len, 0x0238u, dac[2], 0, 0);
    if (r == 0)
        r = gxfp_cfg_patch_reg(out, out_len, 0x023au, dac[3], 0, 0);
    return r;
}

/* Recovered from gfspi.dll 1.1.18.25: CRC-8/0x07, init 0, xorout 0xff. */
static uint8_t chicago_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    size_t i;
    unsigned int bit;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80) ? 0x07 : 0));
    }
    return (uint8_t)(crc ^ 0xff);
}

int gxfp_chicagohs_parse_dac(const uint8_t *otp, size_t len, uint16_t dac[4])
{
    uint8_t ft[19], mt[27];
    int base = -1, same = 0;
    size_t i;
    if (!otp || !dac)
        return -EINVAL;
    if (len < 64)
        return -EBADMSG;
    memcpy(ft, otp + 0x0b, 9);
    ft[9] = otp[0x1c];
    memcpy(ft + 10, otp + 0x32, 4);
    memcpy(ft + 14, otp + 0x38, 4);
    ft[18] = otp[0x3e];
    memcpy(mt, otp + 0x14, 8);
    memcpy(mt + 8, otp + 0x1d, 7);
    memcpy(mt + 15, otp + 0x28, 10);
    memcpy(mt + 25, otp + 0x36, 2);
    if (chicago_crc8(ft, sizeof(ft)) == otp[0x3d] ||
        (otp[0x32] && otp[0x33] && otp[0x34] && otp[0x35] &&
         chicago_crc8(otp + 0x32, 4) == otp[0x3e]))
        base = 0x32;
    else if (chicago_crc8(mt, sizeof(mt)) == otp[0x3f] ||
             (otp[0x2e] && otp[0x2f] && otp[0x30] && otp[0x31] &&
              chicago_crc8(otp + 0x2e, 4) == otp[0x16]))
        base = 0x2e;
    if (base >= 0) {
        for (i = 0; i < 4; i++)
            dac[i] = otp[base + i];
        return base;
    }
    for (i = 0; i < 4; i++)
        same += otp[0x2e + i] == otp[0x32 + i] && otp[0x32 + i] != 0;
    if (same < 3)
        return -ENODATA;
    for (i = 0; i < 4; i++)
        dac[i] = otp[0x2e + i] == otp[0x32 + i] ? otp[0x32 + i] :
                 (otp[0x2e + ((i + 1) % 4)] + otp[0x2e + ((i + 2) % 4)] +
                  otp[0x2e + ((i + 3) % 4)]) / 3;
    return 0; /* Recovered from redundant copies. */
}

/* ChicagoHU OTP initialization, gfspi.dll 1.1.18.25 RVA 0x2d529..0x2da73.
 * This is distinct from Milan's 0x16/0x17 layout and includes a 0x40 bias.
 * Persisted Windows runtime overrides are deliberately not imported here. */
int gxfp_chicagohs_parse_timing(const uint8_t *otp, size_t len,
                               struct gxfp_milanl_otp_cfg *out)
{
    uint8_t raw;
    if (!otp || !out)
        return -EINVAL;
    if (len < 64)
        return -EBADMSG;
    memset(out, 0, sizeof(*out));
    if (otp[0x2a] && (unsigned)otp[0x2a] + otp[0x2b] == 255)
        raw = otp[0x2a];
    else if (otp[0x2d] && (unsigned)otp[0x2d] + otp[0x2b] == 255)
        raw = otp[0x2d];
    else if (otp[0x2a] && otp[0x2a] == otp[0x2d])
        raw = otp[0x2a];
    else
        return -ENODATA;
    out->tcode = (uint16_t)((((raw >> 4) + 1u) << 4) + 0x40u);
    out->fdt_delta = (uint16_t)(((((raw & 15u) + 2u) * 25600u /
                                 out->tcode) / 3u) >> 4);
    out->has_tcode_delta = 1;
    return 0;
}

int gxfp_chicagohs_apply_timing(uint8_t *cfg, size_t len,
                               const struct gxfp_milanl_otp_cfg *timing)
{
    int r;
    if (!cfg || !timing || !timing->has_tcode_delta)
        return -EINVAL;
    /* Chicago capture timing is in section 0; FDT delta is in section 2. */
    r = gxfp_cfg_patch_reg(cfg, len, 0x005c, timing->tcode, 0, 0);
    if (r < 0)
        return r;
    return gxfp_cfg_patch_reg(cfg, len, 0x0082,
                            (uint16_t)(timing->fdt_delta << 8), 2, 2);
}
