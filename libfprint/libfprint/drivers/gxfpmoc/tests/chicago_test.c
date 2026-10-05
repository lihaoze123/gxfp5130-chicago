#include "gxfp/algo/sensor_cfg.h"
#include "gxfp/flow/fdt.h"
#include "gxfp/proto/goodix_proto.h"
#include "gxfp/proto/goodix_constants.h"
#include "gxfp/proto/goodix_cmd.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

static int feed(struct gxfp_fdt_flow *flow, size_t len, unsigned touch, uint32_t *events)
{
    uint8_t payload[16] = {0, 1}, wire[32];
    struct gxfp_tap_hdr tap = {0};
    struct gxfp_dev dev = {0};
    int n;
    payload[2] = touch;
    tap.type = GOODIX_MP_TYPE_NOTICE;
    n = gxfp_goodix_build_frame(GXFP_CMD_FDT_MODE, payload, len, wire, sizeof(wire));
    assert(n > 0);
    return gxfp_fdt_flow_feed_record(flow, &dev, &tap, wire, n, events);
}

int main(void)
{
    uint8_t otp[64] = {0};
    uint16_t dac[4];
    struct gxfp_milanl_otp_cfg timing;
    uint8_t config[GXFP_MILANL_CFG_LEN] = {0};
    struct gxfp_fdt_flow flow;
    uint32_t events;
    assert(gxfp_chicagohs_parse_dac(otp, 63, dac) == -EBADMSG);
    assert(gxfp_chicagohs_parse_dac(otp, sizeof(otp), dac) == -ENODATA);
    /* Three agreeing redundant bytes allow Windows to recover the fourth. */
    otp[0x2e] = otp[0x32] = 10;
    otp[0x2f] = otp[0x33] = 20;
    otp[0x30] = otp[0x34] = 30;
    otp[0x31] = 100;
    otp[0x35] = 200;
    assert(gxfp_chicagohs_parse_dac(otp, sizeof(otp), dac) == 0);
    assert(dac[0] == 10 && dac[1] == 20 && dac[2] == 30 && dac[3] == 20);
    /* Known CRC8 vector: bytes 10,20,30,200 -> 0x9d. */
    otp[0x3e] = 0x9d;
    assert(gxfp_chicagohs_parse_dac(otp, sizeof(otp), dac) == 0x32);
    assert(dac[3] == 200);

    assert(gxfp_chicagohs_parse_timing(NULL, 64, &timing) == -EINVAL);
    assert(gxfp_chicagohs_parse_timing(otp, 63, &timing) == -EBADMSG);
    assert(gxfp_chicagohs_parse_timing(otp, 64, &timing) == -ENODATA);
    otp[0x2a] = otp[0x2d] = 0xcd;
    otp[0x2b] = 0x32;
    assert(gxfp_chicagohs_parse_timing(otp, 64, &timing) == 0);
    assert(timing.tcode == 0x110 && timing.fdt_delta == 29);
    otp[0x2a] = 0; /* recover from the backup and inverse */
    assert(gxfp_chicagohs_parse_timing(otp, 64, &timing) == 0);
    assert(timing.tcode == 0x110);

    /* Independent synthetic config: timing in section 0, delta in section 2. */
    config[1] = 0x11;
    config[2] = 4;
    config[5] = 0x15;
    config[6] = 4;
    config[0x11] = 0x5c;
    config[0x15] = 0x82;
    config[0x17] = 0x80;
    assert(gxfp_chicagohs_apply_timing(config, sizeof(config), &timing) == 0);
    assert(config[0x13] == 0x10 && config[0x14] == 1);
    assert(config[0x17] == 0x80 && config[0x18] == 29);
    otp[0x2a] = 0xcd;
    otp[0x2b] = 0; /* recover from agreeing primary and backup */
    assert(gxfp_chicagohs_parse_timing(otp, 64, &timing) == 0);
    assert(timing.tcode == 0x110);

    gxfp_fdt_flow_init(&flow);
    flow.cmd.chicago = 1;
    flow.mode = GXFP_FDT_MODE_WAIT_UP;
    flow.chicago_manual_seen = 1;
    assert(feed(&flow, 4, 0, &events) == -EBADMSG);
    assert(flow.chicago_manual_seen == 1 && events == GXFP_FDT_EVENT_NONE);
    assert(feed(&flow, 16, 0, &events) == 0);
    assert(events == GXFP_FDT_EVENT_UP);
    flow.state = GXFP_FDT_STATE_UNKNOWN;
    assert(feed(&flow, 16, 63, &events) == -EAGAIN);
    assert(events == GXFP_FDT_EVENT_NONE);
    assert(gxfp_fdt_flow_wait_up_retry_due(&flow, flow.wait_up_armed_ms + 749) == 0);
    assert(gxfp_fdt_flow_wait_up_retry_due(&flow, flow.wait_up_armed_ms + 750) == 1);
    flow.chicago_manual_seen = 31;
    assert(feed(&flow, 16, 63, &events) == -ETIMEDOUT);
    assert(events == GXFP_FDT_EVENT_NONE);
    return 0;
}
