// SPDX-License-Identifier: LGPL-2.1-or-later
/* Copyright (C) 2026 Berke Kabagöz <berkekbgz@gmail.com> */

#include <glib.h>
#include <string.h>

#include "goodix-chicago-feature.h"

typedef struct
{
  gint32 original_x;
  gint32 original_y;
  gint32 original_scale;
  gint32 original_response;
  gint32 accepted;
  GoodixChicagoCandidate candidate;
  guint32 curvature;
} ChicagoRefinementRecord;

G_STATIC_ASSERT (sizeof (ChicagoRefinementRecord) == 48);

static void
test_builds_uniform_scale_space (void)
{
  guint8 enhanced[GOODIX_CHICAGO_FEATURE_PIXELS] = { 0, };
  guint8 source[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint8 orientation_map[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint8 prepared[GOODIX_CHICAGO_FEATURE_PIXELS];
  gint32 gradient_input[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint32 magnitude[GOODIX_CHICAGO_FEATURE_PIXELS];
  gint16 orientation[GOODIX_CHICAGO_FEATURE_PIXELS];
  g_autofree guint16 (*scales)[GOODIX_CHICAGO_FEATURE_PIXELS] =
    g_malloc0_n (GOODIX_CHICAGO_FEATURE_SCALES, sizeof (*scales));

  goodix_chicago_feature_build_source (enhanced, source);
  g_assert_cmpmem (source, sizeof (source), enhanced, sizeof (enhanced));
  goodix_chicago_feature_build_scale_space (enhanced, scales);
  for (guint scale = 0; scale < GOODIX_CHICAGO_FEATURE_SCALES; scale++)
    for (guint pixel = 0; pixel < GOODIX_CHICAGO_FEATURE_PIXELS; pixel++)
      g_assert_cmpuint (scales[scale][pixel], ==, 0);

  goodix_chicago_feature_build_orientation_map (enhanced, orientation_map);
  goodix_chicago_feature_build_prepared_image (enhanced, prepared);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_FEATURE_PIXELS; pixel++)
    {
      g_assert_cmpuint (orientation_map[pixel], ==, 45);
      g_assert_cmpuint (prepared[pixel], ==, 0);
    }

  goodix_chicago_feature_build_gradient_input (prepared, gradient_input);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_FEATURE_PIXELS; pixel++)
    g_assert_cmpint (gradient_input[pixel], ==, 0);
  goodix_chicago_feature_build_gradients (gradient_input, magnitude,
                                             orientation);
  for (guint y = 0; y < GOODIX_CHICAGO_FEATURE_HEIGHT; y++)
    for (guint x = 0; x < GOODIX_CHICAGO_FEATURE_WIDTH; x++)
      {
        const guint pixel = y * GOODIX_CHICAGO_FEATURE_WIDTH + x;
        const gboolean border =
          x == 0 || x + 1 == GOODIX_CHICAGO_FEATURE_WIDTH ||
          y == 0 || y + 1 == GOODIX_CHICAGO_FEATURE_HEIGHT;

        g_assert_cmpuint (magnitude[pixel], ==, 0);
        g_assert_cmpint (orientation[pixel], ==,
                         border ? 0 : (gint16) 0x3244);
      }
}

static void
test_matches_oracle_scale_space (void)
{
  const gchar *enhanced_path = g_getenv ("CHICAGO_FEATURE_ENHANCED");
  const gchar *source_path = g_getenv ("CHICAGO_FEATURE_SOURCE");
  const gchar *scale_directory = g_getenv ("CHICAGO_FEATURE_SCALE_DIR");
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *expected_source = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 (*scales)[GOODIX_CHICAGO_FEATURE_PIXELS] = NULL;
  guint8 source[GOODIX_CHICAGO_FEATURE_PIXELS];
  gsize enhanced_size;
  gsize source_size;

  if (!enhanced_path || !source_path || !scale_directory)
    {
      g_test_skip ("Chicago Wine feature-scale vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (source_path, &expected_source,
                                      &source_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (source_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);

  scales = g_malloc_n (GOODIX_CHICAGO_FEATURE_SCALES, sizeof (*scales));
  goodix_chicago_feature_build_source ((const guint8 *) enhanced, source);
  g_assert_cmpmem (source, sizeof (source), expected_source, source_size);
  goodix_chicago_feature_build_scale_space ((const guint8 *) enhanced,
                                               scales);

  for (guint scale = 0; scale < GOODIX_CHICAGO_FEATURE_SCALES; scale++)
    {
      g_autofree gchar *path =
        g_strdup_printf ("%s/scale-%u.bin", scale_directory, scale);
      g_autofree gchar *expected = NULL;
      gsize expected_size;

      g_assert_true (g_file_get_contents (path, &expected, &expected_size,
                                          &error));
      g_assert_no_error (error);
      g_assert_cmpuint (expected_size, ==,
                        GOODIX_CHICAGO_FEATURE_PIXELS * sizeof (guint16));
      g_assert_cmpmem (scales[scale], expected_size, expected, expected_size);
    }
}

static void
test_matches_oracle_gradients (void)
{
  const gchar *feature_image_path =
    g_getenv ("CHICAGO_FEATURE_GRADIENT_IMAGE");
  const gchar *enhanced_path = g_getenv ("CHICAGO_FEATURE_ENHANCED");
  const gchar *orientation_map_path =
    g_getenv ("CHICAGO_FEATURE_ORIENTATION_MAP");
  const gchar *input_path = g_getenv ("CHICAGO_FEATURE_GRADIENT_INPUT");
  const gchar *magnitude_path = g_getenv ("CHICAGO_FEATURE_MAGNITUDE");
  const gchar *orientation_path = g_getenv ("CHICAGO_FEATURE_ORIENTATION");
  g_autofree gchar *feature_image = NULL;
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *expected_orientation_map = NULL;
  g_autofree gchar *expected_input = NULL;
  g_autofree gchar *expected_magnitude = NULL;
  g_autofree gchar *expected_orientation = NULL;
  g_autoptr(GError) error = NULL;
  guint32 magnitude[GOODIX_CHICAGO_FEATURE_PIXELS];
  gint16 orientation[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint8 orientation_map[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint8 prepared[GOODIX_CHICAGO_FEATURE_PIXELS];
  gint32 gradient_input[GOODIX_CHICAGO_FEATURE_PIXELS];
  gsize feature_image_size;
  gsize enhanced_size;
  gsize orientation_map_size;
  gsize input_size;
  gsize magnitude_size;
  gsize orientation_size;

  if (!feature_image_path || !enhanced_path || !orientation_map_path ||
      !input_path || !magnitude_path || !orientation_path)
    {
      g_test_skip ("Chicago Wine gradient vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (feature_image_path, &feature_image,
                                      &feature_image_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (orientation_map_path,
                                      &expected_orientation_map,
                                      &orientation_map_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (input_path, &expected_input,
                                      &input_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (magnitude_path, &expected_magnitude,
                                      &magnitude_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (orientation_path, &expected_orientation,
                                      &orientation_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (feature_image_size, ==,
                    GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (orientation_map_size, ==,
                    GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (input_size, ==,
                    GOODIX_CHICAGO_FEATURE_PIXELS * sizeof (guint32));
  g_assert_cmpuint (magnitude_size, ==,
                    GOODIX_CHICAGO_FEATURE_PIXELS * sizeof (guint32));
  g_assert_cmpuint (orientation_size, ==,
                    GOODIX_CHICAGO_FEATURE_PIXELS * sizeof (gint16));

  goodix_chicago_feature_build_orientation_map ((const guint8 *) enhanced,
                                                   orientation_map);
  g_assert_cmpmem (orientation_map, sizeof (orientation_map),
                   expected_orientation_map, orientation_map_size);
  goodix_chicago_feature_build_prepared_image ((const guint8 *) enhanced,
                                                  prepared);
  g_assert_cmpmem (prepared, sizeof (prepared), feature_image,
                   feature_image_size);
  goodix_chicago_feature_build_gradient_input (prepared, gradient_input);
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_FEATURE_PIXELS; pixel++)
    g_assert_cmpint (gradient_input[pixel], ==,
                     ((const gint32 *) expected_input)[pixel]);
  goodix_chicago_feature_build_gradients (gradient_input, magnitude,
                                             orientation);
  g_assert_cmpmem (magnitude, sizeof (magnitude),
                   expected_magnitude, magnitude_size);
  g_assert_cmpmem (orientation, sizeof (orientation),
                   expected_orientation, orientation_size);
}

static void
test_matches_oracle_extrema (void)
{
  const gchar *enhanced_path = g_getenv ("CHICAGO_FEATURE_ENHANCED");
  const gchar *extrema_path = g_getenv ("CHICAGO_FEATURE_EXTREMA");
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *expected = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 (*scales)[GOODIX_CHICAGO_FEATURE_PIXELS] = NULL;
  g_autofree GoodixChicagoExtremum *actual = NULL;
  const GoodixChicagoExtremum *expected_records;
  guint32 expected_count;
  guint actual_count;
  gsize enhanced_size;
  gsize expected_size;

  if (!enhanced_path || !extrema_path)
    {
      g_test_skip ("Chicago Wine extrema vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (extrema_path, &expected,
                                      &expected_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (expected_size, >=, sizeof (expected_count));
  memcpy (&expected_count, expected, sizeof (expected_count));
  g_assert_cmpuint (expected_size, ==,
                    sizeof (expected_count) +
                    expected_count * sizeof (GoodixChicagoExtremum));
  expected_records = (const GoodixChicagoExtremum *)
    (expected + sizeof (expected_count));

  scales = g_malloc_n (GOODIX_CHICAGO_FEATURE_SCALES, sizeof (*scales));
  actual = g_new (GoodixChicagoExtremum, expected_count + 1);
  goodix_chicago_feature_build_scale_space ((const guint8 *) enhanced,
                                               scales);
  actual_count = goodix_chicago_feature_collect_extrema (
    (const guint16 (*)[GOODIX_CHICAGO_FEATURE_PIXELS]) scales,
    actual, expected_count + 1);
  g_assert_cmpuint (actual_count, ==, expected_count);
  g_assert_cmpmem (actual, actual_count * sizeof (*actual), expected_records,
                   expected_count * sizeof (*expected_records));
}

static void
test_matches_oracle_refinement (void)
{
  const gchar *enhanced_path = g_getenv ("CHICAGO_FEATURE_ENHANCED");
  const gchar *refinement_path = g_getenv ("CHICAGO_FEATURE_REFINEMENT");
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *expected = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 (*scales)[GOODIX_CHICAGO_FEATURE_PIXELS] = NULL;
  const ChicagoRefinementRecord *records;
  guint32 expected_count;
  guint32 expected_accepted;
  guint32 actual_accepted = 0;
  gsize enhanced_size;
  gsize expected_size;

  if (!enhanced_path || !refinement_path)
    {
      g_test_skip ("Chicago Wine refinement vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (refinement_path, &expected,
                                      &expected_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (expected_size, >=, 2 * sizeof (guint32));
  memcpy (&expected_count, expected, sizeof (expected_count));
  memcpy (&expected_accepted, expected + sizeof (guint32),
          sizeof (expected_accepted));
  g_assert_cmpuint (expected_size, ==,
                    2 * sizeof (guint32) +
                    expected_count * sizeof (ChicagoRefinementRecord));
  records = (const ChicagoRefinementRecord *)
    (expected + 2 * sizeof (guint32));

  scales = g_malloc_n (GOODIX_CHICAGO_FEATURE_SCALES, sizeof (*scales));
  goodix_chicago_feature_build_scale_space ((const guint8 *) enhanced,
                                               scales);
  for (guint record_index = 0; record_index < expected_count; record_index++)
    {
      const ChicagoRefinementRecord *record = &records[record_index];
      GoodixChicagoCandidate candidate = {
        .x = record->original_x,
        .y = record->original_y,
        .scale = record->original_scale,
        .strength = record->original_response,
      };
      guint32 curvature = 0;
      const gboolean accepted = goodix_chicago_feature_refine_extremum (
        (const guint16 (*)[GOODIX_CHICAGO_FEATURE_PIXELS]) scales,
        &candidate, &curvature);

      g_assert_cmpint (accepted, ==, record->accepted != 0);
      g_assert_cmpmem (&candidate, sizeof (candidate), &record->candidate,
                       sizeof (record->candidate));
      g_assert_cmpuint (curvature, ==, record->curvature);
      actual_accepted += accepted;
    }
  g_assert_cmpuint (actual_accepted, ==, expected_accepted);
}

static void
test_matches_oracle_materialization (void)
{
  const gchar *enhanced_path = g_getenv ("CHICAGO_FEATURE_ENHANCED");
  const gchar *materialization_path =
    g_getenv ("CHICAGO_FEATURE_MATERIALIZATION");
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *expected = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 (*scales)[GOODIX_CHICAGO_FEATURE_PIXELS] = NULL;
  g_autofree GoodixChicagoExtremum *extrema = NULL;
  g_autofree GoodixChicagoFeatureRecord *records = NULL;
  g_autofree GoodixChicagoFeatureRank *ranks = NULL;
  g_autofree GoodixChicagoFeatureAux *aux = NULL;
  const GoodixChicagoFeatureRecord *expected_records;
  const GoodixChicagoFeatureRank *expected_ranks;
  const GoodixChicagoFeatureAux *expected_aux;
  guint8 prepared[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint8 feature_source[GOODIX_CHICAGO_FEATURE_PIXELS];
  gint32 gradient_input[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint32 magnitude[GOODIX_CHICAGO_FEATURE_PIXELS];
  gint16 orientation[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint32 expected_raw_count;
  guint32 expected_feature_count;
  guint actual_feature_count;
  gsize enhanced_size;
  gsize expected_size;
  gsize offset;

  if (!enhanced_path || !materialization_path)
    {
      g_test_skip ("Chicago Wine materialization vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (materialization_path, &expected,
                                      &expected_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (expected_size, >=, 2 * sizeof (guint32));
  memcpy (&expected_raw_count, expected, sizeof (expected_raw_count));
  memcpy (&expected_feature_count, expected + sizeof (guint32),
          sizeof (expected_feature_count));
  g_assert_cmpuint (expected_size, ==,
                    2 * sizeof (guint32) + expected_feature_count *
                    (sizeof (GoodixChicagoFeatureRecord) +
                     sizeof (GoodixChicagoFeatureRank) +
                     sizeof (GoodixChicagoFeatureAux)));
  offset = 2 * sizeof (guint32);
  expected_records = (const GoodixChicagoFeatureRecord *)
    (expected + offset);
  offset += expected_feature_count * sizeof (*expected_records);
  expected_ranks = (const GoodixChicagoFeatureRank *) (expected + offset);
  offset += expected_feature_count * sizeof (*expected_ranks);
  expected_aux = (const GoodixChicagoFeatureAux *) (expected + offset);

  scales = g_malloc_n (GOODIX_CHICAGO_FEATURE_SCALES, sizeof (*scales));
  goodix_chicago_feature_build_scale_space ((const guint8 *) enhanced,
                                               scales);
  extrema = g_new (GoodixChicagoExtremum, expected_raw_count);
  g_assert_cmpuint (goodix_chicago_feature_collect_extrema (
                      (const guint16 (*)[GOODIX_CHICAGO_FEATURE_PIXELS])
                        scales, extrema, expected_raw_count),
                    ==, expected_raw_count);
  goodix_chicago_feature_build_prepared_image ((const guint8 *) enhanced,
                                                  prepared);
  goodix_chicago_feature_build_source ((const guint8 *) enhanced,
                                         feature_source);
  goodix_chicago_feature_build_gradient_input (prepared, gradient_input);
  goodix_chicago_feature_build_gradients (gradient_input, magnitude,
                                             orientation);
  records = g_new0 (GoodixChicagoFeatureRecord, expected_feature_count);
  ranks = g_new0 (GoodixChicagoFeatureRank, expected_feature_count);
  aux = g_new0 (GoodixChicagoFeatureAux, expected_feature_count);

  actual_feature_count = goodix_chicago_feature_collect_candidates (
    feature_source,
    (const guint16 (*)[GOODIX_CHICAGO_FEATURE_PIXELS]) scales,
    magnitude, orientation, records, ranks, aux, expected_feature_count);

  g_assert_cmpuint (actual_feature_count, ==, expected_feature_count);
  g_assert_cmpmem (records, actual_feature_count * sizeof (*records),
                   expected_records,
                   expected_feature_count * sizeof (*expected_records));
  g_assert_cmpmem (ranks, actual_feature_count * sizeof (*ranks),
                   expected_ranks,
                   expected_feature_count * sizeof (*expected_ranks));
  g_assert_cmpmem (aux, actual_feature_count * sizeof (*aux), expected_aux,
                   expected_feature_count * sizeof (*expected_aux));
}

static void
test_matches_oracle_descriptors (void)
{
  const gchar *enhanced_path = g_getenv ("CHICAGO_FEATURE_ENHANCED");
  const gchar *materialization_path =
    g_getenv ("CHICAGO_FEATURE_MATERIALIZATION");
  const gchar *descriptor_path = g_getenv ("CHICAGO_FEATURE_DESCRIPTOR");
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *materialization = NULL;
  g_autofree gchar *descriptor = NULL;
  g_autoptr(GError) error = NULL;
  const GoodixChicagoFeatureRecord *input_records;
  const GoodixChicagoFeatureAux *input_aux;
  const GoodixChicagoFeatureRecord *expected_records;
  guint8 prepared[GOODIX_CHICAGO_FEATURE_PIXELS];
  gint32 gradient_input[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint32 magnitude[GOODIX_CHICAGO_FEATURE_PIXELS];
  gint16 orientation[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint32 feature_count;
  guint32 expected_count;
  gsize enhanced_size;
  gsize materialization_size;
  gsize descriptor_size;
  gsize aux_offset;

  if (!enhanced_path || !materialization_path || !descriptor_path)
    {
      g_test_skip ("Chicago Wine descriptor vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (materialization_path, &materialization,
                                      &materialization_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (descriptor_path, &descriptor,
                                      &descriptor_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (materialization_size, >=, 2 * sizeof (guint32));
  g_assert_cmpuint (descriptor_size, >=, sizeof (guint32));
  memcpy (&feature_count, materialization + sizeof (guint32),
          sizeof (feature_count));
  memcpy (&expected_count, descriptor, sizeof (expected_count));
  g_assert_cmpuint (feature_count, ==, expected_count);
  g_assert_cmpuint (materialization_size, ==,
                    2 * sizeof (guint32) + feature_count *
                    (sizeof (GoodixChicagoFeatureRecord) +
                     sizeof (GoodixChicagoFeatureRank) +
                     sizeof (GoodixChicagoFeatureAux)));
  g_assert_cmpuint (descriptor_size, ==,
                    sizeof (guint32) + feature_count *
                    sizeof (GoodixChicagoFeatureRecord));
  input_records = (const GoodixChicagoFeatureRecord *)
    (materialization + 2 * sizeof (guint32));
  aux_offset = 2 * sizeof (guint32) + feature_count *
    (sizeof (GoodixChicagoFeatureRecord) +
     sizeof (GoodixChicagoFeatureRank));
  input_aux = (const GoodixChicagoFeatureAux *)
    (materialization + aux_offset);
  expected_records = (const GoodixChicagoFeatureRecord *)
    (descriptor + sizeof (guint32));

  goodix_chicago_feature_build_prepared_image ((const guint8 *) enhanced,
                                                  prepared);
  goodix_chicago_feature_build_gradient_input (prepared, gradient_input);
  goodix_chicago_feature_build_gradients (gradient_input, magnitude,
                                             orientation);
  for (guint index = 0; index < feature_count; index++)
    {
      GoodixChicagoFeatureRecord record = input_records[index];

      goodix_chicago_feature_build_descriptor (magnitude, orientation,
                                                  &input_aux[index], &record);
      g_assert_cmpmem (&record, sizeof (record), &expected_records[index],
                       sizeof (expected_records[index]));
    }
}

static void
test_matches_oracle_finalized_records (void)
{
  const gchar *descriptor_path = g_getenv ("CHICAGO_FEATURE_DESCRIPTOR");
  const gchar *finalized_path = g_getenv ("CHICAGO_FEATURE_FINALIZED");
  g_autofree gchar *descriptor = NULL;
  g_autofree gchar *finalized = NULL;
  g_autoptr(GError) error = NULL;
  const GoodixChicagoFeatureRecord *input_records;
  const GoodixChicagoFeatureRecord *expected_records;
  guint32 count;
  gsize descriptor_size;
  gsize finalized_size;

  if (!descriptor_path || !finalized_path)
    {
      g_test_skip ("Chicago Wine finalized records were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (descriptor_path, &descriptor,
                                      &descriptor_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (finalized_path, &finalized,
                                      &finalized_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (descriptor_size, >=, sizeof (count));
  memcpy (&count, descriptor, sizeof (count));
  g_assert_cmpuint (descriptor_size, ==,
                    sizeof (count) + count *
                    sizeof (GoodixChicagoFeatureRecord));
  g_assert_cmpuint (finalized_size, ==,
                    count * sizeof (GoodixChicagoFeatureRecord));
  input_records = (const GoodixChicagoFeatureRecord *)
    (descriptor + sizeof (count));
  expected_records = (const GoodixChicagoFeatureRecord *) finalized;

  for (guint index = 0; index < count; index++)
    {
      GoodixChicagoFeatureRecord record = input_records[index];

      goodix_chicago_feature_finalize_record (&record);
      if (memcmp (&record, &expected_records[index], sizeof (record)) != 0)
        {
          for (guint byte = 0; byte < sizeof (record); byte++)
            if (((const guint8 *) &record)[byte] !=
                ((const guint8 *) &expected_records[index])[byte])
              {
                g_test_message ("record=%u byte=%u actual=%02x expected=%02x",
                                index, byte,
                                ((const guint8 *) &record)[byte],
                                ((const guint8 *) &expected_records[index])[byte]);
                break;
              }
        }
      g_assert_cmpmem (&record, sizeof (record), &expected_records[index],
                       sizeof (expected_records[index]));
    }
}

static void
test_post_extraction_primitives (void)
{
  static const guint8 expected_packed[24] = {
    0x10, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c, 0x1f,
    0x11, 0x12, 0x14, 0x17, 0x18, 0x1b, 0x1d, 0x1e,
    0x20, 0x21, 0x22, 0x23, 0x00, 0x00, 0x00, 0x00,
  };
  GoodixChicagoFeatureRecord records[4] = { 0, };
  GoodixChicagoFeatureConsensus consensus;
  guint8 annotations[4] = { 10, 11, 12, 13 };
  guint8 mask[GOODIX_CHICAGO_FEATURE_PIXELS];
  guint count;

  for (guint byte = 0; byte < sizeof (records[0]); byte++)
    ((guint8 *) &records[0])[byte] = byte;
  goodix_chicago_feature_pack_record (&records[0], FALSE);
  g_assert_cmpmem (((guint8 *) &records[0]) + 0x10,
                   sizeof (expected_packed), expected_packed,
                   sizeof (expected_packed));

  memset (records, 0, sizeof (records));
  ((guint8 *) &records[0])[0] = 1;
  ((guint8 *) &records[1])[0] = 0;
  ((guint8 *) &records[2])[0] = 1;
  ((guint8 *) &records[3])[0] = 0;
  count = goodix_chicago_feature_partition_records (records, annotations, 4);
  g_assert_cmpuint (count, ==, 2);
  g_assert_cmpuint (((guint8 *) &records[0])[0], ==, 0);
  g_assert_cmpuint (((guint8 *) &records[1])[0], ==, 0);
  g_assert_cmpuint (((guint8 *) &records[2])[0], ==, 1);
  g_assert_cmpuint (((guint8 *) &records[3])[0], ==, 1);
  g_assert_cmpuint (annotations[0], ==, 13);
  g_assert_cmpuint (annotations[1], ==, 11);
  g_assert_cmpuint (annotations[2], ==, 12);
  g_assert_cmpuint (annotations[3], ==, 10);

  memset (records, 0, sizeof (records));
  memset (mask, 1, sizeof (mask));
  records[0].refined_x = 1 << 8;
  records[0].refined_y = 2 << 8;
  records[1].refined_x = 3 << 8;
  records[1].refined_y = 4 << 8;
  records[2].refined_x = 5 << 8;
  records[2].refined_y = 6 << 8;
  ((guint8 *) &records[0])[10] = 10;
  ((guint8 *) &records[1])[10] = 11;
  ((guint8 *) &records[2])[10] = 12;
  mask[4 * GOODIX_CHICAGO_FEATURE_WIDTH + 3] = 0;
  count = goodix_chicago_feature_filter_mask (mask, records, 3);
  g_assert_cmpuint (count, ==, 2);
  g_assert_cmpuint (((guint8 *) &records[0])[10], ==, 10);
  g_assert_cmpuint (((guint8 *) &records[1])[10], ==, 12);
  g_assert_cmpuint (((guint8 *) &records[2])[10], ==, 0);

  memset (records, 0, sizeof (records));
  records[0].refined_x = 40 << 8;
  records[0].refined_y = 32 << 8;
  ((guint8 *) &records[0])[0x38] = 1;
  g_assert_true (goodix_chicago_feature_classify_status_full (
                   records, 1, 24, &consensus));
  g_assert_cmpuint (consensus.negative_percent, ==, 0);
  g_assert_cmpuint (consensus.positive_percent, ==, 0);
  g_assert_cmpuint (consensus.neutral_percent, ==, 100);
  g_assert_cmpuint (consensus.density_class, ==, 0);
  g_assert_cmpuint (consensus.inactive_count, ==, 0);

  memset (records, 0, sizeof (records));
  memset (&consensus, 0xff, sizeof (consensus));
  g_assert_false (goodix_chicago_feature_classify_status_full (
                    records, 1, 24, &consensus));
  g_assert_cmpuint (consensus.negative_percent, ==, 0);
  g_assert_cmpuint (consensus.positive_percent, ==, 0);
  g_assert_cmpuint (consensus.neutral_percent, ==, 0);
  g_assert_cmpuint (consensus.density_class, ==, 0);
  g_assert_cmpuint (consensus.inactive_count, ==, 0);
}

static void
test_matches_oracle_post_extraction (void)
{
  const gchar *enhanced_path = g_getenv ("CHICAGO_FEATURE_ENHANCED");
  const gchar *finalized_path = g_getenv ("CHICAGO_FEATURE_FINALIZED");
  const gchar *mask_path = g_getenv ("CHICAGO_FEATURE_MASK");
  const gchar *post_mask_path = g_getenv ("CHICAGO_FEATURE_POST_MASK");
  const gchar *post_quality_path = g_getenv ("CHICAGO_FEATURE_POST_QUALITY");
  const gchar *annotations_path = g_getenv ("CHICAGO_FEATURE_ANNOTATIONS");
  const gchar *pre_annotations_path =
    g_getenv ("CHICAGO_FEATURE_PRE_ANNOTATIONS");
  g_autofree gchar *enhanced = NULL;
  g_autofree gchar *finalized = NULL;
  g_autofree gchar *mask = NULL;
  g_autofree gchar *post_mask = NULL;
  g_autofree gchar *post_quality = NULL;
  g_autofree gchar *annotations = NULL;
  g_autofree gchar *pre_annotations = NULL;
  g_autofree GoodixChicagoFeatureRecord *records = NULL;
  g_autofree GoodixChicagoFeatureRecord *pipeline_records = NULL;
  g_autofree guint8 *native_annotations = NULL;
  g_autofree guint8 *pipeline_annotations = NULL;
  g_autoptr(GError) error = NULL;
  gsize enhanced_size;
  gsize finalized_size;
  gsize mask_size;
  gsize post_mask_size;
  gsize post_quality_size;
  gsize annotations_size;
  gsize pre_annotations_size;
  guint original_count;
  guint filtered_count;
  guint active_count;
  guint pipeline_count;
  guint pipeline_active;
  guint expected_active = 0;

  guint8 native_mask[GOODIX_CHICAGO_FEATURE_PIXELS];

  if (!enhanced_path || !finalized_path || !mask_path || !post_mask_path ||
      !post_quality_path || !annotations_path || !pre_annotations_path)
    {
      g_test_skip ("Chicago Wine post-extraction vectors were not requested");
      return;
    }

  g_assert_true (g_file_get_contents (enhanced_path, &enhanced,
                                      &enhanced_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (finalized_path, &finalized,
                                      &finalized_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (mask_path, &mask, &mask_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (post_mask_path, &post_mask,
                                      &post_mask_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (post_quality_path, &post_quality,
                                      &post_quality_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (annotations_path, &annotations,
                                      &annotations_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (pre_annotations_path, &pre_annotations,
                                      &pre_annotations_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (finalized_size % sizeof (*records), ==, 0);
  g_assert_cmpuint (mask_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
  g_assert_cmpuint (post_mask_size, ==, finalized_size);
  g_assert_cmpuint (post_quality_size, ==, finalized_size);

  original_count = finalized_size / sizeof (*records);
  g_assert_cmpuint (annotations_size, ==, original_count);
  g_assert_cmpuint (pre_annotations_size, ==, original_count);
  records = g_memdup2 (finalized, finalized_size);
  pipeline_records = g_memdup2 (finalized, finalized_size);
  native_annotations = g_new0 (guint8, original_count);
  pipeline_annotations = g_new0 (guint8, original_count);
  pipeline_count = goodix_chicago_feature_prepare_subtemplate (
    (const guint8 *) enhanced, pipeline_records, original_count,
    pipeline_annotations, &pipeline_active);
  goodix_chicago_feature_build_mask ((const guint8 *) enhanced, native_mask);
  g_assert_cmpmem (native_mask, sizeof (native_mask), mask, mask_size);
  goodix_chicago_feature_build_annotations (
    (const guint8 *) enhanced, native_mask, records, original_count,
    native_annotations);
  g_assert_cmpmem (native_annotations, original_count, pre_annotations,
                   pre_annotations_size);
  g_assert_true (goodix_chicago_feature_classify_status (records,
                                                           original_count));
  filtered_count = goodix_chicago_feature_filter_mask (
    (const guint8 *) mask, records, original_count);

  for (guint index = 0; index < filtered_count; index++)
    {
      const guint8 *expected = (const guint8 *) post_mask +
                               index * sizeof (*records);
      const guint8 *actual = (const guint8 *) &records[index];

      g_assert_cmpmem (actual, sizeof (*records), expected,
                       sizeof (*records));
    }

  active_count = goodix_chicago_feature_partition_records (
    records, native_annotations, filtered_count);
  g_assert_cmpmem (native_annotations, filtered_count, annotations,
                   filtered_count);
  for (guint index = 0; index < filtered_count; index++)
    goodix_chicago_feature_pack_record (&records[index], FALSE);
  goodix_chicago_feature_score_neighbors (
    records, native_annotations, filtered_count);

  while (expected_active < filtered_count &&
         (((const guint8 *) post_quality)
          [expected_active * sizeof (*records)] & 3u) == 0)
    expected_active++;
  g_assert_cmpuint (active_count, ==, expected_active);
  g_assert_cmpuint (pipeline_count, ==, filtered_count);
  g_assert_cmpuint (pipeline_active, ==, active_count);
  g_assert_cmpmem (pipeline_annotations, filtered_count, native_annotations,
                   filtered_count);
  for (guint index = 0; index < filtered_count; index++)
    {
      const guint8 *expected = (const guint8 *) post_quality +
                               index * sizeof (*records);
      const guint8 *actual = (const guint8 *) &records[index];

      g_assert_cmpmem (actual, sizeof (*records), expected,
                       sizeof (*records));
      g_assert_cmpmem (&pipeline_records[index], sizeof (*pipeline_records),
                       expected, sizeof (*pipeline_records));
    }
}

static void
test_builds_live_auxiliary (void)
{
  GoodixChicagoFeatureLiveAuxiliary auxiliary;
  const guint8 expected_code6[6] = { 1, 0, 0, 6, 0, 0 };
  const guint8 expected_code7[6] = { 0, 0, 0, 8, 0, 0 };
  const guint8 expected_zero[6] = { 0, 0, 0, 4, 0, 0 };

  goodix_chicago_feature_build_live_auxiliary (1, 0x200, &auxiliary);
  g_assert_cmpmem (auxiliary.values, sizeof (auxiliary.values),
                   expected_code6, sizeof (expected_code6));
  goodix_chicago_feature_build_live_auxiliary (0, 0x400, &auxiliary);
  g_assert_cmpmem (auxiliary.values, sizeof (auxiliary.values),
                   expected_code7, sizeof (expected_code7));
  goodix_chicago_feature_build_live_auxiliary (0, 0, &auxiliary);
  g_assert_cmpmem (auxiliary.values, sizeof (auxiliary.values),
                   expected_zero, sizeof (expected_zero));
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gdix51c0/chicago-feature/uniform-scale-space",
                   test_builds_uniform_scale_space);
  g_test_add_func ("/gdix51c0/chicago-feature/matches-oracle-scale-space",
                   test_matches_oracle_scale_space);
  g_test_add_func ("/gdix51c0/chicago-feature/matches-oracle-gradients",
                   test_matches_oracle_gradients);
  g_test_add_func ("/gdix51c0/chicago-feature/matches-oracle-extrema",
                   test_matches_oracle_extrema);
  g_test_add_func ("/gdix51c0/chicago-feature/matches-oracle-refinement",
                   test_matches_oracle_refinement);
  g_test_add_func ("/gdix51c0/chicago-feature/matches-oracle-materialization",
                   test_matches_oracle_materialization);
  g_test_add_func ("/gdix51c0/chicago-feature/matches-oracle-descriptors",
                   test_matches_oracle_descriptors);
  g_test_add_func ("/gdix51c0/chicago-feature/matches-oracle-finalized-records",
                   test_matches_oracle_finalized_records);
  g_test_add_func ("/gdix51c0/chicago-feature/post-extraction-primitives",
                   test_post_extraction_primitives);
  g_test_add_func ("/gdix51c0/chicago-feature/live-auxiliary",
                   test_builds_live_auxiliary);
  g_test_add_func ("/gdix51c0/chicago-feature/matches-oracle-post-extraction",
                   test_matches_oracle_post_extraction);
  return g_test_run ();
}
