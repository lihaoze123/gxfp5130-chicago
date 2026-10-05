// SPDX-License-Identifier: LGPL-2.1-or-later
/* Copyright (C) 2026 Berke Kabagöz <berkekbgz@gmail.com> */

/* Structural tests for the observed GDIX51C0 Chicago system template. */

#include <glib.h>
#include <gio/gio.h>

#include "goodix-chicago-template.h"

static void
write_le32 (guint8  *p,
            guint32  value)
{
  p[0] = (guint8) value;
  p[1] = (guint8) (value >> 8);
  p[2] = (guint8) (value >> 16);
  p[3] = (guint8) (value >> 24);
}

static void
test_parse_valid_v1 (void)
{
  const guint8 inner_data[] = { 0x87, 0xde, 0xad, 0xbe, 0xef, 0x86 };
  guint8 random[GOODIX_CHICAGO_SYSTEM_TEMPLATE_V1_RANDOM_LEN];
  GoodixChicagoTemplateView view;
  g_autoptr(GBytes) inner = NULL;
  g_autoptr(GBytes) wrapped = NULL;
  g_autoptr(GError) error = NULL;
  const guint8 *data;
  gsize data_len;

  for (guint index = 0; index < sizeof (random); index++)
    random[index] = index * 7 + 1;
  inner = g_bytes_new_static (inner_data, sizeof (inner_data));
  wrapped = goodix_chicago_template_wrap_v1 (inner, random);
  g_assert_nonnull (wrapped);
  data = g_bytes_get_data (wrapped, &data_len);

  g_assert_true (goodix_chicago_template_parse (data, data_len,
                                                   &view, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (view.type, ==, GOODIX_CHICAGO_SYSTEM_TEMPLATE_TYPE_V1);
  g_assert_cmpuint (view.declared_payload_len, ==,
                    sizeof (inner_data) +
                    GOODIX_CHICAGO_SYSTEM_TEMPLATE_V1_TRAILER_LEN);
  g_assert_true (view.chicago_payload ==
                 data + GOODIX_CHICAGO_SYSTEM_TEMPLATE_PREFIX_LEN);
  g_assert_cmpuint (view.chicago_payload_len, ==, sizeof (inner_data));
  g_assert_cmpmem (view.chicago_payload, view.chicago_payload_len,
                   inner_data, sizeof (inner_data));
  g_assert_cmpmem (view.random,
                   GOODIX_CHICAGO_SYSTEM_TEMPLATE_V1_RANDOM_LEN,
                   random, sizeof (random));
}

static void
test_parse_valid_v2 (void)
{
  guint8 data[GOODIX_CHICAGO_SYSTEM_TEMPLATE_V2_HEADER_LEN] = { 0 };
  GoodixChicagoTemplateView view;
  g_autoptr(GError) error = NULL;

  write_le32 (data, GOODIX_CHICAGO_SYSTEM_TEMPLATE_TYPE_V2);
  write_le32 (data + 4, sizeof (data) - 8);

  g_assert_true (goodix_chicago_template_parse (data, sizeof (data),
                                                   &view, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (view.type, ==, GOODIX_CHICAGO_SYSTEM_TEMPLATE_TYPE_V2);
  g_assert_cmpuint (view.chicago_payload_len, ==, 0);
}

static void
test_parse_rejects_bad_envelope (void)
{
  guint8 data[GOODIX_CHICAGO_SYSTEM_TEMPLATE_V2_HEADER_LEN] = { 0 };
  GoodixChicagoTemplateView view;
  g_autoptr(GError) error = NULL;

  g_assert_false (goodix_chicago_template_parse (
                    data, GOODIX_CHICAGO_SYSTEM_TEMPLATE_PREFIX_LEN - 1,
                                                    &view, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  write_le32 (data, 0x12345678);
  write_le32 (data + 4, sizeof (data) - 8);
  g_assert_false (goodix_chicago_template_parse (data, sizeof (data),
                                                    &view, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_clear_error (&error);

  write_le32 (data, GOODIX_CHICAGO_SYSTEM_TEMPLATE_TYPE_V1);
  write_le32 (data + 4, 0);
  g_assert_false (goodix_chicago_template_parse (data, sizeof (data),
                                                    &view, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
test_parse_rejects_bad_v1_crc (void)
{
  const guint8 inner_data[] = { 0x87, 1, 2, 3, 4, 0x86 };
  guint8 random[GOODIX_CHICAGO_SYSTEM_TEMPLATE_V1_RANDOM_LEN] = { 0 };
  GoodixChicagoTemplateView view;
  g_autoptr(GBytes) inner = g_bytes_new_static (inner_data,
                                                sizeof (inner_data));
  g_autoptr(GBytes) wrapped = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint8 *corrupt = NULL;
  const guint8 *data;
  gsize data_len;

  wrapped = goodix_chicago_template_wrap_v1 (inner, random);
  data = g_bytes_get_data (wrapped, &data_len);
  corrupt = g_memdup2 (data, data_len);
  corrupt[GOODIX_CHICAGO_SYSTEM_TEMPLATE_PREFIX_LEN + 2] ^= 1;
  g_assert_false (goodix_chicago_template_parse (corrupt, data_len,
                                                    &view, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
test_official_v1_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_SYSTEM_TEMPLATE");
  GoodixChicagoTemplateView view;
  g_autofree gchar *contents = NULL;
  g_autoptr(GBytes) inner = NULL;
  g_autoptr(GBytes) rebuilt = NULL;
  g_autoptr(GError) error = NULL;
  const guint8 *rebuilt_data;
  gsize size;
  gsize rebuilt_size;

  if (!path)
    {
      g_test_skip ("EngineAdapter V1 system-template oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_true (goodix_chicago_template_parse (
    (const guint8 *) contents, size, &view, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (view.type, ==, GOODIX_CHICAGO_SYSTEM_TEMPLATE_TYPE_V1);
  inner = g_bytes_new (view.chicago_payload, view.chicago_payload_len);
  rebuilt = goodix_chicago_template_wrap_v1 (inner, view.random);
  rebuilt_data = g_bytes_get_data (rebuilt, &rebuilt_size);
  g_assert_cmpmem (rebuilt_data, rebuilt_size, contents, size);
}

static void
test_native_print_data (void)
{
  guint8 sensor_id[GOODIX_CHICAGO_SENSOR_ID_LEN];
  guint8 other_sensor[GOODIX_CHICAGO_SENSOR_ID_LEN];
  guint8 calibration_data[64];
  guint8 other_calibration_data[64];
  const guint8 packed_data[] = { 0x87, 1, 2, 3, 4, 0x86 };
  g_autoptr(GBytes) calibration = NULL;
  g_autoptr(GBytes) other_calibration = NULL;
  g_autoptr(GBytes) packed = NULL;
  g_autoptr(GBytes) parsed = NULL;
  g_autoptr(GVariant) data = NULL;
  g_autoptr(GVariant) legacy = NULL;
  g_autoptr(GError) error = NULL;
  const guint8 *parsed_data;
  gsize parsed_size;

  for (guint index = 0; index < sizeof (sensor_id); index++)
    sensor_id[index] = index * 7 + 3;
  memcpy (other_sensor, sensor_id, sizeof (sensor_id));
  other_sensor[5] ^= 1;
  for (guint index = 0; index < sizeof (calibration_data); index++)
    calibration_data[index] = index * 11 + 9;
  memcpy (other_calibration_data, calibration_data,
          sizeof (calibration_data));
  other_calibration_data[31] ^= 1;
  calibration = g_bytes_new_static (calibration_data,
                                    sizeof (calibration_data));
  other_calibration = g_bytes_new_static (other_calibration_data,
                                          sizeof (other_calibration_data));
  packed = g_bytes_new_static (packed_data, sizeof (packed_data));
  data = goodix_chicago_print_data_build (sensor_id, calibration, packed);
  g_assert_nonnull (data);
  g_assert_true (g_variant_is_of_type (
                   data, G_VARIANT_TYPE (GOODIX_CHICAGO_PRINT_DATA_TYPE)));
  g_assert_true (goodix_chicago_print_data_parse (
                   data, sensor_id, calibration, &parsed, &error));
  g_assert_no_error (error);
  parsed_data = g_bytes_get_data (parsed, &parsed_size);
  g_assert_cmpmem (parsed_data, parsed_size, packed_data, sizeof (packed_data));
  g_clear_pointer (&parsed, g_bytes_unref);

  g_assert_false (goodix_chicago_print_data_parse (
                    data, other_sensor, calibration, &parsed, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);
  /* NeedUpdateImageBase legitimately changes the calibration payload.  A
   * same-sensor enrolled print must remain usable across that transition. */
  g_assert_true (goodix_chicago_print_data_parse (
                   data, sensor_id, other_calibration, &parsed, &error));
  g_assert_no_error (error);
  g_clear_pointer (&parsed, g_bytes_unref);

  legacy = g_variant_ref_sink (g_variant_new_array (
    G_VARIANT_TYPE ("ay"), NULL, 0));
  g_assert_false (goodix_chicago_print_data_parse (
                    legacy, sensor_id, calibration, &parsed, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/gdix51c0/chicago-template/valid-v1", test_parse_valid_v1);
  g_test_add_func ("/gdix51c0/chicago-template/valid-v2", test_parse_valid_v2);
  g_test_add_func ("/gdix51c0/chicago-template/rejects-bad-envelope",
                   test_parse_rejects_bad_envelope);
  g_test_add_func ("/gdix51c0/chicago-template/rejects-bad-v1-crc",
                   test_parse_rejects_bad_v1_crc);
  g_test_add_func ("/gdix51c0/chicago-template/official-v1-oracle",
                   test_official_v1_oracle);
  g_test_add_func ("/gdix51c0/chicago-template/native-print-data",
                   test_native_print_data);

  return g_test_run ();
}
