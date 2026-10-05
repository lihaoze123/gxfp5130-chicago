// SPDX-License-Identifier: LGPL-2.1-or-later
/* Copyright (C) 2026 Berke Kabagöz <berkekbgz@gmail.com> */

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "goodix-chicago-match.h"
#include "goodix-chicago-preprocess.h"
#include "goodix-chicago-runtime.h"
#include "goodix-chicago-template.h"

typedef struct
{
  guint32 magic;
  guint32 version;
  guint32 old_count;
  guint32 new_count;
  gint32 config[8];
  guint32 selector_limit;
  guint32 best_multiplier;
  guint32 second_multiplier;
  guint32 selected_count;
} CandidateVectorHeader;

typedef struct
{
  guint32 magic;
  guint32 version;
  guint32 count;
  guint32 mode;
  guint32 strict;
  gint32 transform[6];
  gint32 error;
  guint8 inliers[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  guint8 padding[2];
  GoodixChicagoMatchPoint source[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  GoodixChicagoMatchPoint target[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  gint32 source_orientation[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  gint32 target_orientation[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
} GeometryVector;

G_STATIC_ASSERT (sizeof (GeometryVector) == 1100);

typedef struct
{
  guint32 magic;
  guint32 version;
  guint32 count;
  gint32 transform[6];
  gint32 target_orientation[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  gint32 source_orientation[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  guint8 initial[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  guint8 filtered[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
} OrientationVector;

G_STATIC_ASSERT (sizeof (OrientationVector) == 456);

typedef struct
{
  guint32 magic;
  guint32 version;
  guint32 count;
  gint32 error_limit;
  GoodixChicagoMatchPoint source[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  GoodixChicagoMatchPoint target[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  guint8 inliers[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  guint8 padding[2];
  gint32 initial_transform[6];
  gint32 refined_transform[6];
} RefinementVector;

G_STATIC_ASSERT (sizeof (RefinementVector) == 780);

typedef struct
{
  guint32 magic;
  guint32 version;
  gint32 transform[6];
  guint8 result[0x150];
  guint8 work[0x3070];
} SubscoreVector;

G_STATIC_ASSERT (sizeof (SubscoreVector) == 12768);

typedef struct
{
  guint32 magic;
  guint32 version;
  gint32 transform[6];
  gint32 geometry_count;
  gint32 template_type;
  guint32 gallery_count;
  guint32 probe_count;
  GoodixChicagoMatchFeatureOverlap statistics;
  GoodixChicagoFeatureRecord gallery_records[180];
  GoodixChicagoFeatureRecord probe_records[180];
} FeatureOverlapVector;

typedef struct
{
  guint32 magic;
  guint32 version;
  guint32 count;
  guint32 reserved;
} RejectionCorpusHeader;

typedef struct
{
  gint32 width;
  gint32 height;
  gint32 probe_quality;
  gint32 template_type;
  gint32 record[0x68 / 4];
  gint32 transform[6];
  gint32 state[3];
  gint32 rejection_count_in;
  gint32 flag_in;
  gint32 rejected;
  gint32 rejection_count_out;
  gint32 flag_out;
} RejectionCorpusVector;

G_STATIC_ASSERT (sizeof (RejectionCorpusVector) == 176);

typedef struct
{
  guint32 magic;
  guint32 version;
  guint32 count;
  guint32 reserved;
} StudyAggregateCorpusHeader;

typedef struct
{
  gint32 count_zero;
  gint32 count_mixed;
  gint32 count_one;
  gint32 aggregate;
} StudyAggregateCorpusVector;

typedef struct
{
  guint32 magic;
  guint32 version;
  guint32 count;
  guint32 reserved;
} CandidatePrefilterCorpusHeader;

typedef struct
{
  gint32 record[0x68 / 4];
  gint32 auxiliary_count;
  gint32 candidate_metric;
  gint32 rejected;
} CandidatePrefilterCorpusVector;

G_STATIC_ASSERT (sizeof (FeatureOverlapVector) == 21692);

static const GoodixChicagoFeatureRecord *
load_features (const gchar  *path,
               gchar       **contents,
               guint32      *count,
               GError      **error)
{
  gsize size;

  if (!g_file_get_contents (path, contents, &size, error))
    return NULL;
  g_assert_cmpuint (size, >=, 12);
  memcpy (count, *contents, sizeof (*count));
  g_assert_cmpuint (size, ==,
                    12 + *count * sizeof (GoodixChicagoFeatureRecord));
  return (const GoodixChicagoFeatureRecord *) (*contents + 12);
}

static void
test_fallback_geometry_records (void)
{
  static const GoodixChicagoMatchPair pairs9[31] = {
    {12,0}, {7,12}, {4,19}, {21,5}, {26,6}, {16,7}, {19,8}, {15,9},
    {24,10}, {25,17}, {22,20}, {23,21}, {10,22}, {17,24}, {0,25},
    {3,29}, {58,32}, {60,33}, {37,45}, {38,36}, {42,40}, {53,38},
    {59,39}, {33,43}, {45,44}, {39,49}, {54,51}, {49,55}, {36,56},
    {44,59}, {-1,-1},
  };
  static const GoodixChicagoMatchPair pairs17[31] = {
    {17,0}, {23,1}, {21,2}, {13,4}, {31,21}, {18,17}, {30,9}, {24,12},
    {12,13}, {19,15}, {0,18}, {6,22}, {7,23}, {1,24}, {14,28},
    {56,59}, {40,35}, {62,39}, {35,40}, {34,42}, {60,43}, {58,44},
    {55,47}, {45,51}, {50,52}, {53,53}, {48,54}, {51,58}, {36,63},
    {-1,-1}, {-1,-1},
  };
  const gchar *case_name = g_getenv ("CHICAGO_MATCH_FALLBACK_CASE");
  const gchar *gallery_path =
    g_getenv ("CHICAGO_MATCH_FALLBACK_GALLERY_FEATURES");
  const gchar *template_path =
    g_getenv ("CHICAGO_MATCH_FALLBACK_TEMPLATE");
  const gchar *probe_path =
    g_getenv ("CHICAGO_MATCH_FALLBACK_PROBE_FEATURES");
  const gchar *gallery_enhanced_path =
    g_getenv ("CHICAGO_MATCH_FALLBACK_GALLERY_ENHANCED");
  const gchar *probe_enhanced_path =
    g_getenv ("CHICAGO_MATCH_FALLBACK_PROBE_ENHANCED");
  const GoodixChicagoMatchPair *pairs;
  GoodixChicagoMatchPair generated_pairs[GOODIX_CHICAGO_MATCH_PAIR_LIMIT];
  const GoodixChicagoFeatureRecord *gallery;
  const GoodixChicagoFeatureRecord *probe;
  g_autofree gchar *gallery_contents = NULL;
  g_autofree gchar *template_contents = NULL;
  g_autofree gchar *probe_contents = NULL;
  g_autofree gchar *gallery_enhanced = NULL;
  g_autofree gchar *probe_enhanced = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GoodixChicagoEnrollment) enrollment = NULL;
  GoodixChicagoSubtemplateView gallery_view;
  GoodixChicagoMatchGeometry result;
  GoodixChicagoMatchFallbackResult fallback_result;
  GoodixChicagoMatchAggregation aggregation = { 0, };
  GoodixChicagoMetricData gallery_metric;
  GoodixChicagoMetricData probe_metric;
  const GoodixChicagoMetricData *gallery_metric_data = NULL;
  guint32 gallery_count;
  guint32 probe_count;
  guint expected;
  guint selected;
  guint gallery_split = 0;
  guint probe_split = 0;
  gsize enhanced_size;

  if (!case_name || (!gallery_path && !template_path) || !probe_path)
    {
      g_test_skip ("Chicago fallback geometry oracle was not requested");
      return;
    }
  if (g_str_equal (case_name, "9"))
    {
      pairs = pairs9;
      expected = 8;
    }
  else
    {
      g_assert_cmpstr (case_name, ==, "17");
      pairs = pairs17;
      expected = 4;
    }
  if (template_path)
    {
      g_assert_true (g_file_get_contents (template_path, &template_contents,
                                          &enhanced_size, &error));
      g_assert_no_error (error);
      enrollment = goodix_chicago_enrollment_unpack (
        (const guint8 *) template_contents, enhanced_size, &error);
      g_assert_no_error (error);
      g_assert_nonnull (enrollment);
      g_assert_true (goodix_chicago_enrollment_get_subtemplate (
                       enrollment, expected == 8 ? 6 : 5, &gallery_view));
      gallery = gallery_view.records;
      gallery_count = gallery_view.record_count;
      gallery_split = gallery_view.active_count;
      gallery_metric_data = gallery_view.metric_data;
    }
  else
    {
      gallery = load_features (gallery_path, &gallery_contents, &gallery_count,
                               &error);
      g_assert_no_error (error);
      while (gallery_split < gallery_count &&
             (gallery[gallery_split].foreground & 3) == 0)
        gallery_split++;
    }
  probe = load_features (probe_path, &probe_contents, &probe_count, &error);
  g_assert_no_error (error);
  while (probe_split < probe_count &&
         (probe[probe_split].foreground & 3) == 0)
    probe_split++;
  selected = goodix_chicago_match_fallback_correspondences (
    gallery, gallery_count, gallery_split,
    probe, probe_count, probe_split, generated_pairs);
  g_assert_cmpuint (selected, ==, expected == 8 ? 30 : 29);
  g_assert_cmpmem (generated_pairs, sizeof (generated_pairs),
                   pairs, sizeof (generated_pairs));
  g_assert_cmpuint (goodix_chicago_match_geometry_consensus_records (
                      gallery, gallery_count, probe, probe_count,
                      generated_pairs, GOODIX_CHICAGO_MATCH_PAIR_LIMIT,
                      4, &result), ==, expected);
  g_test_message ("fallback case %s geometry=%u transform=%d,%d,%d,%d,%d,%d",
                  case_name, result.inlier_count,
                  result.transform[0], result.transform[1],
                  result.transform[2], result.transform[3],
                  result.transform[4], result.transform[5]);
  if (expected == 8)
    {
      g_assert_nonnull (probe_enhanced_path);
      if (gallery_metric_data == NULL)
        {
          g_assert_nonnull (gallery_enhanced_path);
          g_assert_true (g_file_get_contents (gallery_enhanced_path,
                                              &gallery_enhanced,
                                              &enhanced_size, &error));
          g_assert_no_error (error);
          g_assert_cmpuint (enhanced_size, ==,
                            GOODIX_CHICAGO_FEATURE_PIXELS);
          goodix_chicago_enrollment_build_metric_data (
            (const guint8 *) gallery_enhanced, &gallery_metric);
          gallery_metric_data = &gallery_metric;
        }
      g_assert_true (g_file_get_contents (probe_enhanced_path,
                                          &probe_enhanced, &enhanced_size,
                                          &error));
      g_assert_no_error (error);
      g_assert_cmpuint (enhanced_size, ==, GOODIX_CHICAGO_FEATURE_PIXELS);
      goodix_chicago_enrollment_build_metric_data (
        (const guint8 *) probe_enhanced, &probe_metric);
    }
  g_assert_false (
    goodix_chicago_match_aggregation_consume_fallback_records_type24 (
      &aggregation, gallery, gallery_count, gallery_split,
      expected == 8 ? gallery_metric_data : NULL,
      probe, probe_count, probe_split,
      expected == 8 ? &probe_metric : NULL, 207, &fallback_result));
  g_assert_cmpuint (fallback_result.correspondence_count, ==,
                    expected == 8 ? 30 : 29);
  g_assert_cmpuint (fallback_result.geometry.inlier_count, ==, expected);
  g_assert_cmpint (fallback_result.metric_evaluated, ==, expected == 8);
  if (expected == 8)
    {
      g_assert_cmpint (fallback_result.selector, ==, 128);
      g_assert_cmpint (fallback_result.agreement, ==, 225);
      g_assert_cmpint (fallback_result.coverage, ==, 113);
    }
  g_assert_cmpint (goodix_chicago_match_aggregation_score (&aggregation),
                   ==, expected == 8 ? -7 : -4);
}

static void
test_candidate_self (void)
{
  GoodixChicagoFeatureRecord records[2] = { 0, };
  GoodixChicagoMatchCandidateConfig config = {
    0, 2, 0, 2, 32, 72,
  };
  GoodixChicagoMatchCandidate candidates[2];
  guint8 distances[2 * GOODIX_CHICAGO_MATCH_MATRIX_STRIDE];
  guint8 directions[2 * GOODIX_CHICAGO_MATCH_MATRIX_STRIDE] = { 0, };

  memset (distances, 0xff, sizeof (distances));
  for (guint index = 0; index < G_N_ELEMENTS (records); index++)
    for (guint byte = 4; byte < 40; byte++)
      records[index].descriptor[byte] = index * 41 + byte;
  goodix_chicago_match_init_candidates (candidates,
                                           G_N_ELEMENTS (candidates));
  goodix_chicago_match_update_candidates (records, records, &config,
                                             candidates, distances,
                                             directions);
  for (guint index = 0; index < G_N_ELEMENTS (records); index++)
    {
      g_assert_cmpint (candidates[index].best_distance, ==, 0);
      g_assert_cmpint (candidates[index].best_index, ==, index);
      g_assert_cmpuint (distances[index *
                                  GOODIX_CHICAGO_MATCH_MATRIX_STRIDE +
                                  index], ==, 0);
    }
}

static void
test_ordinary_template_geometry (void)
{
  static const guint expected_self[8] = { 31, 3, 0, 0, 0, 0, 0, 0 };
  static const guint expected_9[8] = { 0, 0, 1, 3, 0, 0, 5, 4 };
  static const guint expected_17[8] = { 0, 4, 0, 0, 0, 5, 0, 0 };
  const gchar *case_name = g_getenv ("CHICAGO_MATCH_ORDINARY_CASE");
  const gchar *template_path = g_getenv ("CHICAGO_MATCH_ORDINARY_TEMPLATE");
  const gchar *probe_path = g_getenv ("CHICAGO_MATCH_ORDINARY_PROBE_FEATURES");
  const guint *expected;
  g_autofree gchar *template_contents = NULL;
  g_autofree gchar *probe_contents = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GoodixChicagoEnrollment) enrollment = NULL;
  const GoodixChicagoFeatureRecord *probe;
  guint32 probe_count;
  guint probe_split = 0;
  gsize template_size;

  if (!case_name || !template_path || !probe_path)
    {
      g_test_skip ("Chicago ordinary template geometry was not requested");
      return;
    }
  expected = g_str_equal (case_name, "self") ? expected_self :
    g_str_equal (case_name, "9") ? expected_9 : expected_17;
  g_assert_true (g_file_get_contents (template_path, &template_contents,
                                      &template_size, &error));
  g_assert_no_error (error);
  enrollment = goodix_chicago_enrollment_unpack (
    (const guint8 *) template_contents, template_size, &error);
  g_assert_no_error (error);
  g_assert_nonnull (enrollment);
  probe = load_features (probe_path, &probe_contents, &probe_count, &error);
  g_assert_no_error (error);
  while (probe_split < probe_count &&
         (probe[probe_split].foreground & 3) == 0)
    probe_split++;

  for (guint gallery_index = 0; gallery_index < 8; gallery_index++)
    {
      GoodixChicagoSubtemplateView gallery;
      GoodixChicagoMatchPair pairs[GOODIX_CHICAGO_MATCH_PAIR_LIMIT];
      GoodixChicagoMatchGeometry geometry;
      guint pair_count;

      g_assert_true (goodix_chicago_enrollment_get_subtemplate (
                       enrollment, gallery_index, &gallery));
      pair_count = goodix_chicago_match_ordinary_correspondences (
        gallery.records, gallery.record_count, gallery.active_count,
        probe, probe_count, probe_split, pairs);
      goodix_chicago_match_geometry_consensus_records (
        gallery.records, gallery.record_count, probe, probe_count,
        pairs, GOODIX_CHICAGO_MATCH_PAIR_LIMIT, 3, &geometry);
      g_test_message ("ordinary %s gallery=%u state=%u pairs=%u geometry=%u/%u "
                      "transform=%d,%d,%d,%d,%d,%d",
                      case_name, gallery_index, gallery.group_state, pair_count,
                      geometry.inlier_count, expected[gallery_index],
                      geometry.transform[0], geometry.transform[1],
                      geometry.transform[2], geometry.transform[3],
                      geometry.transform[4], geometry.transform[5]);
      g_assert_cmpuint (geometry.inlier_count, ==, expected[gallery_index]);
    }
}

static void
test_offline_template_score (void)
{
  const gchar *case_name = g_getenv ("CHICAGO_MATCH_OFFLINE_CASE");
  const gchar *gallery_path = g_getenv ("CHICAGO_MATCH_OFFLINE_GALLERY");
  const gchar *probe_path = g_getenv ("CHICAGO_MATCH_OFFLINE_PROBE");
  g_autofree gchar *gallery_contents = NULL;
  g_autofree gchar *probe_contents = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GoodixChicagoEnrollment) gallery = NULL;
  g_autoptr(GoodixChicagoEnrollment) probe = NULL;
  GoodixChicagoMatchTemplateResult match_result;
  GoodixChicagoSubtemplateView probe_view;
  gint32 expected;
  gint32 score;
  gsize gallery_size;
  gsize probe_size;

  if (!case_name || !gallery_path || !probe_path)
    {
      g_test_skip ("Chicago offline template score was not requested");
      return;
    }
  expected = g_str_equal (case_name, "self") ? 100 :
    g_str_equal (case_name, "9") ? -7 : -4;
  g_assert_true (g_file_get_contents (gallery_path, &gallery_contents,
                                      &gallery_size, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (probe_path, &probe_contents,
                                      &probe_size, &error));
  g_assert_no_error (error);
  gallery = goodix_chicago_enrollment_unpack (
    (const guint8 *) gallery_contents, gallery_size, &error);
  g_assert_no_error (error);
  g_assert_nonnull (gallery);
  probe = goodix_chicago_enrollment_unpack (
    (const guint8 *) probe_contents, probe_size, &error);
  g_assert_no_error (error);
  g_assert_nonnull (probe);
  g_assert_true (goodix_chicago_enrollment_get_subtemplate (
                   probe, 0, &probe_view));
  goodix_chicago_match_template_type24 (
    gallery, &probe_view, 207, &match_result);
  score = match_result.score;
  g_test_message ("offline case=%s quality=%u/%u native=%d official=%d",
                  case_name, probe_view.quality, probe_view.coverage,
                  score, expected);
  g_assert_cmpint (score, ==, expected);
  if (score > 0)
    {
      g_assert_true (match_result.selected_index_known);
      g_assert_cmpint (match_result.selected_index, >=, 0);
      g_assert_cmpint (match_result.selected_index, <,
                       goodix_chicago_enrollment_get_count (gallery));
    }
  else
    g_assert_false (match_result.selected_index_known);
  g_assert_cmpint (match_result.study_eligibility_known, ==, score > 0);
}

static void
test_offline_raw_score (void)
{
  const gchar *case_name = g_getenv ("CHICAGO_MATCH_RAW_CASE");
  const gchar *calibration_path = g_getenv ("CHICAGO_MATCH_RAW_CALIBRATION");
  const gchar *base_path = g_getenv ("CHICAGO_MATCH_RAW_BASE");
  const gchar *gallery_path = g_getenv ("CHICAGO_MATCH_RAW_GALLERY");
  const gchar *raw_path = g_getenv ("CHICAGO_MATCH_RAW_FRAME");
  const gchar *probe_template_path =
    g_getenv ("CHICAGO_MATCH_RAW_PROBE_TEMPLATE");
  const gchar *enhanced_path = g_getenv ("CHICAGO_MATCH_RAW_ENHANCED");
  const gchar *expected_value = g_getenv ("CHICAGO_MATCH_RAW_EXPECTED");
  g_autofree gchar *calibration_file = NULL;
  g_autofree gchar *base_file = NULL;
  g_autofree gchar *gallery_file = NULL;
  g_autofree gchar *raw_file = NULL;
  g_autofree gchar *probe_template_file = NULL;
  g_autofree gchar *enhanced_file = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) calibration = NULL;
  g_autoptr(GBytes) gallery_bytes = NULL;
  g_autoptr(GBytes) packed_probe = NULL;
  g_autoptr(GBytes) runtime_packed_probe = NULL;
  g_autoptr(GVariant) print_data = NULL;
  g_autoptr(GVariant) updated_print_data = NULL;
  g_autoptr(GBytes) updated_packed = NULL;
  g_autoptr(GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr(GoodixChicagoEnrollment) gallery = NULL;
  g_autoptr(GoodixChicagoEnrollment) updated_gallery = NULL;
  g_autoptr(GoodixChicagoEnrollment) probe = NULL;
  g_autoptr(GoodixChicagoRuntimeProbe) runtime_probe = NULL;
  guint16 raw_base[GOODIX_CHICAGO_PIXELS];
  guint16 raw_frame[GOODIX_CHICAGO_PIXELS];
  guint8 enhanced[GOODIX_CHICAGO_PIXELS];
  GoodixChicagoFeatureRecord records[GOODIX_CHICAGO_FEATURE_RECORD_LIMIT];
  GoodixChicagoFeatureConsensus consensus;
  GoodixChicagoFeatureLiveAuxiliary expected_live_auxiliary;
  GoodixChicagoMetricData metric_data;
  GoodixChicagoEnrollmentResult insert_result;
  GoodixChicagoMatchTemplateResult runtime_match_result;
  GoodixChicagoSubtemplateView probe_view;
  const GoodixChicagoSubtemplateView *runtime_probe_view;
  const guint8 *packed_probe_data;
  guint8 quality;
  guint8 coverage;
  guint active_count;
  guint record_count;
  guint resolution_peak_state;
  guint resolution_code;
  guint8 resolution_labels[GOODIX_CHICAGO_PIXELS];
  GoodixChicagoRuntimeReject runtime_reject;
  gint32 expected;
  gint32 score;
  gint32 runtime_score;
  gboolean check_expected;
  gsize calibration_size;
  gsize base_size;
  gsize gallery_size;
  gsize raw_size;
  gsize probe_template_size;
  gsize packed_probe_size;
  gsize enhanced_size;

  if (!case_name || !calibration_path || !base_path || !gallery_path ||
      !raw_path)
    {
      g_test_skip ("Chicago raw offline score was not requested");
      return;
    }
  check_expected = expected_value != NULL ||
    g_str_equal (case_name, "self") || g_str_equal (case_name, "9") ||
    g_str_equal (case_name, "17");
  expected = expected_value ? (gint32) g_ascii_strtoll (
    expected_value, NULL, 10) :
    g_str_equal (case_name, "self") ? 100 :
    g_str_equal (case_name, "9") ? -7 : -4;
  g_assert_true (g_file_get_contents (calibration_path, &calibration_file,
                                      &calibration_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (calibration_size, ==,
                    GOODIX_CHICAGO_CALIBRATION_FILE_LEN);
  calibration = goodix_chicago_calibration_load (
    calibration_path, (const guint8 *) calibration_file, &error);
  g_assert_no_error (error);
  g_assert_nonnull (calibration);
  g_assert_true (g_file_get_contents (base_path, &base_file, &base_size,
                                      &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (raw_path, &raw_file, &raw_size,
                                      &error));
  g_assert_no_error (error);
  g_assert_cmpuint (base_size, ==, sizeof (raw_base));
  g_assert_cmpuint (raw_size, ==, sizeof (raw_frame));
  for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
    {
      memcpy (&raw_base[pixel], base_file + pixel * 2, 2);
      raw_base[pixel] = GUINT16_FROM_LE (raw_base[pixel]);
      memcpy (&raw_frame[pixel], raw_file + pixel * 2, 2);
      raw_frame[pixel] = GUINT16_FROM_LE (raw_frame[pixel]);
    }
  preprocessor = goodix_chicago_preprocessor_new (
    calibration, raw_base, &error);
  g_assert_no_error (error);
  g_assert_nonnull (preprocessor);
  if (g_getenv ("CHICAGO_MATCH_RAW_DUMP_GAIN"))
    {
      guint16 gain[GOODIX_CHICAGO_PIXELS];

      for (guint pixel = 0; pixel < GOODIX_CHICAGO_PIXELS; pixel++)
        {
          g_assert_true (goodix_chicago_preprocessor_get_normalized_gain (
            preprocessor, pixel, &gain[pixel]));
          gain[pixel] = GUINT16_TO_LE (gain[pixel]);
        }
      g_assert_true (g_file_set_contents (
        g_getenv ("CHICAGO_MATCH_RAW_DUMP_GAIN"), (const gchar *) gain,
        sizeof (gain), &error));
      g_assert_no_error (error);
    }
  goodix_chicago_preprocessor_build_enhanced (
    preprocessor, raw_frame, enhanced);
  if (enhanced_path)
    {
      g_assert_true (g_file_get_contents (enhanced_path, &enhanced_file,
                                          &enhanced_size, &error));
      g_assert_no_error (error);
      g_assert_cmpmem (enhanced, sizeof (enhanced),
                       enhanced_file, enhanced_size);
    }
  goodix_chicago_preprocessor_finalize_metrics (
    goodix_chicago_preprocessor_compute_base_quality_from_enhanced (enhanced),
    goodix_chicago_preprocessor_compute_coverage (enhanced),
    &quality, &coverage);
  memset (records, 0, sizeof (records));
  record_count = goodix_chicago_feature_extract_subtemplate_full (
    enhanced, records, G_N_ELEMENTS (records), &active_count, &consensus);
  goodix_chicago_enrollment_build_metric_data (enhanced, &metric_data);
  probe = goodix_chicago_enrollment_new ();
  g_assert_true (goodix_chicago_enrollment_insert_first (
                   probe, records, record_count, active_count,
                   quality, coverage, &metric_data, &insert_result, &error));
  g_assert_no_error (error);
  g_assert_true (goodix_chicago_enrollment_get_subtemplate (
                   probe, 0, &probe_view));
  packed_probe = goodix_chicago_enrollment_pack (probe, &error);
  g_assert_no_error (error);
  g_assert_nonnull (packed_probe);
  if (g_getenv ("CHICAGO_MATCH_RAW_DUMP_PROBE"))
    {
      packed_probe_data = g_bytes_get_data (packed_probe, &packed_probe_size);
      g_assert_true (g_file_set_contents (
        g_getenv ("CHICAGO_MATCH_RAW_DUMP_PROBE"),
        (const gchar *) packed_probe_data, packed_probe_size, &error));
      g_assert_no_error (error);
    }
  if (probe_template_path)
    {
      g_assert_true (g_file_get_contents (probe_template_path,
                                          &probe_template_file,
                                          &probe_template_size, &error));
      g_assert_no_error (error);
      packed_probe_data = g_bytes_get_data (packed_probe, &packed_probe_size);
      g_assert_cmpmem (packed_probe_data, packed_probe_size,
                       probe_template_file, probe_template_size);
    }
  g_assert_true (g_file_get_contents (gallery_path, &gallery_file,
                                      &gallery_size, &error));
  g_assert_no_error (error);
  gallery = goodix_chicago_enrollment_unpack (
    (const guint8 *) gallery_file, gallery_size, &error);
  g_assert_no_error (error);
  g_assert_nonnull (gallery);
  score = goodix_chicago_match_score_template_type24 (
    gallery, &probe_view, 207);
  goodix_chicago_preprocessor_build_resolution_map_full (
    preprocessor, raw_frame, resolution_labels, &resolution_peak_state);
  goodix_chicago_preprocessor_classify_resolution_labels (
    0x18, resolution_labels, GOODIX_CHICAGO_PIXELS,
    GOODIX_CHICAGO_PIXELS, &resolution_code, NULL);
  goodix_chicago_feature_build_live_auxiliary (
    resolution_peak_state,
    goodix_chicago_preprocessor_pack_resolution_code (resolution_code),
    &expected_live_auxiliary);
  gallery_bytes = g_bytes_new (gallery_file, gallery_size);
  print_data = g_variant_ref_sink (goodix_chicago_print_data_build (
    (const guint8 *) calibration_file, calibration, gallery_bytes));
  runtime_probe = goodix_chicago_runtime_prepare_probe (
    preprocessor, raw_frame, &runtime_reject, &error);
  g_assert_no_error (error);
  g_assert_cmpint (runtime_reject, ==,
                   GOODIX_CHICAGO_RUNTIME_REJECT_NONE);
  g_assert_nonnull (runtime_probe);
  runtime_probe_view = goodix_chicago_runtime_probe_get_view (runtime_probe);
  g_assert_nonnull (runtime_probe_view);
  g_assert_cmpuint (runtime_probe_view->density_positive_percent, ==,
                    consensus.positive_percent);
  g_assert_cmpuint (runtime_probe_view->density_class, ==,
                    consensus.density_class);
  g_assert_cmpuint (runtime_probe_view->density_inactive_count, ==,
                    consensus.inactive_count);
  g_assert_cmpmem (runtime_probe_view->live_auxiliary.values,
                   sizeof (runtime_probe_view->live_auxiliary.values),
                   expected_live_auxiliary.values,
                   sizeof (expected_live_auxiliary.values));
  runtime_packed_probe = goodix_chicago_runtime_probe_pack (
    runtime_probe, &error);
  g_assert_no_error (error);
  g_assert_nonnull (runtime_packed_probe);
  g_assert_true (g_bytes_equal (runtime_packed_probe, packed_probe));
  g_assert_true (goodix_chicago_runtime_match_print_data (
    runtime_probe, print_data, (const guint8 *) calibration_file,
    calibration, &runtime_match_result, &error));
  g_assert_no_error (error);
  runtime_score = runtime_match_result.score;
  g_assert_cmpint (runtime_score, ==, score);
  g_assert_cmpint (runtime_match_result.study_eligibility_known, ==,
                   runtime_score > 0);
  g_assert_true (goodix_chicago_runtime_study_print_data (
    runtime_probe, print_data, (const guint8 *) calibration_file,
    calibration, &runtime_match_result, &updated_print_data, &error));
  g_assert_no_error (error);
  if (runtime_match_result.study_eligibility_known &&
      runtime_match_result.study_eligible &&
      goodix_chicago_enrollment_get_count (gallery) <
        goodix_chicago_enrollment_get_capacity (gallery))
    {
      const guint8 *updated_packed_data;
      gsize updated_packed_size;

      g_assert_nonnull (updated_print_data);
      g_assert_true (goodix_chicago_print_data_parse (
        updated_print_data, (const guint8 *) calibration_file,
        calibration, &updated_packed, &error));
      g_assert_no_error (error);
      updated_packed_data = g_bytes_get_data (updated_packed,
                                              &updated_packed_size);
      updated_gallery = goodix_chicago_enrollment_unpack (
        updated_packed_data, updated_packed_size, &error);
      g_assert_no_error (error);
      g_assert_nonnull (updated_gallery);
      g_assert_cmpuint (goodix_chicago_enrollment_get_count (updated_gallery),
                        ==,
                        goodix_chicago_enrollment_get_count (gallery) + 1);
    }
  else
    {
      g_assert_null (updated_print_data);
    }
  g_test_message ("raw case=%s records=%u/%u quality=%u/%u native=%d%s%d",
                  case_name, record_count, active_count, quality, coverage,
                  score, check_expected ? " official=" : " unverified=",
                  check_expected ? expected : score);
  if (check_expected)
    g_assert_cmpint (score, ==, expected);
}

static void
test_resolution_evidence_decoders (void)
{
  static const gint32 gallery_primary[4] = { 0, 2, 2, 3 };
  static const gint32 gallery_secondary[8] = { 0, 1, 2, 4, 5, 5, 0, 0 };

  for (guint high = 0; high < 8; high++)
    for (guint low = 0; low < 4; low++)
      {
        GoodixChicagoMatchResolutionEvidence evidence;
        guint32 packed = 0xf0f00000u | (high << 8) | low;

        goodix_chicago_match_decode_probe_resolution_evidence (
          packed, &evidence);
        g_assert_cmpint (evidence.primary, ==, low);
        g_assert_cmpint (evidence.secondary, ==, high == 0 ? 0 : high + 4);

        goodix_chicago_match_decode_gallery_resolution_evidence (
          packed, &evidence);
        g_assert_cmpint (evidence.primary, ==, gallery_primary[low]);
        g_assert_cmpint (evidence.secondary, ==, gallery_secondary[high]);
      }
}

static void
test_late_rejection_type24 (void)
{
  static const gint32 baseline_values[0x68 / 4] = {
    31, 31, 0, 0, 246, 253, 254, 250, 254, 137, 100, 32, 0,
    0, 0, 256, 0, 0, 0, 256, 0, 89, 100, 89, 100, 0,
  };
  static const struct
  {
    gint32 transform[6];
    gint32 record[0x68 / 4];
    gint32 probe_quality;
    gint32 initial_flag;
    gint32 expected_flag;
  } real_vectors[] = {
    {
      { 256, 0, 0, 0, 256, 0 },
      { 31, 31, 0, 0, 246, 253, 254, 250, 254, 137, 100, 32,
        0, 0, 0, 256, 0, 0, 0, 256, 0, 89, 100, 89, 100, 0 },
      89, 1, 1,
    },
    {
      { 259, 2, -6391, -5, 249, -10719 },
      { 4, 7, 7, 0, 220, 218, 227, 170, 218, 29, 88, 77,
        0, 0, 0, 259, 2, -6391, -5, 249, -10719, 88, 100, 89, 100, 1 },
      88, 0, 0,
    },
    {
      { 256, 0, 0, 0, 256, 0 },
      { 31, 31, 0, 0, 247, 254, 254, 250, 254, 137, 100, 35,
        0, 0, 0, 256, 0, 0, 0, 256, 0, 88, 100, 88, 100, 0 },
      88, 1, 1,
    },
    {
      { 251, 10, -9189, -5, 252, -2363 },
      { 28, 28, 0, 0, 232, 206, 227, 162, 221, 65, 88, 62,
        0, 0, 0, 251, 10, -9189, -5, 252, -2363, 85, 100, 89, 100, 1 },
      85, 2, 2,
    },
  };
  static const gint32 identity[6] = { 256, 0, 0, 0, 256, 0 };

  for (gint32 value = 0; value <= 260; value += 10)
    {
      GoodixChicagoMatchScoreRecord record;
      gint32 rejection_count = 0;
      gint32 flag = 1;

      memcpy (&record, baseline_values, sizeof (record));
      record.agreement = value;
      g_assert_cmpint (goodix_chicago_match_late_rejection_type24 (
                         80, 64, 89, &record, identity, 2, 0, 4,
                         &rejection_count, &flag), ==, value < 160);
      g_assert_cmpint (rejection_count, ==, value < 160);
      g_assert_cmpint (flag, ==, 1);

      memcpy (&record, baseline_values, sizeof (record));
      record.study_metric_20 = value;
      rejection_count = 0;
      flag = 1;
      g_assert_cmpint (goodix_chicago_match_late_rejection_type24 (
                         80, 64, 89, &record, identity, 2, 0, 4,
                         &rejection_count, &flag), ==, value < 160);
      g_assert_cmpint (rejection_count, ==, value < 160);
      g_assert_cmpint (flag, ==, 1);

      memcpy (&record, baseline_values, sizeof (record));
      record.agreement = value;
      record.study_metric_20 = value;
      rejection_count = 0;
      flag = 1;
      g_assert_cmpint (goodix_chicago_match_late_rejection_type24 (
                         80, 64, 89, &record, identity, 2, 0, 4,
                         &rejection_count, &flag), ==, value < 210);
      g_assert_cmpint (rejection_count, ==, value < 210);
      g_assert_cmpint (flag, ==, 1);
    }

  for (gint32 value = 0; value <= 100; value += 5)
    {
      GoodixChicagoMatchScoreRecord record;
      gint32 rejection_count = 0;
      gint32 flag = 1;

      memcpy (&record, baseline_values, sizeof (record));
      g_assert_false (goodix_chicago_match_late_rejection_type24 (
        80, 64, value, &record, identity, 2, 0, 4,
        &rejection_count, &flag));
      g_assert_cmpint (rejection_count, ==, 0);
      g_assert_cmpint (flag, ==, 1);
    }

  for (gint32 value = 0; value <= 30; value++)
    {
      GoodixChicagoMatchScoreRecord record;
      gint32 rejection_count = 0;
      gint32 flag = 1;

      memcpy (&record, baseline_values, sizeof (record));
      record.geometry_count = value;
      record.agreement = 217;
      record.geometry_percent = 25;
      g_assert_false (goodix_chicago_match_late_rejection_type24 (
        80, 64, 89, &record, identity, 2, 0, 4,
        &rejection_count, &flag));
      g_assert_cmpint (rejection_count, ==, 0);
      g_assert_cmpint (flag, ==, value < 18 ? 0 : 1);
    }

  {
    GoodixChicagoMatchScoreRecord record;
    gint32 rejection_count = 5;
    gint32 flag = 1;

    memcpy (&record, baseline_values, sizeof (record));
    record.agreement = 150;
    g_assert_true (goodix_chicago_match_late_rejection_type24 (
      80, 64, 89, &record, identity, 2, 0, 4,
      &rejection_count, &flag));
    g_assert_cmpint (rejection_count, ==, 6);
    g_assert_cmpint (flag, ==, 1);
  }

  /* A high auxiliary count with a low candidate count follows a separate
   * official gate. This vector caught that branch when the shared oracle was
   * expanded beyond the original type-24 sample set. */
  {
    static const gint32 transform[6] = {
      284, -24, -2386, -5, 282, -580,
    };
    static const gint32 values[0x68 / 4] = {
      33, 30, 0, 0, 157, 124, 162, 242, 108, 120, 80, 66, 0,
      1, 1, 284, -24, -2386, -5, 282, -580, 40, 94, 66, 89, 0,
    };
    gint32 rejection_count = 7;
    gint32 flag = 1;

    g_assert_true (goodix_chicago_match_late_rejection_type24 (
      80, 64, 62, (const GoodixChicagoMatchScoreRecord *) values,
      transform, 0, 0, 5, &rejection_count, &flag));
    g_assert_cmpint (rejection_count, ==, 8);
    g_assert_cmpint (flag, ==, 1);
  }

  for (gint axis = 0; axis < 3; axis++)
    for (gint32 value = 0; value <= 8; value++)
      {
        GoodixChicagoMatchScoreRecord record;
        gint32 state[3] = { 2, 0, 4 };
        gint32 rejection_count = 0;
        gint32 flag = 1;

        memcpy (&record, baseline_values, sizeof (record));
        state[axis] = value;
        g_assert_false (goodix_chicago_match_late_rejection_type24 (
          80, 64, 89, &record, identity, state[0], state[1], state[2],
          &rejection_count, &flag));
        g_assert_cmpint (rejection_count, ==, 0);
        g_assert_cmpint (flag, ==, 1);
      }

  for (guint index = 0; index < G_N_ELEMENTS (real_vectors); index++)
    {
      gint32 rejection_count = 0;
      gint32 flag = real_vectors[index].initial_flag;

      g_assert_false (goodix_chicago_match_late_rejection_type24 (
        80, 64, real_vectors[index].probe_quality,
        (const GoodixChicagoMatchScoreRecord *) real_vectors[index].record,
        real_vectors[index].transform, 2, 0, 4,
        &rejection_count, &flag));
      g_assert_cmpint (rejection_count, ==, 0);
      g_assert_cmpint (flag, ==, real_vectors[index].expected_flag);
    }
}

static void
test_late_rejection_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_REJECTION_VECTOR");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  const RejectionCorpusHeader *header;
  const RejectionCorpusVector *vectors;
  gsize size;

  if (!path)
    {
      g_test_skip ("Chicago late-rejection oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (size, >=, sizeof (*header));
  header = (const RejectionCorpusHeader *) contents;
  g_assert_cmphex (header->magic, ==, 0x34523243u);
  g_assert_cmpuint (header->version, ==, 2);
  g_assert_cmpuint (header->reserved, ==, 0);
  g_assert_cmpuint (size, ==, sizeof (*header) +
                    (gsize) header->count * sizeof (*vectors));
  vectors = (const RejectionCorpusVector *) (header + 1);
  for (guint index = 0; index < header->count; index++)
    {
      gint32 rejection_count = vectors[index].rejection_count_in;
      gint32 flag = vectors[index].flag_in;
      gboolean rejected;

      rejected = goodix_chicago_match_late_rejection (
        vectors[index].template_type,
        vectors[index].width, vectors[index].height,
        vectors[index].probe_quality,
        (const GoodixChicagoMatchScoreRecord *) vectors[index].record,
        vectors[index].transform, vectors[index].state[0],
        vectors[index].state[1], vectors[index].state[2],
        &rejection_count, &flag);
      if (rejected != vectors[index].rejected ||
          rejection_count != vectors[index].rejection_count_out ||
          flag != vectors[index].flag_out)
        g_error ("late-rejection oracle mismatch at vector %u, type %d: "
                 "rejected %d/%d, count %d/%d, flag %d/%d",
                 index, vectors[index].template_type,
                 rejected, vectors[index].rejected,
                 rejection_count, vectors[index].rejection_count_out,
                 flag, vectors[index].flag_out);
    }
}

static void
test_transform_overlap_area_type24 (void)
{
  static const struct
  {
    gint32 transform[6];
    gint32 inverse[6];
    gint32 forward;
    gint32 reverse;
    gint32 maximum;
  } vectors[] = {
    { { 256, 0, 0, 0, 256, 0 },
      { 256, 0, 0, 0, 256, 0 }, 5120, 5120, 5120 },
    { { 259, 2, -6391, -5, 249, -10719 },
      { 252, -2, 6230, 5, 263, 11145 }, 1068, 1058, 1068 },
    { { 251, 10, -9189, -5, 252, -2363 },
      { 260, -10, 9269, 5, 259, 2584 }, 2350, 2284, 2350 },
    { { 256, 0, 256, 0, 256, 0 },
      { 256, 0, -256, 0, 256, 0 }, 5056, 5056, 5056 },
    { { 256, 0, -256, 0, 256, 0 },
      { 256, 0, 256, 0, 256, 0 }, 5056, 5056, 5056 },
    { { 256, 0, 0, 0, 256, 256 },
      { 256, 0, 0, 0, 256, -256 }, 5040, 5040, 5040 },
    { { 256, 0, 0, 0, 256, -256 },
      { 256, 0, 0, 0, 256, 256 }, 5040, 5040, 5040 },
    { { 240, 32, 0, -32, 240, 0 },
      { 268, -35, 0, 35, 268, 0 }, 4611, 4113, 4611 },
    { { 272, -24, 1024, 24, 272, -768 },
      { 239, 21, -893, -21, 239, 801 }, 4279, 4919, 4919 },
    { { 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0 }, 5120, 5120, 5120 },
  };

  for (guint index = 0; index < G_N_ELEMENTS (vectors); index++)
    {
      gint32 inverse[6];

      goodix_chicago_match_invert_transform_q8_type24 (
        vectors[index].transform, inverse);
      g_assert_cmpmem (inverse, sizeof (inverse),
                       vectors[index].inverse, sizeof (vectors[index].inverse));
      g_assert_cmpint (
        goodix_chicago_match_transform_overlap_area_type24 (
          80, 64, vectors[index].transform), ==, vectors[index].forward);
      g_assert_cmpint (
        goodix_chicago_match_transform_overlap_area_type24 (
          80, 64, inverse), ==, vectors[index].reverse);
      g_assert_cmpint (
        goodix_chicago_match_bidirectional_overlap_area_type24 (
          80, 64, vectors[index].transform), ==, vectors[index].maximum);
    }
}

static void
test_scheduler_auxiliary_type24 (void)
{
  GoodixChicagoMatchSchedulerAuxiliary state;

  goodix_chicago_match_scheduler_auxiliary_init_type24 (0x200, &state);
  g_assert_cmpint (state.auxiliary_count, ==, 2);
  g_assert_false (state.raised);
  g_assert_cmpint (
    goodix_chicago_match_scheduler_auxiliary_consume_type24 (
      &state, 0x200, 0), ==, 4);
  g_assert_cmpint (state.auxiliary_count, ==, 2);
  g_assert_cmpint (
    goodix_chicago_match_scheduler_auxiliary_consume_type24 (
      &state, 0x300, 0), ==, 5);
  g_assert_cmpint (state.auxiliary_count, ==, 4);
  g_assert_cmpint (
    goodix_chicago_match_scheduler_auxiliary_consume_type24 (
      &state, 0x500, 0), ==, 5);
  g_assert_cmpint (state.auxiliary_count, ==, 5);

  goodix_chicago_match_scheduler_auxiliary_init_type24 (0, &state);
  g_assert_cmpint (
    goodix_chicago_match_scheduler_auxiliary_consume_type24 (
      &state, 0, 6), ==, 0);
  g_assert_cmpint (state.auxiliary_count, ==, 1);
  g_assert_true (state.raised);
  g_assert_cmpint (
    goodix_chicago_match_scheduler_auxiliary_consume_type24 (
      &state, 0, 6), ==, 1);
  g_assert_cmpint (state.auxiliary_count, ==, 1);
}

static void
test_study_metric_offline_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_STUDY_TEMPLATE");
  const gchar *expected = g_getenv ("CHICAGO_MATCH_STUDY_EXPECTED");
  const gchar *gallery_value =
    g_getenv ("CHICAGO_MATCH_STUDY_GALLERY_INDEX");
  const gchar *probe_value = g_getenv ("CHICAGO_MATCH_STUDY_PROBE_INDEX");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GoodixChicagoEnrollment) enrollment = NULL;
  GoodixChicagoSubtemplateView gallery;
  GoodixChicagoSubtemplateView probe;
  GoodixChicagoMatchScoreRecord record;
  GoodixChicagoMatchGeometry geometry;
  gint expected_18;
  gint expected_1c;
  gint expected_20;
  guint gallery_index = gallery_value ?
    (guint) g_ascii_strtoull (gallery_value, NULL, 0) : 0;
  guint probe_index = probe_value ?
    (guint) g_ascii_strtoull (probe_value, NULL, 0) : 0;
  gsize size;

  if (!path || !expected)
    {
      g_test_skip ("Chicago study-metric oracle was not requested");
      return;
    }
  g_assert_cmpint (sscanf (expected, "%d,%d,%d", &expected_18,
                           &expected_1c, &expected_20), ==, 3);
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  enrollment = goodix_chicago_enrollment_unpack (
    (const guint8 *) contents, size, &error);
  g_assert_no_error (error);
  g_assert_nonnull (enrollment);
  g_assert_true (goodix_chicago_enrollment_get_subtemplate (
    enrollment, gallery_index, &gallery));
  g_assert_true (goodix_chicago_enrollment_get_subtemplate (
    enrollment, probe_index, &probe));
  goodix_chicago_match_score_subtemplate_type24 (
    &gallery, &probe, &record, &geometry);
  g_test_message ("study metric native=%d,%d,%d official=%d,%d,%d",
                  record.study_metric_18, record.study_metric_1c,
                  record.study_metric_20, expected_18, expected_1c,
                  expected_20);
  g_assert_cmpint (record.study_metric_18, ==, expected_18);
  g_assert_cmpint (record.study_metric_1c, ==, expected_1c);
  g_assert_cmpint (record.study_metric_20, ==, expected_20);
}

static void
test_study_aggregate_q8_type24 (void)
{
  g_assert_cmpint (
    goodix_chicago_match_study_aggregate_q8_type24 (0, 0, 0), ==, 0);
  g_assert_cmpint (
    goodix_chicago_match_study_aggregate_q8_type24 (1, 0, 0), ==, 128);
  g_assert_cmpint (
    goodix_chicago_match_study_aggregate_q8_type24 (0, 0, 1), ==, 128);
  g_assert_cmpint (
    goodix_chicago_match_study_aggregate_q8_type24 (1, 1, 1), ==, 128);
  g_assert_cmpint (
    goodix_chicago_match_study_aggregate_q8_type24 (3, 0, 1), ==, 204);
  g_assert_cmpint (
    goodix_chicago_match_study_aggregate_q8_type24 (400, 480, 400), ==,
    159);
}

static void
test_study_aggregate_q8_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_STUDY_AGGREGATE_VECTOR");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  const StudyAggregateCorpusHeader *header;
  const StudyAggregateCorpusVector *vectors;
  gsize size;

  if (!path)
    {
      g_test_skip ("Chicago study-aggregate oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (size, >=, sizeof (*header));
  header = (const StudyAggregateCorpusHeader *) contents;
  g_assert_cmphex (header->magic, ==, 0x38414743);
  g_assert_cmpuint (header->version, ==, 1);
  g_assert_cmpuint (size, ==, sizeof (*header) +
                    header->count * sizeof (*vectors));
  vectors = (const StudyAggregateCorpusVector *) (header + 1);
  for (guint index = 0; index < header->count; index++)
    g_assert_cmpint (
      goodix_chicago_match_study_aggregate_q8_type24 (
        vectors[index].count_zero, vectors[index].count_mixed,
        vectors[index].count_one), ==, vectors[index].aggregate);
}

static void
test_candidate_prefilter_type24_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_PREFILTER_VECTOR");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  const CandidatePrefilterCorpusHeader *header;
  const CandidatePrefilterCorpusVector *vectors;
  gsize size;

  if (!path)
    {
      g_test_skip ("Chicago candidate-prefilter oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (size, >=, sizeof (*header));
  header = (const CandidatePrefilterCorpusHeader *) contents;
  g_assert_cmphex (header->magic, ==, 0x46504334);
  g_assert_cmpuint (header->version, ==, 1);
  g_assert_cmpuint (size, ==, sizeof (*header) +
                    header->count * sizeof (*vectors));
  vectors = (const CandidatePrefilterCorpusVector *) (header + 1);
  for (guint index = 0; index < header->count; index++)
    g_assert_cmpint (
      goodix_chicago_match_candidate_prefilter_type24 (
        (const GoodixChicagoMatchScoreRecord *) vectors[index].record,
        vectors[index].auxiliary_count,
        vectors[index].candidate_metric), ==, vectors[index].rejected != 0);
}

static void
test_study_admission_type24 (void)
{
  GoodixChicagoMatchScoreRecord strong_record = {
    .geometry_count = 20,
    .agreement = 230,
    .study_metric_1c = 200,
    .study_metric_20 = 230,
    .normalized_coverage = 200,
    .matched_percent = 80,
    .geometry_percent = 100,
  };
  GoodixChicagoMatchStudyAdmissionInput input = {
    .auxiliary_count = 4,
    .candidate_metric = 1,
    .probe_quality = 80,
  };
  GoodixChicagoMatchScoreRecord record;
  gint32 status;
  gint32 study;

  record = (GoodixChicagoMatchScoreRecord) {
    .geometry_count = 10,
  };
  input.auxiliary_count = 1;
  input.candidate_metric = 0;
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &record, &input, &status, &study);
  g_assert_cmpint (status, ==, 1);
  g_assert_cmpint (study, ==, 1);

  record = (GoodixChicagoMatchScoreRecord) {
    .geometry_count = 9,
    .agreement = 210,
    .study_metric_20 = 194,
    .normalized_coverage = 179,
    .matched_percent = 39,
    .geometry_percent = 19,
  };
  input = (GoodixChicagoMatchStudyAdmissionInput) {
    .auxiliary_count = 1,
    .candidate_metric = 0,
  };
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &record, &input, &status, &study);
  g_assert_cmpint (status, ==, 1);
  g_assert_cmpint (study, ==, 0);

  record = (GoodixChicagoMatchScoreRecord) {
    .geometry_count = 9,
    .agreement = 218,
    .study_metric_20 = 204,
    .normalized_coverage = 79,
    .matched_percent = 69,
    .geometry_percent = 39,
  };
  input = (GoodixChicagoMatchStudyAdmissionInput) {
    .candidate_metric = 0,
    .probe_auxiliary = 1,
  };
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &record, &input, &status, &study);
  g_assert_cmpint (status, ==, 1);
  g_assert_cmpint (study, ==, 0);

  record = (GoodixChicagoMatchScoreRecord) {
    .normalized_coverage = 100,
    .matched_percent = 46,
    .geometry_percent = 10,
  };
  input = (GoodixChicagoMatchStudyAdmissionInput) {
    .auxiliary_count = 5,
    .candidate_metric = 1,
    .probe_quality = 57,
  };
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &record, &input, &status, &study);
  g_assert_cmpint (status, ==, 0);
  g_assert_cmpint (study, ==, 0);

  record = (GoodixChicagoMatchScoreRecord) {
    .geometry_count = 18,
    .agreement = 200,
    .study_metric_20 = 199,
    .normalized_coverage = 128,
    .matched_percent = 60,
    .geometry_percent = 28,
  };
  input = (GoodixChicagoMatchStudyAdmissionInput) {
    .auxiliary_count = 3,
    .candidate_metric = 4,
    .probe_quality = 70,
  };
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &record, &input, &status, &study);
  g_assert_cmpint (status, ==, 0);
  g_assert_cmpint (study, ==, 0);

  input = (GoodixChicagoMatchStudyAdmissionInput) {
    .auxiliary_count = 4,
    .candidate_metric = 1,
    .probe_quality = 49,
    .aggregate_metric = 220,
  };
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &strong_record, &input, &status, &study);
  g_assert_cmpint (status, ==, 1);
  g_assert_cmpint (study, ==, 0);

  record = strong_record;
  record.geometry_count = 7;
  record.geometry_percent = 24;
  record.study_metric_20 = 220;
  input = (GoodixChicagoMatchStudyAdmissionInput) {
    .auxiliary_count = 4,
    .candidate_metric = 1,
    .probe_quality = 80,
  };
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &record, &input, &status, &study);
  g_assert_cmpint (status, ==, 1);
  g_assert_cmpint (study, ==, 0);

  record = strong_record;
  record.geometry_count = 5;
  record.study_metric_1c = 134;
  record.study_metric_20 = 200;
  input = (GoodixChicagoMatchStudyAdmissionInput) {
    .auxiliary_count = 5,
    .candidate_metric = 1,
    .probe_quality = 80,
  };
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &record, &input, &status, &study);
  g_assert_cmpint (status, ==, 0);
  g_assert_cmpint (study, ==, 0);

  input = (GoodixChicagoMatchStudyAdmissionInput) {
    .auxiliary_count = 4,
    .candidate_metric = 1,
    .probe_quality = 80,
  };
  status = 1;
  study = 1;
  goodix_chicago_match_filter_study_admission_type24 (
    &strong_record, &input, &status, &study);
  g_assert_cmpint (status, ==, 1);
  g_assert_cmpint (study, ==, 1);
}

static void
test_score_aggregation_official (void)
{
  static const gint32 heldout9_geometry[8] = { 0, 0, 1, 3, 0, 0, 5, 4 };
  static const gboolean heldout9_mask[8] = {
    FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE,
  };
  static const gint32 heldout17_geometry[8] = { 0, 4, 0, 0, 0, 5, 0, 0 };
  static const gboolean heldout17_mask[8] = {
    FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE,
  };
  GoodixChicagoMatchScoreRecord self_record = {
    .geometry_count = 31,
    .secondary_geometry_count = 31,
    .selector = 276,
    .agreement = 254,
    .normalized_coverage = 63,
    .matched_percent = 100,
    .geometry_percent = 106,
  };
  GoodixChicagoMatchScoreRecord heldout9_record = {
    .geometry_count = 5,
    .secondary_geometry_count = 5,
    .selector = 128,
    .agreement = 227,
    .normalized_coverage = 49,
    .matched_percent = 37,
    .geometry_percent = 17,
  };
  GoodixChicagoMatchScoreRecord heldout17_record = {
    .geometry_count = 5,
    .secondary_geometry_count = 5,
    .selector = 128,
    .agreement = 224,
    .normalized_coverage = 49,
    .matched_percent = 68,
    .geometry_percent = 26,
  };
  GoodixChicagoMatchSchedulerEvidence self_evidence;
  GoodixChicagoMatchSchedulerEvidence heldout9_evidence;
  GoodixChicagoMatchSchedulerEvidence heldout17_evidence;
  GoodixChicagoMatchAggregation self = { 0, };
  GoodixChicagoMatchAggregation heldout9 = { 0, };
  GoodixChicagoMatchAggregation heldout17 = { 0, };
  GoodixChicagoMatchAggregation capped = {
    .fallback_score = 117,
  };

  goodix_chicago_match_scheduler_evidence_type24 (
    &self_record, 92, 100, TRUE, &self_evidence);
  goodix_chicago_match_scheduler_evidence_type24 (
    &heldout9_record, 57, 100, TRUE, &heldout9_evidence);
  goodix_chicago_match_scheduler_evidence_type24 (
    &heldout17_record, 75, 100, TRUE, &heldout17_evidence);
  g_assert_cmpint (self_evidence.confidence, ==, 2);
  g_assert_cmpint (self_evidence.status, ==, 1);
  g_assert_cmpint (self_evidence.special, ==, 0);
  g_assert_cmpint (heldout9_evidence.confidence, ==, 0);
  g_assert_cmpint (heldout9_evidence.status, ==, 0);
  g_assert_cmpint (heldout9_evidence.special, ==, 0);
  g_assert_cmpint (heldout17_evidence.confidence, ==, 0);
  g_assert_cmpint (heldout17_evidence.status, ==, 0);
  g_assert_cmpint (heldout17_evidence.special, ==, 0);
  g_assert_true (goodix_chicago_match_accept_type24 (
                   276, 254, self_evidence.status, 207));
  g_assert_false (goodix_chicago_match_accept_type24 (
                    128, 227, heldout9_evidence.status, 207));
  g_assert_false (goodix_chicago_match_accept_type24 (
                    128, 224, heldout17_evidence.status, 207));
  g_assert_true (goodix_chicago_match_accept_type24 (0, 0, 1, 207));
  g_assert_true (goodix_chicago_match_aggregation_consume_type24 (
                   &self, &self_record, 92, 100, 207));
  g_assert_false (goodix_chicago_match_aggregation_consume_type24 (
                    &self, &heldout9_record, 57, 100, 207));
  g_assert_false (goodix_chicago_match_aggregation_consume_type24 (
                    &self, &heldout17_record, 75, 100, 207));
  g_assert_false (goodix_chicago_match_aggregation_consume_fallback_type24 (
                    &heldout9, 8, 128, 225, 113, 207));
  g_assert_false (goodix_chicago_match_aggregation_consume_fallback_type24 (
                    &heldout17, 4, 0, 0, 0, 207));
  for (guint index = 0; index < 8; index++)
    {
      g_assert_cmpint (goodix_chicago_match_fallback_gallery_enabled_type24 (
                         heldout9_geometry[index], 0, 1), ==,
                       heldout9_mask[index]);
      g_assert_cmpint (goodix_chicago_match_fallback_gallery_enabled_type24 (
                         heldout17_geometry[index], 0, 1), ==,
                       heldout17_mask[index]);
    }
  g_assert_false (goodix_chicago_match_fallback_gallery_enabled_type24 (
                    31, 1, 1));
  g_assert_false (goodix_chicago_match_fallback_gallery_enabled_type24 (
                    31, 0, 0));
  g_assert_cmpint (goodix_chicago_match_aggregation_score (&self), ==, 100);
  g_assert_cmpint (goodix_chicago_match_aggregation_score (&heldout9), ==,
                   -7);
  g_assert_cmpint (goodix_chicago_match_aggregation_score (&heldout17), ==,
                   -4);
  g_assert_cmpint (goodix_chicago_match_aggregation_score (&capped), ==,
                   100);
}

static void
test_candidate_oracle (void)
{
  const gchar *old_path = g_getenv ("CHICAGO_MATCH_OLD_FEATURES");
  const gchar *new_path = g_getenv ("CHICAGO_MATCH_NEW_FEATURES");
  const gchar *vector_path = g_getenv ("CHICAGO_MATCH_CANDIDATE_VECTOR");
  g_autofree gchar *old_contents = NULL;
  g_autofree gchar *new_contents = NULL;
  g_autofree gchar *vector_contents = NULL;
  g_autofree GoodixChicagoMatchCandidate *candidates = NULL;
  g_autofree guint8 *distances = NULL;
  g_autofree guint8 *directions = NULL;
  g_autoptr(GError) error = NULL;
  const GoodixChicagoFeatureRecord *old_records;
  const GoodixChicagoFeatureRecord *new_records;
  const CandidateVectorHeader *header;
  const gint32 *official_best;
  const gint32 *official_indices;
  const guint8 *official_distances;
  const guint8 *official_directions;
  const GoodixChicagoMatchPair *official_pairs;
  g_autofree GoodixChicagoMatchPair *pairs = NULL;
  GoodixChicagoMatchCandidateConfig config;
  guint32 old_count;
  guint32 new_count;
  gsize vector_size;
  gsize matrix_size;

  if (!old_path || !new_path || !vector_path)
    {
      g_test_skip ("Chicago identify candidate oracle was not requested");
      return;
    }
  old_records = load_features (old_path, &old_contents, &old_count, &error);
  g_assert_no_error (error);
  new_records = load_features (new_path, &new_contents, &new_count, &error);
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (vector_path, &vector_contents,
                                      &vector_size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (vector_size, >=, sizeof (*header));
  header = (const CandidateVectorHeader *) vector_contents;
  g_assert_cmphex (header->magic, ==, 0x43444943);
  g_assert_cmpuint (header->version, ==, 2);
  g_assert_cmpuint (header->old_count, ==, old_count);
  g_assert_cmpuint (header->new_count, ==, new_count);
  matrix_size = old_count * GOODIX_CHICAGO_MATCH_MATRIX_STRIDE;
  g_assert_cmpuint (vector_size, ==,
                    sizeof (*header) + old_count * 4 * sizeof (gint32) +
                    matrix_size * 2 + header->selector_limit *
                    sizeof (GoodixChicagoMatchPair));
  official_best = (const gint32 *) (vector_contents + sizeof (*header));
  official_indices = official_best + old_count * 2;
  official_distances = (const guint8 *) (official_indices + old_count * 2);
  official_directions = official_distances + matrix_size;
  official_pairs = (const GoodixChicagoMatchPair *)
    (official_directions + matrix_size);
  config = (GoodixChicagoMatchCandidateConfig) {
    header->config[2], header->config[3],
    header->config[4], header->config[5],
    header->config[6], header->config[7],
  };
  candidates = g_new (GoodixChicagoMatchCandidate, old_count);
  distances = g_malloc (matrix_size);
  directions = g_malloc0 (matrix_size);
  pairs = g_new (GoodixChicagoMatchPair, header->selector_limit);
  memset (distances, 0xff, matrix_size);
  goodix_chicago_match_init_candidates (candidates, old_count);
  goodix_chicago_match_update_candidates (old_records, new_records,
                                             &config, candidates, distances,
                                             directions);
  for (guint index = 0; index < old_count; index++)
    {
      g_assert_cmpint (candidates[index].best_distance, ==,
                       official_best[index * 2]);
      g_assert_cmpint (candidates[index].second_distance, ==,
                       official_best[index * 2 + 1]);
      g_assert_cmpint (candidates[index].best_index, ==,
                       official_indices[index * 2]);
      g_assert_cmpint (candidates[index].second_index, ==,
                       official_indices[index * 2 + 1]);
    }
  g_assert_cmpmem (distances, matrix_size, official_distances, matrix_size);
  g_assert_cmpmem (directions, matrix_size, official_directions, matrix_size);
  g_assert_cmpuint (goodix_chicago_match_select_candidates (
                      new_records, candidates, old_count,
                      header->selector_limit, header->best_multiplier,
                      header->second_multiplier, pairs), ==,
                    header->selected_count);
  g_assert_cmpmem (pairs,
                   header->selector_limit * sizeof (*pairs),
                   official_pairs,
                   header->selector_limit * sizeof (*official_pairs));
}

static void
test_geometry_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_GEOMETRY_VECTOR");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  const GeometryVector *vector;
  GoodixChicagoMatchGeometry result;
  gsize size;
  guint official_inlier_count = 0;

  if (!path)
    {
      g_test_skip ("Chicago identify geometry oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (size, ==, sizeof (*vector));
  vector = (const GeometryVector *) contents;
  g_assert_cmphex (vector->magic, ==, 0x4f454743);
  g_assert_cmpuint (vector->version, ==, 1);
  g_assert_cmpuint (vector->count, <=,
                    GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT);
  goodix_chicago_match_estimate_geometry (
    vector->source, vector->target, vector->target_orientation,
    vector->source_orientation, vector->count, vector->mode,
    vector->strict, &result);
  for (guint index = 0; index < vector->count; index++)
    official_inlier_count += vector->inliers[index] != 0;
  g_assert_cmpuint (result.inlier_count, ==, official_inlier_count);
  g_assert_cmpint (result.error, ==, vector->error);
  g_assert_cmpmem (result.transform, sizeof (result.transform),
                   vector->transform, sizeof (vector->transform));
  g_assert_cmpmem (result.inliers, sizeof (result.inliers),
                   vector->inliers, sizeof (vector->inliers));
  g_assert_cmpuint (goodix_chicago_match_geometry_consensus (
                      vector->source, vector->target,
                      vector->target_orientation, vector->source_orientation,
                      vector->count, vector->mode, &result), ==,
                    official_inlier_count);
  g_assert_cmpint (result.error, ==, vector->error);
  g_assert_cmpmem (result.transform, sizeof (result.transform),
                   vector->transform, sizeof (vector->transform));
  g_assert_cmpmem (result.inliers, sizeof (result.inliers),
                   vector->inliers, sizeof (vector->inliers));
}

static void
test_orientation_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_ORIENTATION_VECTOR");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  const OrientationVector *vector;
  guint8 inliers[GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT];
  gsize size;
  guint official_count = 0;

  if (!path)
    {
      g_test_skip ("Chicago orientation-filter oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (size, ==, sizeof (*vector));
  vector = (const OrientationVector *) contents;
  g_assert_cmphex (vector->magic, ==, 0x524f4643);
  g_assert_cmpuint (vector->version, ==, 1);
  g_assert_cmpuint (vector->count, <=,
                    GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT);
  memcpy (inliers, vector->initial, sizeof (inliers));
  for (guint index = 0; index < vector->count; index++)
    official_count += vector->filtered[index] != 0;
  g_assert_cmpuint (goodix_chicago_match_filter_orientations (
                      vector->transform, vector->target_orientation,
                      vector->source_orientation, vector->count, inliers), ==,
                    official_count);
  g_assert_cmpmem (inliers, sizeof (inliers),
                   vector->filtered, sizeof (vector->filtered));
}

static void
test_refinement_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_REFINEMENT_VECTOR");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  const RefinementVector *vector;
  gint32 transform[6];
  gsize size;

  if (!path)
    {
      g_test_skip ("Chicago refinement oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (size, ==, sizeof (*vector));
  vector = (const RefinementVector *) contents;
  g_assert_cmphex (vector->magic, ==, 0x46455243);
  g_assert_cmpuint (vector->version, ==, 1);
  g_assert_cmpuint (vector->count, <=,
                    GOODIX_CHICAGO_MATCH_GEOMETRY_LIMIT);
  memcpy (transform, vector->initial_transform, sizeof (transform));
  goodix_chicago_match_refine_geometry (
    vector->source, vector->target, vector->inliers, vector->count,
    vector->error_limit, transform);
  g_assert_cmpmem (transform, sizeof (transform),
                   vector->refined_transform,
                   sizeof (vector->refined_transform));
}

static void
test_subscore_config_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_SUBSCORE_VECTOR");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  const SubscoreVector *vector;
  GoodixChicagoMatchScoreConfig config;
  gsize size;

  if (!path)
    {
      g_test_skip ("Chicago subscore oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (size, ==, sizeof (*vector));
  vector = (const SubscoreVector *) contents;
  g_assert_cmphex (vector->magic, ==, 0x42555343);
  g_assert_cmpuint (vector->version, ==, 1);
  goodix_chicago_match_init_score_config_type24 (&config);
  g_assert_cmpmem (&config, sizeof (config), vector->work, sizeof (config));
}

static void
test_feature_overlap_oracle (void)
{
  const gchar *path = g_getenv ("CHICAGO_MATCH_FEATURE_OVERLAP_VECTOR");
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  const FeatureOverlapVector *vector;
  GoodixChicagoMatchFeatureOverlap result;
  gsize size;

  if (!path)
    {
      g_test_skip ("Chicago feature-overlap oracle was not requested");
      return;
    }
  g_assert_true (g_file_get_contents (path, &contents, &size, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (size, ==, sizeof (*vector));
  vector = (const FeatureOverlapVector *) contents;
  g_assert_cmphex (vector->magic, ==, 0x504f4643);
  g_assert_cmpuint (vector->version, ==, 1);
  g_assert_cmpint (vector->template_type, ==, 24);
  g_assert_cmpuint (vector->gallery_count, <=, 180);
  g_assert_cmpuint (vector->probe_count, <=, 180);
  goodix_chicago_match_feature_overlap_type24 (
    vector->gallery_records, vector->gallery_count,
    vector->probe_records, vector->probe_count, 80, 64,
    vector->transform, vector->geometry_count, &result);
  g_test_message ("feature overlap native=%d,%d,%d,%d,%d,%d "
                  "official=%d,%d,%d,%d,%d,%d",
                  result.eligible_count, result.matched_count,
                  result.matched_percent, result.geometry_percent,
                  result.mean_distance, result.special_count,
                  vector->statistics.eligible_count,
                  vector->statistics.matched_count,
                  vector->statistics.matched_percent,
                  vector->statistics.geometry_percent,
                  vector->statistics.mean_distance,
                  vector->statistics.special_count);
  g_assert_cmpmem (&result, sizeof (result), &vector->statistics,
                   sizeof (vector->statistics));
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gdix51c0/chicago-match/candidate-self",
                   test_candidate_self);
  g_test_add_func ("/gdix51c0/chicago-match/score-aggregation-official",
                   test_score_aggregation_official);
  g_test_add_func ("/gdix51c0/chicago-match/study-admission-type24",
                   test_study_admission_type24);
  g_test_add_func ("/gdix51c0/chicago-match/study-aggregate-q8-type24",
                   test_study_aggregate_q8_type24);
  g_test_add_func ("/gdix51c0/chicago-match/study-aggregate-q8-oracle",
                   test_study_aggregate_q8_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/candidate-prefilter-type24-oracle",
                   test_candidate_prefilter_type24_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/study-metric-offline-oracle",
                   test_study_metric_offline_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/resolution-evidence-decoders",
                   test_resolution_evidence_decoders);
  g_test_add_func ("/gdix51c0/chicago-match/transform-overlap-area-type24",
                   test_transform_overlap_area_type24);
  g_test_add_func ("/gdix51c0/chicago-match/late-rejection-type24",
                   test_late_rejection_type24);
  g_test_add_func ("/gdix51c0/chicago-match/late-rejection-oracle",
                   test_late_rejection_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/scheduler-auxiliary-type24",
                   test_scheduler_auxiliary_type24);
  g_test_add_func ("/gdix51c0/chicago-match/fallback-geometry-records",
                   test_fallback_geometry_records);
  g_test_add_func ("/gdix51c0/chicago-match/ordinary-template-geometry",
                   test_ordinary_template_geometry);
  g_test_add_func ("/gdix51c0/chicago-match/offline-template-score",
                   test_offline_template_score);
  g_test_add_func ("/gdix51c0/chicago-match/offline-raw-score",
                   test_offline_raw_score);
  g_test_add_func ("/gdix51c0/chicago-match/candidate-oracle",
                   test_candidate_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/geometry-oracle",
                   test_geometry_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/orientation-oracle",
                   test_orientation_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/refinement-oracle",
                   test_refinement_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/subscore-config-oracle",
                   test_subscore_config_oracle);
  g_test_add_func ("/gdix51c0/chicago-match/feature-overlap-oracle",
                   test_feature_overlap_oracle);
  return g_test_run ();
}
