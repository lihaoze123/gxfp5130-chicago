// SPDX-License-Identifier: LGPL-2.1-or-later
/* Copyright (C) 2026 Berke Kabagöz <berkekbgz@gmail.com> */

/* Tests for the native parser of Chicago's sensor-bound calibration file. */

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#include "goodix-chicago-calibration.h"

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
test_load_calibration (void)
{
  guint8 sensor_id[GOODIX_CHICAGO_SENSOR_ID_LEN];
  guint8 other_sensor_id[GOODIX_CHICAGO_SENSOR_ID_LEN];
  g_autofree guint8 *file_data = NULL;
  g_autofree gchar *path = NULL;
  g_autoptr(GBytes) payload = NULL;
  g_autoptr(GBytes) malformed_payload = NULL;
  g_autoptr(GError) error = NULL;
  const guint8 *payload_data;
  gsize payload_len = 0;
  guint16 gain = 0;
  guint16 offset = 0;
  int fd;

  for (gsize i = 0; i < G_N_ELEMENTS (sensor_id); i++)
    {
      sensor_id[i] = (guint8) (0x20 + i);
      other_sensor_id[i] = (guint8) (0x90 + i);
    }

  fd = g_file_open_tmp ("gdix51c0-calibration-XXXXXX", &path, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);
  close (fd);

  file_data = g_malloc (GOODIX_CHICAGO_CALIBRATION_FILE_LEN);
  memcpy (file_data, sensor_id, sizeof (sensor_id));
  for (gsize i = GOODIX_CHICAGO_SENSOR_ID_LEN;
       i < GOODIX_CHICAGO_CALIBRATION_FILE_LEN; i++)
    file_data[i] = (guint8) (i * 31 + 7);
  file_data[GOODIX_CHICAGO_SENSOR_ID_LEN +
            GOODIX_CHICAGO_GAIN_MAP_OFFSET] = 0x34;
  file_data[GOODIX_CHICAGO_SENSOR_ID_LEN +
            GOODIX_CHICAGO_GAIN_MAP_OFFSET + 1] = 0x12;
  file_data[GOODIX_CHICAGO_SENSOR_ID_LEN +
            GOODIX_CHICAGO_OFFSET_MAP_OFFSET] = 0xcd;
  file_data[GOODIX_CHICAGO_SENSOR_ID_LEN +
            GOODIX_CHICAGO_OFFSET_MAP_OFFSET + 1] = 0xab;
  write_le32 (file_data + GOODIX_CHICAGO_SENSOR_ID_LEN +
              GOODIX_CHICAGO_GAIN_MAP_CRC_OFFSET,
              calibration_map_crc32 (file_data + GOODIX_CHICAGO_SENSOR_ID_LEN +
                                     GOODIX_CHICAGO_GAIN_MAP_OFFSET,
                                     GOODIX_CHICAGO_CALIBRATION_MAP_BYTES));
  write_le32 (file_data + GOODIX_CHICAGO_SENSOR_ID_LEN +
              GOODIX_CHICAGO_OFFSET_MAP_CRC_OFFSET,
              calibration_map_crc32 (file_data + GOODIX_CHICAGO_SENSOR_ID_LEN +
                                     GOODIX_CHICAGO_OFFSET_MAP_OFFSET,
                                     GOODIX_CHICAGO_CALIBRATION_MAP_BYTES));
  g_assert_true (g_file_set_contents (path,
                                      (const gchar *) file_data,
                                      GOODIX_CHICAGO_CALIBRATION_FILE_LEN,
                                      &error));
  g_assert_no_error (error);

  payload = goodix_chicago_calibration_load (path, sensor_id, &error);
  g_assert_no_error (error);
  g_assert_nonnull (payload);
  payload_data = g_bytes_get_data (payload, &payload_len);
  g_assert_cmpuint (payload_len, ==, GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);
  g_assert_cmpmem (payload_data, payload_len,
                   file_data + GOODIX_CHICAGO_SENSOR_ID_LEN,
                   GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);
  g_assert_true (goodix_chicago_calibration_get_corrections (payload, 0,
                                                                &gain, &offset,
                                                                &error));
  g_assert_no_error (error);
  g_assert_cmpuint (gain, ==, 0x1234);
  g_assert_cmpuint (offset, ==, 0xabcd);
  g_assert_false (goodix_chicago_calibration_get_corrections (
                    payload, GOODIX_CHICAGO_PIXELS, NULL, NULL, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);

  malformed_payload = g_bytes_new_static ("bad", 3);
  g_assert_false (goodix_chicago_calibration_get_corrections (
                    malformed_payload, 0, NULL, NULL, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  g_clear_pointer (&payload, g_bytes_unref);
  file_data[GOODIX_CHICAGO_SENSOR_ID_LEN +
            GOODIX_CHICAGO_GAIN_MAP_OFFSET] ^= 0x01;
  g_assert_true (g_file_set_contents (path,
                                      (const gchar *) file_data,
                                      GOODIX_CHICAGO_CALIBRATION_FILE_LEN,
                                      &error));
  g_assert_no_error (error);
  g_assert_null (goodix_chicago_calibration_load (path, sensor_id, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  g_assert_null (goodix_chicago_calibration_load (path, other_sensor_id, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_clear_error (&error);

  g_assert_true (g_file_set_contents (path, "short", 5, &error));
  g_assert_no_error (error);
  g_assert_null (goodix_chicago_calibration_load (path, sensor_id, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_assert_cmpint (g_unlink (path), ==, 0);
}

static void
test_generate_calibration (void)
{
  guint8 sensor_id[GOODIX_CHICAGO_SENSOR_ID_LEN];
  guint16 image_base[GOODIX_CHICAGO_PIXELS];
  g_autofree gchar *path = NULL;
  g_autoptr(GBytes) generated = NULL;
  g_autoptr(GBytes) loaded = NULL;
  g_autoptr(GError) error = NULL;
  guint16 gain = 0;
  guint16 offset = 0;
  guint32 sample_count = G_MAXUINT32;
  int fd;

  for (guint i = 0; i < G_N_ELEMENTS (sensor_id); i++)
    sensor_id[i] = 0x40 + i;
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    image_base[pixel] = 1000 + pixel % 3000;
  image_base[0] = 5000; /* An untouched corner proves the 12-bit clamp. */
  image_base[GOODIX_CHICAGO_IMAGE_WIDTH + 1] = 2345;
  image_base[(GOODIX_CHICAGO_IMAGE_HEIGHT - 2) *
             GOODIX_CHICAGO_IMAGE_WIDTH + 78] = 3456;

  generated = goodix_chicago_calibration_generate (image_base);
  g_assert_nonnull (generated);
  g_assert_true (goodix_chicago_calibration_validate_payload (generated,
                                                               &error));
  g_assert_no_error (error);

  g_assert_true (goodix_chicago_calibration_get_corrections (
                   generated, 0, &gain, &offset, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (gain, ==, 0x2000);
  g_assert_cmpuint (offset, ==, 0x0fff);

  g_assert_true (goodix_chicago_calibration_get_corrections (
                   generated, 1, &gain, &offset, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (gain, ==, 0x2000);
  g_assert_cmpuint (offset, ==, 2345);

  g_assert_true (goodix_chicago_calibration_get_corrections (
                   generated,
                   (GOODIX_CHICAGO_IMAGE_HEIGHT - 1) *
                   GOODIX_CHICAGO_IMAGE_WIDTH + 78,
                   &gain, &offset, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (gain, ==, 0x2000);
  g_assert_cmpuint (offset, ==, 3456);
  g_assert_true (goodix_chicago_calibration_get_temporal_sample_count (
                   generated, &sample_count, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (sample_count, ==, 0);

  fd = g_file_open_tmp ("gdix51c0-generated-calibration-XXXXXX",
                        &path, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);
  close (fd);
  g_assert_true (goodix_chicago_calibration_save (
                   path, sensor_id, generated, &error));
  g_assert_no_error (error);
  loaded = goodix_chicago_calibration_load (path, sensor_id, &error);
  g_assert_no_error (error);
  g_assert_nonnull (loaded);
  g_assert_true (g_bytes_equal (generated, loaded));
  g_assert_cmpint (g_unlink (path), ==, 0);
}

static void
test_rebase_calibration (void)
{
  guint16 first_base[GOODIX_CHICAGO_PIXELS];
  guint16 next_base[GOODIX_CHICAGO_PIXELS];
  g_autoptr(GBytes) generated = NULL;
  g_autoptr(GBytes) mature = NULL;
  g_autoptr(GBytes) rebased = NULL;
  g_autoptr(GBytes) malformed = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint8 *mature_data = NULL;
  const guint8 *generated_data;
  const guint8 *rebased_data;
  gsize generated_len = 0;
  gsize rebased_len = 0;
  guint16 gain = 0;
  guint16 offset = 0;
  guint32 sample_count = 0;

  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    {
      first_base[pixel] = 2500 + pixel % 200;
      next_base[pixel] = 1800 + pixel % 500;
    }
  next_base[0] = 5000;
  next_base[GOODIX_CHICAGO_IMAGE_WIDTH + 1] = 2222;
  next_base[(GOODIX_CHICAGO_IMAGE_HEIGHT - 2) *
            GOODIX_CHICAGO_IMAGE_WIDTH + 78] = 3333;

  generated = goodix_chicago_calibration_generate (first_base);
  generated_data = g_bytes_get_data (generated, &generated_len);
  mature_data = g_memdup2 (generated_data, generated_len);
  write_le32 (mature_data + GOODIX_CHICAGO_TEMPORAL_SAMPLE_COUNT_OFFSET, 157);
  mature_data[0x20000] = 0x5a;
  mature = g_bytes_new_take (g_steal_pointer (&mature_data), generated_len);

  rebased = goodix_chicago_calibration_rebase (mature, next_base, &error);
  g_assert_no_error (error);
  g_assert_nonnull (rebased);
  g_assert_true (goodix_chicago_calibration_validate_payload (rebased, &error));
  g_assert_no_error (error);

  g_assert_true (goodix_chicago_calibration_get_corrections (
                   rebased, 0, &gain, &offset, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (gain, ==, 0x2000);
  g_assert_cmpuint (offset, ==, 0x0fff);
  g_assert_true (goodix_chicago_calibration_get_corrections (
                   rebased, 1, &gain, &offset, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (offset, ==, 2222);
  g_assert_true (goodix_chicago_calibration_get_corrections (
                   rebased,
                   (GOODIX_CHICAGO_IMAGE_HEIGHT - 1) *
                   GOODIX_CHICAGO_IMAGE_WIDTH + 78,
                   &gain, &offset, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (offset, ==, 3333);
  g_assert_true (goodix_chicago_calibration_get_temporal_sample_count (
                   rebased, &sample_count, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (sample_count, ==, 157);

  rebased_data = g_bytes_get_data (rebased, &rebased_len);
  g_assert_cmpuint (rebased_len, ==, generated_len);
  g_assert_cmpuint (rebased_data[0x20000], ==, 0x5a);

  /* Rebase must not mutate the caller's persistent payload. */
  g_assert_true (goodix_chicago_calibration_get_corrections (
                   mature, 0, NULL, &offset, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (offset, ==, first_base[0]);

  malformed = g_bytes_new_static ("bad", 3);
  g_assert_null (goodix_chicago_calibration_rebase (
                   malformed, next_base, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gdix51c0/chicago-calibration/load", test_load_calibration);
  g_test_add_func ("/gdix51c0/chicago-calibration/generate",
                   test_generate_calibration);
  g_test_add_func ("/gdix51c0/chicago-calibration/rebase",
                   test_rebase_calibration);
  return g_test_run ();
}
