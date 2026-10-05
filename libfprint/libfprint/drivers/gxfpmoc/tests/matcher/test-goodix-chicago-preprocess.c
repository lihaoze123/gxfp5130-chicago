// SPDX-License-Identifier: LGPL-2.1-or-later
/* Copyright (C) 2026 Berke Kabagöz <berkekbgz@gmail.com> */

/* Tests for the native Chicago preprocessing input-state boundary. */

#include <gio/gio.h>
#include <glib.h>
#include <string.h>

#include "goodix-chicago-preprocess.h"

static guint32
calibration_map_crc32 (const guint8 *data,
                       gsize         length)
{
  guint32 crc = G_MAXUINT32;

  for (gsize i = 0; i < length; i++)
    {
      crc ^= data[i];
      for (guint bit = 0; bit < 8; bit++)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }

  return crc;
}

static void
write_le32 (guint8  *data,
            guint32  value)
{
  data[0] = value;
  data[1] = value >> 8;
  data[2] = value >> 16;
  data[3] = value >> 24;
}

static void
write_le16 (guint8  *data,
            guint16  value)
{
  data[0] = value;
  data[1] = value >> 8;
}

static GBytes *
build_calibration (void)
{
  g_autofree guint8 *data =
    g_malloc0 (GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);
  const guint pixel = 37;

  for (guint i = 0; i < GOODIX_CHICAGO_PIXELS; i++)
    write_le16 (data + GOODIX_CHICAGO_GAIN_MAP_OFFSET +
                i * sizeof (guint16), 0x2000);

  write_le16 (data + GOODIX_CHICAGO_GAIN_MAP_OFFSET +
              pixel * sizeof (guint16), 0x3000);
  write_le16 (data + GOODIX_CHICAGO_OFFSET_MAP_OFFSET +
              pixel * sizeof (guint16), 0xabcd);
  write_le32 (data + GOODIX_CHICAGO_GAIN_MAP_CRC_OFFSET,
              calibration_map_crc32 (data + GOODIX_CHICAGO_GAIN_MAP_OFFSET,
                                     GOODIX_CHICAGO_CALIBRATION_MAP_BYTES));
  write_le32 (data + GOODIX_CHICAGO_OFFSET_MAP_CRC_OFFSET,
              calibration_map_crc32 (data + GOODIX_CHICAGO_OFFSET_MAP_OFFSET,
                                     GOODIX_CHICAGO_CALIBRATION_MAP_BYTES));
  return g_bytes_new_take (g_steal_pointer (&data),
                           GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);
}

static GBytes *
build_zero_gain_calibration (void)
{
  g_autofree guint8 *data =
    g_malloc0 (GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);

  write_le32 (data + GOODIX_CHICAGO_GAIN_MAP_CRC_OFFSET,
              calibration_map_crc32 (data + GOODIX_CHICAGO_GAIN_MAP_OFFSET,
                                     GOODIX_CHICAGO_CALIBRATION_MAP_BYTES));
  write_le32 (data + GOODIX_CHICAGO_OFFSET_MAP_CRC_OFFSET,
              calibration_map_crc32 (data + GOODIX_CHICAGO_OFFSET_MAP_OFFSET,
                                     GOODIX_CHICAGO_CALIBRATION_MAP_BYTES));
  return g_bytes_new_take (g_steal_pointer (&data),
                           GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);
}

static void
test_input_state (void)
{
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  guint16 image_base[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 raw[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 prepared[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 gain = 0;
  guint16 offset = 0;
  guint16 normalized_gain = 0;

  image_base[0] = 0x0234;
  image_base[GOODIX_CHICAGO_PIXELS - 1] = 0x0abc;
  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  g_assert_nonnull (preprocessor);
  image_base[0] = 0;
  g_assert_cmpuint (goodix_chicago_preprocessor_get_image_base (preprocessor)[0],
                    ==, 0x0234);
  g_assert_cmpuint (goodix_chicago_preprocessor_get_image_base (preprocessor)
                    [GOODIX_CHICAGO_PIXELS - 1], ==, 0x0abc);

  g_assert_true (goodix_chicago_preprocessor_get_corrections (preprocessor, 37,
                                                                  &gain, &offset));
  g_assert_cmpuint (gain, ==, 0x3000);
  g_assert_cmpuint (offset, ==, 0xabcd);
  g_assert_false (goodix_chicago_preprocessor_get_corrections (
                    preprocessor, GOODIX_CHICAGO_PIXELS, NULL, NULL));

  /* The gain mean is 0x2001, so this checks the vendor's rounded 0x2000
   * fixed-point normalization rather than merely preserving the map. */
  g_assert_true (goodix_chicago_preprocessor_get_normalized_gain (
                    preprocessor, 0, &normalized_gain));
  g_assert_cmpuint (normalized_gain, ==, 0x1fff);
  g_assert_true (goodix_chicago_preprocessor_get_normalized_gain (
                    preprocessor, 37, &normalized_gain));
  g_assert_cmpuint (normalized_gain, ==, 0x2fff);
  g_assert_false (goodix_chicago_preprocessor_get_normalized_gain (
                    preprocessor, GOODIX_CHICAGO_PIXELS, NULL));

  raw[80] = 0;
  raw[81] = 0x0fff;
  raw[82] = 0x1000;
  raw[83] = G_MAXUINT16;
  goodix_chicago_preprocessor_prepare_raw (preprocessor, raw, prepared);
  g_assert_cmpuint (prepared[80], ==, 0);
  g_assert_cmpuint (prepared[81], ==, 0x0fff);
  g_assert_cmpuint (prepared[82], ==, 0x0fff);
  g_assert_cmpuint (prepared[83], ==, 0x0fff);
  g_assert_cmpuint (prepared[1], ==, prepared[81]);
}

static void
fill_resolution_labels (guint8 labels[GOODIX_CHICAGO_PIXELS],
                        guint  class1,
                        guint  class2,
                        guint  class3)
{
  guint offset = 0;

  g_assert_cmpuint (class1 + class2 + class3, <=,
                    GOODIX_CHICAGO_PIXELS);
  memset (labels, 0, GOODIX_CHICAGO_PIXELS);
  memset (labels + offset, 1, class1);
  offset += class1;
  memset (labels + offset, 2, class2);
  offset += class2;
  memset (labels + offset, 3, class3);
}

static void
test_classifies_resolution_labels (void)
{
  guint8 labels[GOODIX_CHICAGO_PIXELS];
  gboolean auxiliary = FALSE;
  guint code = 0;
  guint class3;

  /* Consecutive official live frames on either side of the strict 25%
   * aggregate threshold: 1,210 classes stays at code 6, while 1,300 becomes
   * code 7. */
  fill_resolution_labels (labels, 292, 465, 453);
  class3 = goodix_chicago_preprocessor_classify_resolution_labels (
    0x18, labels, G_N_ELEMENTS (labels), G_N_ELEMENTS (labels),
    &code, &auxiliary);
  g_assert_cmpuint (class3, ==, 453);
  g_assert_cmpuint (code, ==, 6);
  g_assert_true (auxiliary);
  g_assert_cmpuint (labels[0], ==, 0);
  g_assert_cmpuint (labels[292], ==, 0);
  g_assert_cmpuint (labels[292 + 465], ==, 3);

  fill_resolution_labels (labels, 300, 492, 508);
  class3 = goodix_chicago_preprocessor_classify_resolution_labels (
    0x18, labels, G_N_ELEMENTS (labels), G_N_ELEMENTS (labels),
    &code, &auxiliary);
  g_assert_cmpuint (class3, ==, 508);
  g_assert_cmpuint (code, ==, 7);
  g_assert_true (auxiliary);

  /* The percentage comparison is strictly greater-than. At exactly 20%,
   * mode 0x18 falls through to its class-3 count threshold. */
  fill_resolution_labels (labels, 0, 0, 1024);
  goodix_chicago_preprocessor_classify_resolution_labels (
    0x18, labels, G_N_ELEMENTS (labels), G_N_ELEMENTS (labels),
    &code, &auxiliary);
  g_assert_cmpuint (code, ==, 7);
  fill_resolution_labels (labels, 0, 0, 1025);
  goodix_chicago_preprocessor_classify_resolution_labels (
    0x18, labels, G_N_ELEMENTS (labels), G_N_ELEMENTS (labels),
    &code, &auxiliary);
  g_assert_cmpuint (code, ==, 8);
}

static void
test_builds_resolution_labels (void)
{
  const GoodixChicagoResolutionThresholds thresholds = {
    .secondary_hard = 5250,
    .primary_high = 800,
    .primary_mid = 600,
    .secondary_mid = 8500,
    .primary_low = 500,
    .promotion = 733,
  };
  guint16 primary[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 secondary[GOODIX_CHICAGO_PIXELS];
  guint8 input_mask[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint8 labels[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint8 promotion_mask[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint class3_seeds;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    secondary[pixel] = 6000;

  for (guint pixel = 100; pixel <= 107; pixel++)
    input_mask[pixel] = 0xff;
  secondary[100] = 5000;
  primary[101] = 801;
  primary[102] = 700;
  primary[103] = 550;
  primary[104] = 600;
  primary[105] = 500;
  primary[106] = 550;
  labels[106] = 2;
  primary[107] = 900;
  input_mask[107] = 0;

  class3_seeds = goodix_chicago_preprocessor_build_resolution_labels (
    primary, secondary, input_mask, &thresholds, labels, promotion_mask);
  g_assert_cmpuint (class3_seeds, ==, 2);
  g_assert_cmpuint (labels[100], ==, 3);
  g_assert_cmpuint (labels[101], ==, 3);
  g_assert_cmpuint (labels[102], ==, 2);
  g_assert_cmpuint (labels[103], ==, 1);
  g_assert_cmpuint (labels[104], ==, 1);
  g_assert_cmpuint (labels[105], ==, 0);
  g_assert_cmpuint (labels[106], ==, 2);
  g_assert_cmpuint (labels[107], ==, 0);
  g_assert_cmpuint (promotion_mask[100], ==, 0xff);
  g_assert_cmpuint (promotion_mask[102], ==, 0);

  memset (primary, 0, sizeof (primary));
  memset (labels, 0, sizeof (labels));
  memset (input_mask, 0xff, sizeof (input_mask));
  secondary[100] = 6000;
  for (guint x = 10; x <= 60; x++)
    primary[10 * 80 + x] = 801;
  primary[10 * 80 + 61] = 750;
  primary[10 * 80 + 62] = 700;
  primary[10 * 80 + 63] = 550;

  class3_seeds = goodix_chicago_preprocessor_build_resolution_labels (
    primary, secondary, input_mask, &thresholds, labels, promotion_mask);
  g_assert_cmpuint (class3_seeds, ==, 51);
  g_assert_cmpuint (labels[10 * 80 + 60], ==, 3);
  g_assert_cmpuint (labels[10 * 80 + 61], ==, 3);
  g_assert_cmpuint (labels[10 * 80 + 62], ==, 2);
  g_assert_cmpuint (labels[10 * 80 + 63], ==, 1);
  g_assert_cmpuint (promotion_mask[10 * 80 + 61], ==, 0xff);
  g_assert_cmpuint (promotion_mask[10 * 80 + 62], ==, 0);
}

static void
test_builds_resolution_input_mask (void)
{
  guint8 input_mask[GOODIX_CHICAGO_PIXELS];
  guint enabled = 0;

  goodix_chicago_preprocessor_build_resolution_input_mask (input_mask);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    enabled += input_mask[pixel] != 0;

  g_assert_cmpuint (enabled, ==, 3536);
  g_assert_cmpuint (input_mask[5 * 80 + 6], ==, 0);
  g_assert_cmpuint (input_mask[6 * 80 + 5], ==, 0);
  g_assert_cmpuint (input_mask[6 * 80 + 6], ==, 0xff);
  g_assert_cmpuint (input_mask[57 * 80 + 73], ==, 0xff);
  g_assert_cmpuint (input_mask[57 * 80 + 74], ==, 0);
  g_assert_cmpuint (input_mask[58 * 80 + 73], ==, 0);
}

static void
test_builds_resolution_base_plane (void)
{
  guint16 current[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 image_base[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 resolution_base[GOODIX_CHICAGO_PIXELS] = { 0, };

  current[0] = 1000;
  image_base[0] = 2200;
  current[1] = 9000;
  image_base[1] = 1000;
  current[2] = 7095;
  image_base[2] = 0;

  goodix_chicago_preprocessor_build_resolution_base_plane (
    current, image_base, resolution_base);
  g_assert_cmpuint (resolution_base[0], ==, 8295);
  g_assert_cmpuint (resolution_base[1], ==, 0);
  g_assert_cmpuint (resolution_base[2], ==, 0);
  g_assert_cmpuint (resolution_base[3], ==, 7095);
}

static void
test_builds_resolution_secondary_plane (void)
{
  guint16 resolution_base[GOODIX_CHICAGO_PIXELS];
  guint16 secondary[GOODIX_CHICAGO_PIXELS];
  const guint center = 32 * 80 + 40;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    resolution_base[pixel] = 1234;
  goodix_chicago_preprocessor_build_resolution_secondary_plane (
    resolution_base, secondary);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    g_assert_cmpuint (secondary[pixel], ==, 1234);

  memset (resolution_base, 0, sizeof (resolution_base));
  resolution_base[center] = G_MAXUINT16;
  goodix_chicago_preprocessor_build_resolution_secondary_plane (
    resolution_base, secondary);
  g_assert_cmpuint (secondary[center], ==, 40588);
  g_assert_cmpuint (secondary[center - 1], ==, 5492);
  g_assert_cmpuint (secondary[center + 1], ==, 5492);
  g_assert_cmpuint (secondary[center - 80], ==, 5493);
  g_assert_cmpuint (secondary[center + 80], ==, 5493);
  g_assert_cmpuint (secondary[center - 81], ==, 743);
}

static void
test_builds_resolution_gradients (void)
{
  guint16 secondary[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint8 input_mask[GOODIX_CHICAGO_PIXELS];
  guint16 falling[GOODIX_CHICAGO_PIXELS];
  guint8 falling_directions[GOODIX_CHICAGO_PIXELS];
  guint16 rising[GOODIX_CHICAGO_PIXELS];
  guint8 rising_directions[GOODIX_CHICAGO_PIXELS];
  const guint center = 32 * 80 + 40;

  memset (input_mask, 0xff, sizeof (input_mask));
  secondary[center] = 100;
  secondary[center - 1] = 50;
  secondary[center + 1] = 140;
  secondary[center - 80] = 200;
  goodix_chicago_preprocessor_build_resolution_gradients (
    secondary, input_mask, falling, falling_directions,
    rising, rising_directions);
  g_assert_cmpuint (falling[center], ==, 100);
  g_assert_cmpuint (falling_directions[center], ==, 3);
  g_assert_cmpuint (rising[center], ==, 100);
  g_assert_cmpuint (rising_directions[center], ==, 2);
}

static void
test_builds_resolution_primary_plane (void)
{
  guint16 secondary[GOODIX_CHICAGO_PIXELS];
  guint8 input_mask[GOODIX_CHICAGO_PIXELS];
  guint16 primary[GOODIX_CHICAGO_PIXELS];
  const guint pixel = 20 * 80 + 20;

  for (guint y = 0; y < 64; y++)
    for (guint x = 0; x < 80; x++)
      secondary[y * 80 + x] = x * 10;
  goodix_chicago_preprocessor_build_resolution_input_mask (input_mask);

  goodix_chicago_preprocessor_build_resolution_primary_plane (
    secondary, input_mask, G_MAXINT16, G_MAXINT16, FALSE, primary);
  g_assert_cmpuint (primary[pixel], ==, 60);
  goodix_chicago_preprocessor_build_resolution_primary_plane (
    secondary, input_mask, 0, G_MAXINT16, TRUE, primary);
  g_assert_cmpuint (primary[pixel], ==, 60);
}

static void
test_calculates_resolution_histogram_statistics (void)
{
  guint16 secondary[GOODIX_CHICAGO_PIXELS];
  guint16 filtered[GOODIX_CHICAGO_PIXELS];
  guint16 primary[GOODIX_CHICAGO_PIXELS];
  guint8 input_mask[GOODIX_CHICAGO_PIXELS];
  GoodixChicagoResolutionStatistics statistics = { 0, };

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    {
      secondary[pixel] = 1000;
      primary[pixel] = 500;
    }
  goodix_chicago_preprocessor_build_resolution_input_mask (input_mask);
  goodix_chicago_preprocessor_build_resolution_filtered_gradient (
    secondary, input_mask, filtered);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    g_assert_cmpuint (filtered[pixel], ==, 0);
  g_assert_cmpint (
    goodix_chicago_preprocessor_calculate_resolution_gradient_threshold (
      filtered, input_mask),
    ==, 300);

  goodix_chicago_preprocessor_calculate_resolution_primary_statistics (
    primary, input_mask, &statistics);
  g_assert_cmpint (statistics.primary_center_a, ==, 500);
  g_assert_cmpint (statistics.primary_center_b, ==, 500);
  g_assert_cmpint (statistics.primary_high_sample, ==, 500);
  g_assert_cmpint (statistics.primary_mid_sample, ==, 500);
}

static void
test_calculates_resolution_secondary_analysis (void)
{
  guint16 secondary[GOODIX_CHICAGO_PIXELS];
  guint16 filtered[GOODIX_CHICAGO_PIXELS];
  guint8 input_mask[GOODIX_CHICAGO_PIXELS];
  GoodixChicagoResolutionSecondaryAnalysis analysis = { 0, };
  GoodixChicagoResolutionStatistics gradient_statistics = { 0, };
  gboolean use_exceptional = FALSE;
  gboolean use_normal = FALSE;
  gint branch_state = -1;

  for (guint y = 0; y < 64; y++)
    for (guint x = 0; x < 80; x++)
      secondary[y * 80 + x] =
        7600 + ((x * 37 + y * 53 + (x * y) % 97) % 1300);
  goodix_chicago_preprocessor_build_resolution_input_mask (input_mask);
  goodix_chicago_preprocessor_calculate_resolution_secondary_analysis (
    secondary, input_mask, &analysis);

  g_assert_cmpint (analysis.upper_cutoff, ==, 8649);
  g_assert_cmpint (analysis.upper_state, ==, 5);
  g_assert_cmpint (analysis.lower_cutoff, ==, 7849);
  g_assert_cmpint (analysis.lower_state, ==, 5);
  g_assert_cmpint (analysis.upper_mid_sample, ==, 8639);
  g_assert_cmpint (analysis.balance_center, ==, 8248);
  g_assert_cmpint (analysis.upper_outer_mean, ==, 8866);
  g_assert_cmpint (analysis.upper_inner_mean, ==, 8834);
  g_assert_cmpint (analysis.lower_outer_mean, ==, 7629);
  g_assert_cmpint (analysis.lower_inner_mean, ==, 7665);
  g_assert_cmpint (analysis.peak_state, ==, 1);
  g_assert_cmpint (analysis.peak_value, ==, 8248);

  goodix_chicago_preprocessor_build_resolution_filtered_gradient (
    secondary, input_mask, filtered);
  goodix_chicago_preprocessor_calculate_resolution_primary_statistics (
    filtered, input_mask, &gradient_statistics);
  goodix_chicago_preprocessor_select_resolution_branches (
    &analysis, &gradient_statistics,
    &use_exceptional, &use_normal, &branch_state);
  g_assert_true (use_exceptional);
  g_assert_true (use_normal);
  g_assert_cmpint (branch_state, ==, 0);
}

static void
test_calculates_resolution_thresholds (void)
{
  GoodixChicagoResolutionStatistics statistics = {
    .primary_center_a = 522,
    .primary_center_b = 514,
    .primary_high_sample = 793,
    .primary_mid_sample = 724,
    .branch_state = 2,
  };
  GoodixChicagoResolutionThresholds thresholds;

  goodix_chicago_preprocessor_calculate_resolution_thresholds (
    &statistics, FALSE, &thresholds);
  g_assert_cmpint (thresholds.secondary_hard, ==, 5250);
  g_assert_cmpint (thresholds.primary_high, ==, 793);
  g_assert_cmpint (thresholds.primary_mid, ==, 621);
  g_assert_cmpint (thresholds.primary_low, ==, 568);
  g_assert_cmpint (thresholds.promotion, ==, 733);

  statistics.primary_center_a = 488;
  statistics.primary_center_b = 485;
  statistics.primary_high_sample = 814;
  statistics.primary_mid_sample = 727;
  statistics.branch_state = 0;
  statistics.secondary_base = 8642;
  statistics.secondary_mid_sample = 8545;
  goodix_chicago_preprocessor_calculate_resolution_thresholds (
    &statistics, TRUE, &thresholds);
  g_assert_cmpint (thresholds.secondary_hard, ==, 9150);
  g_assert_cmpint (thresholds.primary_high, ==, 814);
  g_assert_cmpint (thresholds.primary_mid, ==, 606);
  g_assert_cmpint (thresholds.secondary_mid, ==, 8762);
  g_assert_cmpint (thresholds.primary_low, ==, 486);
  g_assert_cmpint (thresholds.promotion, ==, 754);

  statistics.branch_state = 2;
  goodix_chicago_preprocessor_calculate_resolution_thresholds (
    &statistics, TRUE, &thresholds);
  g_assert_cmpint (thresholds.primary_high, ==, 874);
  g_assert_cmpint (thresholds.promotion, ==, 814);
}

static void
test_builds_exceptional_resolution_labels (void)
{
  const GoodixChicagoResolutionThresholds thresholds = {
    .secondary_hard = 9000,
    .primary_high = 800,
    .primary_mid = 600,
    .secondary_mid = 8500,
    .primary_low = 500,
    .promotion = 740,
  };
  guint16 primary[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 secondary[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint8 input_mask[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint8 labels[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint8 promotion_mask[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint class3_seeds;

  memset (secondary, 0, sizeof (secondary));
  for (guint pixel = 100; pixel <= 105; pixel++)
    input_mask[pixel] = 0xff;
  secondary[100] = 9001;
  primary[101] = 801;
  primary[102] = 700;
  secondary[102] = 8600;
  primary[103] = 700;
  secondary[103] = 8400;
  primary[104] = 550;
  secondary[105] = 9000;

  class3_seeds =
    goodix_chicago_preprocessor_build_exceptional_resolution_labels (
      primary, secondary, input_mask, &thresholds, labels, promotion_mask);
  g_assert_cmpuint (class3_seeds, ==, 3);
  g_assert_cmpuint (labels[100], ==, 3);
  g_assert_cmpuint (labels[101], ==, 3);
  g_assert_cmpuint (labels[102], ==, 3);
  g_assert_cmpuint (labels[103], ==, 2);
  g_assert_cmpuint (labels[104], ==, 1);
  g_assert_cmpuint (labels[105], ==, 0);
  g_assert_cmpuint (promotion_mask[100], ==, 0xff);
  g_assert_cmpuint (promotion_mask[101], ==, 0xff);
  g_assert_cmpuint (promotion_mask[102], ==, 0);
}

static void
test_packs_resolution_codes (void)
{
  static const guint expected[11] = {
    0x000, 0x200, 0x200, 0x300, 0x000, 0x100,
    0x200, 0x400, 0x500, 0x500, 0x000,
  };

  for (guint code = 0; code < G_N_ELEMENTS (expected); code++)
    g_assert_cmpuint (
      goodix_chicago_preprocessor_pack_resolution_code (code), ==,
      expected[code]);
}

static void
test_rejects_bad_payload (void)
{
  g_autoptr(GBytes) malformed = g_bytes_new_static ("bad", 3);
  g_autoptr(GError) error = NULL;
  guint16 image_base[GOODIX_CHICAGO_PIXELS] = { 0, };

  g_assert_null (goodix_chicago_preprocessor_new (malformed, image_base, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
test_zero_mean_gain_has_no_normalized_output (void)
{
  g_autoptr(GBytes) calibration = build_zero_gain_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  guint16 image_base[GOODIX_CHICAGO_PIXELS] = { 0, };

  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  g_assert_nonnull (preprocessor);
  g_assert_false (goodix_chicago_preprocessor_get_normalized_gain (
                    preprocessor, 0, NULL));
}

static void
test_builds_source_plane (void)
{
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  guint16 image_base[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 current[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 source[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 normalized_gain = 0;

  image_base[0] = 6000;
  current[0] = 1000;
  image_base[1] = 1000;
  current[1] = 2000;
  image_base[37] = 5000;
  current[37] = 1000;

  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_build_source_plane (
    preprocessor, current, image_base, source);

  g_assert_true (goodix_chicago_preprocessor_get_normalized_gain (
                   preprocessor, 0, &normalized_gain));
  g_assert_cmpuint (source[0], ==,
                    (5000u * 0x2000u + normalized_gain / 2) /
                    normalized_gain);
  g_assert_cmpuint (source[1], ==, 0);
  g_assert_true (goodix_chicago_preprocessor_get_normalized_gain (
                   preprocessor, 37, &normalized_gain));
  g_assert_cmpuint (source[37], ==,
                    (4000u * 0x2000u + normalized_gain / 2) /
                    normalized_gain);
}

static void
test_builds_mask (void)
{
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  guint16 image_base[GOODIX_CHICAGO_PIXELS];
  guint16 current[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint8 mask[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint16 threshold;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    image_base[pixel] = 300;
  image_base[1] = 50;
  current[2] = 0x0fff;
  image_base[2] = 0x0fff;

  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  threshold = goodix_chicago_preprocessor_build_mask (
    preprocessor, current, image_base, mask);

  g_assert_cmpuint (threshold, ==, 60);
  g_assert_cmpuint (mask[0], ==, 0xff);
  g_assert_cmpuint (mask[1], ==, 0);
  g_assert_cmpuint (mask[2], ==, 0);
}

static void
test_finalizes_metrics (void)
{
  guint8 quality = 0;
  guint8 coverage = 0;

  goodix_chicago_preprocessor_finalize_metrics (
    85, 100, &quality, &coverage);
  g_assert_cmpuint (quality, ==, 92);
  g_assert_cmpuint (coverage, ==, 100);

  goodix_chicago_preprocessor_finalize_metrics (
    85, 25, &quality, &coverage);
  g_assert_cmpuint (quality, ==, 23);
  g_assert_cmpuint (coverage, ==, 25);

  goodix_chicago_preprocessor_finalize_metrics (
    -1, 150, &quality, &coverage);
  g_assert_cmpuint (quality, ==, 0);
  g_assert_cmpuint (coverage, ==, 100);
}

static void
test_computes_uniform_base_quality (void)
{
  guint8 enhanced[GOODIX_CHICAGO_PIXELS] = { 0, };
  guint8 quality_mask[GOODIX_CHICAGO_PIXELS];
  const guint center = 20 * 80 + 20;

  memset (quality_mask, 0xff, sizeof (quality_mask));
  g_assert_cmpint (goodix_chicago_preprocessor_compute_base_quality (
                     enhanced, quality_mask), ==, 0);
  goodix_chicago_preprocessor_build_quality_mask (enhanced, quality_mask);
  g_assert_cmpuint (quality_mask[center], ==, 0xff);

  enhanced[center] = 0xff;
  enhanced[center - 1] = enhanced[center + 1] = 0xff;
  enhanced[center - 2] = enhanced[center + 2] = 0xff;
  enhanced[center - 80] = enhanced[center + 80] = 0xff;
  enhanced[center - 160] = enhanced[center + 160] = 0xff;
  goodix_chicago_preprocessor_build_quality_mask (enhanced, quality_mask);
  g_assert_cmpuint (quality_mask[center], ==, 0);
  g_assert_cmpuint (quality_mask[center - 1], ==, 0xff);
}

static void
test_computes_uniform_coverage (void)
{
  guint8 enhanced[GOODIX_CHICAGO_PIXELS] = { 0, };

  g_assert_cmpint (goodix_chicago_preprocessor_compute_coverage (enhanced),
                   ==, 0);
  memset (enhanced, 0xff, sizeof (enhanced));
  g_assert_cmpint (goodix_chicago_preprocessor_compute_coverage (enhanced),
                   ==, 0);
}

static void
test_does_not_reject_low_metrics_alone (void)
{
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 *image_base =
    g_new (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *raw = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint8 *enhanced = g_new0 (guint8, GOODIX_CHICAGO_PIXELS);
  guint8 quality;
  guint8 coverage;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    image_base[pixel] = raw[pixel] = 2048;
  preprocessor = goodix_chicago_preprocessor_new (
    calibration, image_base, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (
    goodix_chicago_preprocessor_build_enhanced_checked (
      preprocessor, raw, enhanced),
    ==, GOODIX_CHICAGO_PREPROCESS_STATUS_OK);
  goodix_chicago_preprocessor_finalize_metrics (
    goodix_chicago_preprocessor_compute_base_quality_from_enhanced (enhanced),
    goodix_chicago_preprocessor_compute_coverage (enhanced),
    &quality, &coverage);
  g_assert_cmpuint (quality, ==, 0);
  g_assert_cmpuint (coverage, ==, 0);
}

static void
test_rejects_saturated_raw_input (void)
{
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 *image_base =
    g_new (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *raw = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint8 *enhanced = g_new (guint8, GOODIX_CHICAGO_PIXELS);

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    image_base[pixel] = 2048;
  memset (enhanced, 0xa5, GOODIX_CHICAGO_PIXELS);
  preprocessor = goodix_chicago_preprocessor_new (
    calibration, image_base, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (
    goodix_chicago_preprocessor_build_enhanced_checked (
      preprocessor, raw, enhanced),
    ==, GOODIX_CHICAGO_PREPROCESS_STATUS_BAD_INPUT);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    g_assert_cmpuint (enhanced[pixel], ==, 0);
}

static void
test_matches_oracle_coverage (void)
{
  const gchar *enhanced_path = g_getenv ("CHICAGO_COVERAGE_ENHANCED");
  const gchar *metadata_path = g_getenv ("CHICAGO_COVERAGE_METADATA");
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *metadata = NULL;
  g_autoptr(GError) error = NULL;
  gsize enhanced_size;
  gsize metadata_size;

  if (!enhanced_path || !metadata_path)
    {
      g_test_skip ("Chicago Wine coverage vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (metadata_path, &metadata,
                                      &metadata_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_PIXELS);
  g_assert_cmpuint (metadata_size, >=, 0x2a);
  g_assert_cmpint (goodix_chicago_preprocessor_compute_coverage (
                     (const guint8 *) enhanced),
                   ==, (guint8) metadata[0x29]);
}

static void
test_matches_oracle_base_quality (void)
{
  const gchar *enhanced_path = g_getenv ("CHICAGO_QUALITY_ENHANCED");
  const gchar *mask_path = g_getenv ("CHICAGO_QUALITY_MASK");
  const gchar *base_quality_path = g_getenv ("CHICAGO_BASE_QUALITY");
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *quality_mask = NULL;
  g_autofree gchar *base_quality_bytes = NULL;
  g_autoptr(GError) error = NULL;
  gsize enhanced_size;
  gsize mask_size;
  gsize base_quality_size;
  gint32 expected;
  gint actual;
  guint8 actual_mask[GOODIX_CHICAGO_PIXELS];

  if (!enhanced_path || !mask_path || !base_quality_path)
    {
      g_test_skip ("Chicago Wine base-quality vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (mask_path, &quality_mask,
                                      &mask_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (base_quality_path, &base_quality_bytes,
                                      &base_quality_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_PIXELS);
  g_assert_cmpuint (mask_size, ==, GOODIX_CHICAGO_PIXELS);
  g_assert_cmpuint (base_quality_size, ==, sizeof (expected));

  memcpy (&expected, base_quality_bytes, sizeof (expected));
  expected = GINT32_FROM_LE (expected);
  goodix_chicago_preprocessor_build_quality_mask (
    (const guint8 *) enhanced, actual_mask);
  g_assert_cmpmem (actual_mask, sizeof (actual_mask), quality_mask, mask_size);
  actual = goodix_chicago_preprocessor_compute_base_quality (
    (const guint8 *) enhanced, (const guint8 *) quality_mask);
  g_assert_cmpint (actual, ==, expected);
  actual = goodix_chicago_preprocessor_compute_base_quality_from_enhanced (
    (const guint8 *) enhanced);
  g_assert_cmpint (actual, ==, expected);
}

static void
test_matches_oracle_metrics (void)
{
  const gchar *base_quality_path = g_getenv ("CHICAGO_BASE_QUALITY");
  const gchar *metadata_path = g_getenv ("CHICAGO_METRICS_METADATA");
  g_autofree gchar *base_quality_bytes = NULL;
  g_autofree gchar *metadata = NULL;
  g_autoptr(GError) error = NULL;
  gsize base_quality_size;
  gsize metadata_size;
  gint32 base_quality;
  guint8 quality;
  guint8 coverage;

  if (!base_quality_path || !metadata_path)
    {
      g_test_skip ("Chicago Wine metric vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (base_quality_path, &base_quality_bytes,
                                      &base_quality_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (metadata_path, &metadata,
                                      &metadata_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (base_quality_size, ==, sizeof (base_quality));
  g_assert_cmpuint (metadata_size, >=, 0x2a);

  memcpy (&base_quality, base_quality_bytes, sizeof (base_quality));
  base_quality = GINT32_FROM_LE (base_quality);
  goodix_chicago_preprocessor_finalize_metrics (
    base_quality, (guint8) metadata[0x29], &quality, &coverage);
  g_assert_cmpuint (quality, ==, (guint8) metadata[0x28]);
  g_assert_cmpuint (coverage, ==, (guint8) metadata[0x29]);
}

static void
test_refines_diagonal_envelopes (void)
{
  const guint width = 80;
  const guint height = 64;
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 *image_base = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *local_max = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *local_min = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *refined_min = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *refined_max = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  const guint center = 10 * width + 10;
  const guint bottom_right = width * height - 1;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    local_max[pixel] = 1000;

  /* Top-left sees only itself and its down-right diagonal. Axial values must
   * not participate in this vendor kernel. */
  local_max[0] = 500;
  local_max[width + 1] = 400;
  local_max[1] = 1;
  local_max[width] = 1;
  local_min[0] = 500;
  local_min[width + 1] = 600;
  local_min[1] = G_MAXUINT16;
  local_min[width] = G_MAXUINT16;

  local_max[center] = 500;
  local_max[center - width - 1] = 400;
  local_max[center - width + 1] = 300;
  local_max[center + width - 1] = 200;
  local_max[center + width + 1] = 100;
  local_max[center - 1] = 1;
  local_max[center + 1] = 1;
  local_min[center] = 500;
  local_min[center - width - 1] = 600;
  local_min[center - width + 1] = 700;
  local_min[center + width - 1] = 800;
  local_min[center + width + 1] = 900;
  local_min[center - 1] = G_MAXUINT16;
  local_min[center + 1] = G_MAXUINT16;

  local_max[bottom_right] = 500;
  local_max[bottom_right - width - 1] = 450;
  local_min[bottom_right] = 500;
  local_min[bottom_right - width - 1] = 550;

  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_refine_envelopes (
    preprocessor, local_max, local_min, refined_min, refined_max);

  g_assert_cmpuint (refined_min[0], ==, 400);
  g_assert_cmpuint (refined_max[0], ==, 600);
  g_assert_cmpuint (refined_min[center], ==, 100);
  g_assert_cmpuint (refined_max[center], ==, 900);
  g_assert_cmpuint (refined_min[bottom_right], ==, 450);
  g_assert_cmpuint (refined_max[bottom_right], ==, 550);
}

static void
test_builds_cross_local_envelopes (void)
{
  const guint width = 80;
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 *image_base = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *source = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *local_max = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *local_min = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  const guint center = 20 * width + 20;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    source[pixel] = 500;

  source[center + 5] = 900;
  source[center + 5 * width] = 100;
  source[center + width + 1] = 1000;
  source[center - width - 1] = 0;

  source[5] = 800;
  source[5 * width] = 200;
  source[width + 1] = 1000;
  source[2 * width + 2] = 0;

  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_build_local_envelopes (
    preprocessor, source, local_max, local_min);

  g_assert_cmpuint (local_max[center], ==, 900);
  g_assert_cmpuint (local_min[center], ==, 100);
  g_assert_cmpuint (local_max[0], ==, 800);
  g_assert_cmpuint (local_min[0], ==, 200);
}

static void
test_centers_masked_local_mean (void)
{
  const guint width = 80;
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 *image_base = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *source = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint8 *mask = g_new (guint8, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *centered = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  const guint center = 20 * width + 20;
  const guint far_pixel = 40 * width + 40;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    {
      source[pixel] = 1000;
      mask[pixel] = 1;
    }
  source[center] = 2000;
  mask[far_pixel] = 0;

  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_center_local_mean (
    preprocessor, source, mask, centered);

  /* (120 * 1000 + 2000 + 60) / 121 rounds to 1008. */
  g_assert_cmpuint (centered[center], ==, 3992);
  g_assert_cmpuint (centered[far_pixel], ==, 3000);
  g_assert_cmpuint (centered[0], ==, 3000);
}

static void
test_smooths_centered_interior (void)
{
  const guint width = 80;
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 *image_base = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *centered = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  const guint pixel = 20 * width + 20;

  centered[0] = 1234;
  centered[pixel] = 160;
  centered[pixel - width] = 80;
  centered[pixel + width] = 80;
  centered[pixel - 1] = 80;
  centered[pixel + 1] = 80;
  centered[pixel - width - 1] = 16;
  centered[pixel - width + 1] = 16;
  centered[pixel + width - 1] = 16;
  centered[pixel + width + 1] = 16;

  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_smooth_centered (preprocessor, centered);

  g_assert_cmpuint (centered[pixel], ==, 84);
  g_assert_cmpuint (centered[0], ==, 1234);
}

static void
test_builds_uniform_candidate (void)
{
  g_autoptr(GBytes) calibration = build_calibration ();
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 *image_base = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint16 *source = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  g_autofree guint8 *mask = g_new (guint8, GOODIX_CHICAGO_PIXELS);
  g_autofree guint8 *candidate = g_new0 (guint8, GOODIX_CHICAGO_PIXELS);
  const guint masked_pixel = 123;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    {
      source[pixel] = 1000;
      mask[pixel] = 1;
    }
  mask[masked_pixel] = 0;

  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_build_candidate (
    preprocessor, source, mask, candidate);

  g_assert_cmpuint (candidate[0], ==, 0);
  g_assert_cmpuint (candidate[masked_pixel], ==, 0xff);
}

static void
test_selects_mode24_alternate_candidate (void)
{
  /* Two observed generated-calibration frames on the production sensor. */
  g_assert_true (goodix_chicago_preprocessor_select_alternate_mode24 (
    1041, 354, 233, 600));
  g_assert_false (goodix_chicago_preprocessor_select_alternate_mode24 (
    641, 280, 249, 600));

  /* Low branch uses strict score and correlation limits. */
  g_assert_false (goodix_chicago_preprocessor_select_alternate_mode24 (
    1041, 480, 233, 600));
  g_assert_false (goodix_chicago_preprocessor_select_alternate_mode24 (
    1041, 354, 235, 600));

  /* Mid branch: the direct limit is 564; the relaxed limit also requires
   * correlation below 220. */
  g_assert_true (goodix_chicago_preprocessor_select_alternate_mode24 (
    3000, 563, 255, 600));
  g_assert_true (goodix_chicago_preprocessor_select_alternate_mode24 (
    3000, 564, 219, 600));
  g_assert_false (goodix_chicago_preprocessor_select_alternate_mode24 (
    3000, 564, 220, 600));

  /* High branch has the corresponding strict 200 correlation boundary;
   * primary scores at 7500 or above always promote the alternate. */
  g_assert_true (goodix_chicago_preprocessor_select_alternate_mode24 (
    6000, 900, 255, 600));
  g_assert_true (goodix_chicago_preprocessor_select_alternate_mode24 (
    6000, 901, 199, 600));
  g_assert_false (goodix_chicago_preprocessor_select_alternate_mode24 (
    6000, 901, 200, 600));
  g_assert_true (goodix_chicago_preprocessor_select_alternate_mode24 (
    7500, G_MAXINT, G_MAXINT, 600));
}

static void
test_matches_oracle_candidate (void)
{
  const gchar *source_path = g_getenv ("CHICAGO_STAGE_SOURCE");
  const gchar *mask_path = g_getenv ("CHICAGO_STAGE_MASK");
  const gchar *candidate_path = g_getenv ("CHICAGO_STAGE_CANDIDATE");
  const gchar *actual_path = g_getenv ("CHICAGO_STAGE_ACTUAL");
  g_autofree gchar *source_bytes = NULL;
  g_autofree gchar *mask = NULL;
  g_autofree gchar *expected = NULL;
  g_autofree guint16 *source = NULL;
  g_autofree guint8 *candidate = NULL;
  g_autoptr(GBytes) calibration = NULL;
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  gsize source_size;
  gsize mask_size;
  gsize expected_size;
  guint16 image_base[GOODIX_CHICAGO_PIXELS] = { 0, };

  if (!source_path || !mask_path || !candidate_path)
    {
      g_test_skip ("Chicago Wine stage vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (source_path, &source_bytes, &source_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (mask_path, &mask, &mask_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (candidate_path, &expected, &expected_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (source_size, ==,
                    GOODIX_CHICAGO_PIXELS * sizeof (guint16));
  g_assert_cmpuint (mask_size, ==, GOODIX_CHICAGO_PIXELS);
  g_assert_cmpuint (expected_size, ==, GOODIX_CHICAGO_PIXELS);

  source = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  candidate = g_new0 (guint8, GOODIX_CHICAGO_PIXELS);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    {
      guint16 value;

      memcpy (&value, source_bytes + pixel * sizeof (value), sizeof (value));
      source[pixel] = GUINT16_FROM_LE (value);
    }

  calibration = build_calibration ();
  preprocessor = goodix_chicago_preprocessor_new (calibration, image_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_build_candidate (
    preprocessor, source, (const guint8 *) mask, candidate);
  if (actual_path)
    {
      g_assert_true (g_file_set_contents (actual_path, (const gchar *) candidate,
                                          GOODIX_CHICAGO_PIXELS, &error));
      g_assert_no_error (error);
    }
  g_assert_cmpmem (candidate, GOODIX_CHICAGO_PIXELS,
                   expected, expected_size);
}

static void
test_matches_oracle_source_plane (void)
{
  const gchar *current_path = g_getenv ("CHICAGO_SOURCE_CURRENT");
  const gchar *base_path = g_getenv ("CHICAGO_SOURCE_BASE");
  const gchar *expected_path = g_getenv ("CHICAGO_SOURCE_EXPECTED");
  const gchar *calibration_path = g_getenv ("CHICAGO_CALIBRATION_FILE");
  const gchar *mask_path = g_getenv ("CHICAGO_SOURCE_MASK");
  const gchar *enhanced_path = g_getenv ("CHICAGO_ENHANCED_EXPECTED");
  const gchar *raw_current_path = g_getenv ("CHICAGO_RAW_CURRENT");
  const gchar *raw_base_path = g_getenv ("CHICAGO_RAW_BASE");
  g_autofree gchar *current_bytes = NULL;
  g_autofree gchar *base_bytes = NULL;
  g_autofree gchar *expected_bytes = NULL;
  g_autofree gchar *calibration_file = NULL;
  g_autofree gchar *expected_mask = NULL;
  g_autofree gchar *expected_enhanced = NULL;
  g_autofree gchar *raw_current_bytes = NULL;
  g_autofree gchar *raw_base_bytes = NULL;
  g_autofree guint16 *current = NULL;
  g_autofree guint16 *image_base = NULL;
  g_autofree guint16 *source = NULL;
  g_autofree guint8 *mask = NULL;
  g_autofree guint8 *enhanced = NULL;
  g_autofree guint16 *raw_current = NULL;
  g_autofree guint16 *raw_base = NULL;
  g_autofree guint16 *prepared = NULL;
  g_autoptr(GBytes) calibration = NULL;
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GoodixChicagoPreprocessor) raw_preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  gsize current_size;
  gsize base_size;
  gsize expected_size;
  gsize calibration_size;
  gsize mask_size;
  gsize enhanced_size;
  gsize raw_current_size;
  gsize raw_base_size;

  if (!current_path || !base_path || !expected_path || !calibration_path ||
      !mask_path || !enhanced_path || !raw_current_path || !raw_base_path)
    {
      g_test_skip ("Chicago Wine source-plane vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (current_path, &current_bytes,
                                      &current_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (base_path, &base_bytes, &base_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (expected_path, &expected_bytes,
                                      &expected_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (calibration_path, &calibration_file,
                                      &calibration_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (mask_path, &expected_mask,
                                      &mask_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (enhanced_path, &expected_enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (raw_current_path, &raw_current_bytes,
                                      &raw_current_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (raw_base_path, &raw_base_bytes,
                                      &raw_base_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (current_size, ==,
                    GOODIX_CHICAGO_PIXELS * sizeof (guint16));
  g_assert_cmpuint (base_size, ==,
                    GOODIX_CHICAGO_PIXELS * sizeof (guint16));
  g_assert_cmpuint (expected_size, ==,
                    GOODIX_CHICAGO_PIXELS * sizeof (guint16));
  g_assert_cmpuint (calibration_size, ==,
                    GOODIX_CHICAGO_CALIBRATION_FILE_LEN);
  g_assert_cmpuint (mask_size, ==, GOODIX_CHICAGO_PIXELS);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_PIXELS);
  g_assert_cmpuint (raw_current_size, ==,
                    GOODIX_CHICAGO_PIXELS * sizeof (guint16));
  g_assert_cmpuint (raw_base_size, ==,
                    GOODIX_CHICAGO_PIXELS * sizeof (guint16));

  current = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  image_base = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  source = g_new0 (guint16, GOODIX_CHICAGO_PIXELS);
  mask = g_new0 (guint8, GOODIX_CHICAGO_PIXELS);
  enhanced = g_new0 (guint8, GOODIX_CHICAGO_PIXELS);
  raw_current = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  raw_base = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  prepared = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    {
      guint16 value;

      memcpy (&value, current_bytes + pixel * sizeof (value), sizeof (value));
      current[pixel] = GUINT16_FROM_LE (value);
      memcpy (&value, base_bytes + pixel * sizeof (value), sizeof (value));
      image_base[pixel] = GUINT16_FROM_LE (value);
      memcpy (&value, raw_current_bytes + pixel * sizeof (value), sizeof (value));
      raw_current[pixel] = GUINT16_FROM_LE (value);
      memcpy (&value, raw_base_bytes + pixel * sizeof (value), sizeof (value));
      raw_base[pixel] = GUINT16_FROM_LE (value);
    }

  calibration = g_bytes_new (
    calibration_file + GOODIX_CHICAGO_SENSOR_ID_LEN,
    GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);
  preprocessor = goodix_chicago_preprocessor_new (calibration, raw_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_prepare_raw (
    preprocessor, raw_current, prepared);
  g_assert_cmpmem (prepared,
                   GOODIX_CHICAGO_PIXELS * sizeof (guint16),
                   current, current_size);
  goodix_chicago_preprocessor_prepare_raw (
    preprocessor, raw_base, prepared);
  g_assert_cmpmem (prepared,
                   GOODIX_CHICAGO_PIXELS * sizeof (guint16),
                   image_base, base_size);
  goodix_chicago_preprocessor_build_source_plane (
    preprocessor, current, image_base, source);
  g_assert_cmpmem (source,
                   GOODIX_CHICAGO_PIXELS * sizeof (guint16),
                   expected_bytes, expected_size);
  goodix_chicago_preprocessor_build_mask (
    preprocessor, current, image_base, mask);
  g_assert_cmpmem (mask, GOODIX_CHICAGO_PIXELS,
                   expected_mask, mask_size);
  goodix_chicago_preprocessor_build_enhanced_prepared (
    preprocessor, current, image_base, enhanced);
  g_assert_cmpmem (enhanced, GOODIX_CHICAGO_PIXELS,
                   expected_enhanced, enhanced_size);
  memset (enhanced, 0, GOODIX_CHICAGO_PIXELS);
  /* Both entry points are compared with the oracle's first call. Temporal
   * calibration evolves after a capture, so exercise the raw entry point on
   * a fresh instance rather than treating the preprocessor as stateless. */
  raw_preprocessor = goodix_chicago_preprocessor_new (
    calibration, raw_base, &error);
  g_assert_no_error (error);
  goodix_chicago_preprocessor_build_enhanced (
    raw_preprocessor, raw_current, enhanced);
  g_assert_cmpmem (enhanced, GOODIX_CHICAGO_PIXELS,
                   expected_enhanced, enhanced_size);
}

static void
test_matches_oracle_capture_status (void)
{
  const gchar *calibration_path = g_getenv ("CHICAGO_CALIBRATION_FILE");
  const gchar *raw_current_path = g_getenv ("CHICAGO_RAW_CURRENT");
  const gchar *raw_base_path = g_getenv ("CHICAGO_RAW_BASE");
  const gchar *status_string = g_getenv ("CHICAGO_STATUS_EXPECTED");
  const gchar *enhanced_path = g_getenv ("CHICAGO_ENHANCED_EXPECTED");
  g_autofree gchar *calibration_file = NULL;
  g_autofree gchar *raw_current_bytes = NULL;
  g_autofree gchar *raw_base_bytes = NULL;
  g_autofree gchar *expected_enhanced = NULL;
  g_autofree guint16 *raw_current = NULL;
  g_autofree guint16 *raw_base = NULL;
  g_autofree guint8 *enhanced = NULL;
  g_autoptr(GBytes) calibration = NULL;
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GError) error = NULL;
  GoodixChicagoPreprocessStatus status;
  gsize calibration_size;
  gsize raw_current_size;
  gsize raw_base_size;
  gsize enhanced_size = 0;
  guint expected_status;

  if (!calibration_path || !raw_current_path || !raw_base_path ||
      !status_string)
    {
      g_test_skip ("Chicago Wine capture-status vector was not requested");
      return;
    }

  g_assert_true (g_file_get_contents (calibration_path, &calibration_file,
                                      &calibration_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (raw_current_path, &raw_current_bytes,
                                      &raw_current_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (raw_base_path, &raw_base_bytes,
                                      &raw_base_size, &error));
  g_assert_no_error (error);
  if (enhanced_path)
    {
      g_assert_true (g_file_get_contents (enhanced_path, &expected_enhanced,
                                          &enhanced_size, &error));
      g_assert_no_error (error);
    }
  g_assert_cmpuint (calibration_size, ==,
                    GOODIX_CHICAGO_CALIBRATION_FILE_LEN);
  g_assert_cmpuint (raw_current_size, ==,
                    GOODIX_CHICAGO_PIXELS * sizeof (guint16));
  g_assert_cmpuint (raw_base_size, ==,
                    GOODIX_CHICAGO_PIXELS * sizeof (guint16));
  if (enhanced_path)
    g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_PIXELS);

  raw_current = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  raw_base = g_new (guint16, GOODIX_CHICAGO_PIXELS);
  enhanced = g_new0 (guint8, GOODIX_CHICAGO_PIXELS);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    {
      guint16 value;

      memcpy (&value, raw_current_bytes + pixel * sizeof (value),
              sizeof (value));
      raw_current[pixel] = GUINT16_FROM_LE (value);
      memcpy (&value, raw_base_bytes + pixel * sizeof (value), sizeof (value));
      raw_base[pixel] = GUINT16_FROM_LE (value);
    }

  calibration = g_bytes_new (
    calibration_file + GOODIX_CHICAGO_SENSOR_ID_LEN,
    GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);
  preprocessor = goodix_chicago_preprocessor_new (
    calibration, raw_base, &error);
  g_assert_no_error (error);
  status = goodix_chicago_preprocessor_build_enhanced_checked (
    preprocessor, raw_current, enhanced);
  expected_status = g_ascii_strtoull (status_string, NULL, 0);
  g_assert_cmpuint (status, ==, expected_status);
  if (enhanced_path)
    g_assert_cmpmem (enhanced, GOODIX_CHICAGO_PIXELS,
                     expected_enhanced, enhanced_size);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gdix51c0/chicago-preprocess/input-state", test_input_state);
  g_test_add_func ("/gdix51c0/chicago-preprocess/classifies-resolution-labels",
                   test_classifies_resolution_labels);
  g_test_add_func ("/gdix51c0/chicago-preprocess/builds-resolution-labels",
                   test_builds_resolution_labels);
  g_test_add_func ("/gdix51c0/chicago-preprocess/builds-resolution-input-mask",
                   test_builds_resolution_input_mask);
  g_test_add_func ("/gdix51c0/chicago-preprocess/builds-resolution-base-plane",
                   test_builds_resolution_base_plane);
  g_test_add_func (
    "/gdix51c0/chicago-preprocess/builds-resolution-secondary-plane",
    test_builds_resolution_secondary_plane);
  g_test_add_func (
    "/gdix51c0/chicago-preprocess/builds-resolution-gradients",
    test_builds_resolution_gradients);
  g_test_add_func (
    "/gdix51c0/chicago-preprocess/builds-resolution-primary-plane",
    test_builds_resolution_primary_plane);
  g_test_add_func (
    "/gdix51c0/chicago-preprocess/calculates-resolution-histogram-statistics",
    test_calculates_resolution_histogram_statistics);
  g_test_add_func (
    "/gdix51c0/chicago-preprocess/calculates-resolution-secondary-analysis",
    test_calculates_resolution_secondary_analysis);
  g_test_add_func (
    "/gdix51c0/chicago-preprocess/calculates-resolution-thresholds",
    test_calculates_resolution_thresholds);
  g_test_add_func (
    "/gdix51c0/chicago-preprocess/builds-exceptional-resolution-labels",
    test_builds_exceptional_resolution_labels);
  g_test_add_func ("/gdix51c0/chicago-preprocess/packs-resolution-codes",
                   test_packs_resolution_codes);
  g_test_add_func ("/gdix51c0/chicago-preprocess/rejects-bad-payload",
                   test_rejects_bad_payload);
  g_test_add_func ("/gdix51c0/chicago-preprocess/zero-mean-gain",
                   test_zero_mean_gain_has_no_normalized_output);
  g_test_add_func ("/gdix51c0/chicago-preprocess/builds-source-plane",
                   test_builds_source_plane);
  g_test_add_func ("/gdix51c0/chicago-preprocess/builds-mask",
                   test_builds_mask);
  g_test_add_func ("/gdix51c0/chicago-preprocess/finalizes-metrics",
                   test_finalizes_metrics);
  g_test_add_func ("/gdix51c0/chicago-preprocess/uniform-base-quality",
                   test_computes_uniform_base_quality);
  g_test_add_func ("/gdix51c0/chicago-preprocess/uniform-coverage",
                   test_computes_uniform_coverage);
  g_test_add_func ("/gdix51c0/chicago-preprocess/low-metrics-are-not-rejection",
                   test_does_not_reject_low_metrics_alone);
  g_test_add_func ("/gdix51c0/chicago-preprocess/rejects-saturated-raw-input",
                   test_rejects_saturated_raw_input);
  g_test_add_func ("/gdix51c0/chicago-preprocess/matches-oracle-coverage",
                   test_matches_oracle_coverage);
  g_test_add_func ("/gdix51c0/chicago-preprocess/matches-oracle-base-quality",
                   test_matches_oracle_base_quality);
  g_test_add_func ("/gdix51c0/chicago-preprocess/matches-oracle-metrics",
                   test_matches_oracle_metrics);
  g_test_add_func ("/gdix51c0/chicago-preprocess/refines-diagonal-envelopes",
                   test_refines_diagonal_envelopes);
  g_test_add_func ("/gdix51c0/chicago-preprocess/builds-cross-local-envelopes",
                   test_builds_cross_local_envelopes);
  g_test_add_func ("/gdix51c0/chicago-preprocess/centers-masked-local-mean",
                   test_centers_masked_local_mean);
  g_test_add_func ("/gdix51c0/chicago-preprocess/smooths-centered-interior",
                   test_smooths_centered_interior);
  g_test_add_func ("/gdix51c0/chicago-preprocess/builds-uniform-candidate",
                   test_builds_uniform_candidate);
  g_test_add_func (
    "/gdix51c0/chicago-preprocess/selects-mode24-alternate-candidate",
    test_selects_mode24_alternate_candidate);
  g_test_add_func ("/gdix51c0/chicago-preprocess/matches-oracle-candidate",
                   test_matches_oracle_candidate);
  g_test_add_func ("/gdix51c0/chicago-preprocess/matches-oracle-source-plane",
                   test_matches_oracle_source_plane);
  g_test_add_func ("/gdix51c0/chicago-preprocess/matches-oracle-capture-status",
                   test_matches_oracle_capture_status);
  return g_test_run ();
}
