// SPDX-License-Identifier: LGPL-2.1-or-later
/* Copyright (C) 2026 Berke Kabagöz <berkekbgz@gmail.com> */

/* Tests for the exact ANSI-381 BIR accepted by the Chicago engine oracle. */

#include <gio/gio.h>
#include <glib.h>

#include "goodix-chicago-bir.h"

static guint16
read_le16 (const guint8 *p)
{
  return (guint16) p[0] | ((guint16) p[1] << 8);
}

static guint32
read_le32 (const guint8 *p)
{
  return ((guint32) p[0]) |
         ((guint32) p[1] << 8) |
         ((guint32) p[2] << 16) |
         ((guint32) p[3] << 24);
}

static void
test_build_and_parse (void)
{
  guint16 raw[GOODIX_CHICAGO_PIXELS];
  GoodixChicagoBirView view;
  g_autoptr(GBytes) bir = NULL;
  g_autoptr(GError) error = NULL;
  gsize len = 0;
  const guint8 *data;

  for (gsize i = 0; i < G_N_ELEMENTS (raw); i++)
    raw[i] = (guint16) ((i * 17 + 0x123) & 0x0fff);

  bir = goodix_chicago_bir_build (raw, &error);
  g_assert_no_error (error);
  g_assert_nonnull (bir);
  data = g_bytes_get_data (bir, &len);

  g_assert_cmpuint (len, ==, GOODIX_CHICAGO_BIR_TOTAL_LEN);
  g_assert_cmpuint (read_le32 (data + 0x00), ==,
                    GOODIX_CHICAGO_BIR_BIOMETRIC_HDR_LEN);
  g_assert_cmpuint (read_le32 (data + 0x04), ==,
                    GOODIX_CHICAGO_BIR_HEADER_BLOCK_LEN);
  g_assert_cmpuint (read_le32 (data + 0x0c), ==,
                    GOODIX_CHICAGO_BIR_STANDARD_OFFSET);
  g_assert_cmpuint (read_le16 (data + GOODIX_CHICAGO_BIR_STANDARD_OFFSET + 0x2c),
                    ==, GOODIX_CHICAGO_IMAGE_WIDTH);
  g_assert_cmpuint (read_le16 (data + GOODIX_CHICAGO_BIR_STANDARD_OFFSET + 0x2e),
                    ==, GOODIX_CHICAGO_IMAGE_HEIGHT);
  g_assert_cmpuint (data[GOODIX_CHICAGO_BIR_STANDARD_OFFSET + 0x22], ==, 16);
  g_assert_cmpuint (data[GOODIX_CHICAGO_BIR_STANDARD_OFFSET + 0x23], ==, 0);

  g_assert_true (goodix_chicago_bir_parse (data, len, &view, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (view.columns, ==, GOODIX_CHICAGO_IMAGE_WIDTH);
  g_assert_cmpuint (view.rows, ==, GOODIX_CHICAGO_IMAGE_HEIGHT);
  g_assert_cmpuint (read_le16 (view.raw16_le), ==, raw[0]);
  g_assert_cmpuint (read_le16 (view.raw16_le + sizeof (guint16)), ==, raw[1]);
  g_assert_cmpuint (read_le16 (view.raw16_le +
                               (GOODIX_CHICAGO_PIXELS - 1) * sizeof (guint16)),
                    ==, raw[GOODIX_CHICAGO_PIXELS - 1]);
}

static void
test_rejects_invalid_bir (void)
{
  guint16 raw[GOODIX_CHICAGO_PIXELS] = { 0 };
  g_autoptr(GBytes) bir = NULL;
  g_autoptr(GError) error = NULL;
  GoodixChicagoBirView view;
  gsize len = 0;
  const guint8 *data;
  g_autofree guint8 *bad = NULL;

  g_assert_null (goodix_chicago_bir_build (NULL, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);

  bir = goodix_chicago_bir_build (raw, &error);
  g_assert_no_error (error);
  data = g_bytes_get_data (bir, &len);

  g_assert_false (goodix_chicago_bir_parse (data, len - 1, &view, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  bad = g_memdup2 (data, len);
  bad[GOODIX_CHICAGO_BIR_STANDARD_OFFSET + 0x22] = 8;
  g_assert_false (goodix_chicago_bir_parse (bad, len, &view, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/gdix51c0/chicago-bir/build-and-parse", test_build_and_parse);
  g_test_add_func ("/gdix51c0/chicago-bir/rejects-invalid-bir", test_rejects_invalid_bir);

  return g_test_run ();
}
