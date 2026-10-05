/* End-to-end acceptance test for the ported Chicago recognition chain.
 *
 * The image path in this driver is our own reverse-engineered code; the matcher
 * side is the verbatim port in src/algo/match/. This test walks the whole chain in
 * one process, in the order the driver would:
 *
 *   calibration generate -> preprocessor -> feature/probe -> probe pack ->
 *   print data -> match (with late rejection) -> adaptive study
 *
 * It is fully self-contained: the calibration is generated from a synthetic base
 * frame, so no private calibration file, sensor, or capture is involved. Every
 * stage must complete and report a defined outcome for the chain to count as
 * ported; the assertions are on structure and consistency, not on any biometric
 * value.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#include "goodix-chicago-calibration.h"
#include "goodix-chicago-enrollment.h"
#include "goodix-chicago-match.h"
#include "goodix-chicago-preprocess.h"
#include "goodix-chicago-runtime.h"
#include "goodix-chicago-template.h"

#define WIDTH  80
#define HEIGHT 64
#define PIXELS (WIDTH * HEIGHT)

/* A ridge-like frame: the preprocessor's capture checks reject flat input, so the
 * synthetic scene carries real contrast and a ridge period inside the expected band. */
static void
build_ridge_frame (guint16 *frame, double phase, double amplitude, guint16 level)
{
  for (guint y = 0; y < HEIGHT; y++)
    for (guint x = 0; x < WIDTH; x++)
      {
        double ridge = sin ((x + 0.35 * y) * 2.0 * G_PI / 6.0 + phase);
        double warp = 0.15 * sin ((0.7 * x + 0.4 * y) * 2.0 * G_PI / 23.0);
        double value = level + amplitude * (0.75 * ridge + warp);
        frame[y * WIDTH + x] = (guint16) CLAMP (value, 0.0, 4095.0);
      }
}

static void
build_base_frame (guint16 *frame)
{
  for (guint y = 0; y < HEIGHT; y++)
    for (guint x = 0; x < WIDTH; x++)
      {
        double value = 2800.0 + 120.0 * sin (x * 2.0 * G_PI / 37.0)
                              +  90.0 * cos (y * 2.0 * G_PI / 29.0);
        frame[y * WIDTH + x] = (guint16) CLAMP (value, 0.0, 4095.0);
      }
}

static void
test_chain_runs_end_to_end (void)
{
  guint16 base[PIXELS], frame[PIXELS];
  g_autoptr (GBytes) calibration = NULL;
  g_autoptr (GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr (GError) error = NULL;
  GoodixChicagoRuntimeProbe *probes[3] = { NULL, NULL, NULL };
  g_autoptr (GBytes) packed = NULL;

  build_base_frame (base);
  calibration = goodix_chicago_calibration_generate (base);
  g_assert_nonnull (calibration);
  g_assert_cmpuint (g_bytes_get_size (calibration), ==,
                    GOODIX_CHICAGO_CALIBRATION_PAYLOAD_LEN);

  preprocessor = goodix_chicago_preprocessor_new (calibration, base, &error);
  g_assert_no_error (error);
  g_assert_nonnull (preprocessor);

  /* Stage 1: capture -> feature. Three frames of one synthetic finger. */
  for (guint i = 0; i < G_N_ELEMENTS (probes); i++)
    {
      GoodixChicagoRuntimeReject reject = GOODIX_CHICAGO_RUNTIME_REJECT_NONE;
      /* Amplitude, level and ridge period chosen so the capture checks accept the
       * scene: at period 5.0 px the detector reports NO_FEATURES. */
      build_ridge_frame (frame, 0.4 + 0.25 * i, 300.0, 1200);
      probes[i] = goodix_chicago_runtime_prepare_probe (preprocessor, frame, &reject, &error);
      g_assert_no_error (error);
      g_assert_nonnull (probes[i]);
      g_assert_cmpint (reject, ==, GOODIX_CHICAGO_RUNTIME_REJECT_NONE);
    }

  /* The probe must expose a view: this is the feature stage's output contract. */
  const GoodixChicagoSubtemplateView *view =
    goodix_chicago_runtime_probe_get_view (probes[0]);
  g_assert_nonnull (view);

  /* Actual driver's composition: independently extract enhanced-image features,
   * retaining same-frame raw resolution metadata and exercising serialization. */
  {
    guint8 enhanced[PIXELS];
    GoodixChicagoFeatureRecord expected[GOODIX_CHICAGO_FEATURE_RECORD_LIMIT] = {0};
    GoodixChicagoFeatureConsensus consensus;
    guint active = 0;
    g_autoptr(GoodixChicagoPreprocessor) fresh =
      goodix_chicago_preprocessor_new (calibration, base, &error);
    g_assert_no_error (error);
    build_ridge_frame (frame, 0.4, 300.0, 1200);
    g_assert_cmpint (goodix_chicago_preprocessor_build_enhanced_checked (
      fresh, frame, enhanced), ==, GOODIX_CHICAGO_PREPROCESS_STATUS_OK);
    guint count = goodix_chicago_feature_extract_subtemplate_full (
      enhanced, expected, G_N_ELEMENTS(expected), &active, &consensus);
    g_assert_cmpuint (count, >, 0);
    g_autoptr(GoodixChicagoRuntimeProbe) composed =
      gxfp_chicago_runtime_prepare_enhanced_probe (enhanced, probes[0], &error);
    g_assert_no_error (error);
    g_assert_nonnull (composed);
    const GoodixChicagoSubtemplateView *cv = goodix_chicago_runtime_probe_get_view (composed);
    g_assert_cmpuint (cv->record_count, ==, count);
    g_assert_cmpmem (cv->records, count * sizeof(expected[0]), expected, count * sizeof(expected[0]));
    g_assert_cmpuint (cv->metric_data->packed_resolution, ==, view->metric_data->packed_resolution);
    g_assert_cmpmem (&cv->live_auxiliary, sizeof(cv->live_auxiliary),
                     &view->live_auxiliary, sizeof(view->live_auxiliary));
    /* Reproduce the successful standalone observer independently, including
     * its metrics and serialized gallery, rather than comparing features only. */
    GoodixChicagoMetricData legacy_metric;
    GoodixChicagoEnrollmentResult legacy_info;
    guint8 legacy_quality, legacy_coverage;
    goodix_chicago_preprocessor_finalize_metrics (
      goodix_chicago_preprocessor_compute_base_quality_from_enhanced (enhanced),
      goodix_chicago_preprocessor_compute_coverage (enhanced),
      &legacy_quality, &legacy_coverage);
    goodix_chicago_enrollment_build_metric_data (enhanced, &legacy_metric);
    legacy_metric.packed_resolution = view->metric_data->packed_resolution;
    g_assert_cmpuint (cv->active_count, ==, active);
    g_assert_cmpuint (cv->quality, ==, legacy_quality);
    g_assert_cmpuint (cv->coverage, ==, legacy_coverage);
    g_assert_cmpmem (cv->metric_data, sizeof(legacy_metric),
                     &legacy_metric, sizeof(legacy_metric));
    g_assert_cmpuint (cv->density_positive_percent, ==, consensus.positive_percent);
    g_assert_cmpuint (cv->density_class, ==, consensus.density_class);
    g_assert_cmpuint (cv->density_inactive_count, ==, consensus.inactive_count);
    g_autoptr(GoodixChicagoEnrollment) legacy = goodix_chicago_enrollment_new ();
    g_assert_true (goodix_chicago_enrollment_insert_first (legacy, expected, count,
      active, legacy_quality, legacy_coverage, &legacy_metric, &legacy_info, &error));
    g_assert_no_error (error);
    g_autoptr(GBytes) legacy_packed = goodix_chicago_enrollment_pack (legacy, &error);
    g_assert_no_error (error);
    g_autoptr(GBytes) serialized = goodix_chicago_runtime_probe_pack (composed, &error);
    g_assert_no_error (error);
    g_assert_nonnull (serialized);
    g_assert_true (g_bytes_equal (serialized, legacy_packed));
    guint8 id[16] = {0};
    g_autoptr(GVariant) data = goodix_chicago_print_data_build (id, calibration, serialized);
    GoodixChicagoMatchTemplateResult match_result;
    g_assert_true (goodix_chicago_runtime_match_print_data (
      composed, data, id, calibration, &match_result, &error));
    g_assert_no_error (error);
    GoodixChicagoSubtemplateView legacy_view;
    g_assert_true (goodix_chicago_enrollment_get_subtemplate (legacy, 0, &legacy_view));
    legacy_view.density_positive_percent = consensus.positive_percent;
    legacy_view.density_class = consensus.density_class;
    legacy_view.density_inactive_count = consensus.inactive_count;
    legacy_view.live_auxiliary = view->live_auxiliary;
    gsize packed_len;
    const guint8 *packed_ptr = g_bytes_get_data (legacy_packed, &packed_len);
    g_autoptr(GoodixChicagoEnrollment) legacy_restored =
      goodix_chicago_enrollment_unpack (packed_ptr, packed_len, &error);
    g_assert_no_error (error);
    g_assert_cmpint (match_result.score, ==, goodix_chicago_match_score_template_type24 (
      legacy_restored, &legacy_view, GOODIX_CHICAGO_RUNTIME_SELECTOR_THRESHOLD));
    /* A blank enhanced image must not invent features. */
    memset (enhanced, 128, sizeof(enhanced));
    g_autoptr(GoodixChicagoRuntimeProbe) empty =
      gxfp_chicago_runtime_prepare_enhanced_probe (enhanced, probes[0], &error);
    g_assert_no_error (error);
    g_assert_null (empty);
  }

  /* Stage 2: serialization. */
  packed = goodix_chicago_runtime_probe_pack (probes[0], &error);
  g_assert_no_error (error);
  g_assert_nonnull (packed);
  g_assert_cmpuint (g_bytes_get_size (packed), >, 0);

  /* The unpacked form must round-trip through the enrollment container. */
  {
    gsize size = 0;
    const guint8 *data = g_bytes_get_data (packed, &size);
    g_autoptr (GoodixChicagoEnrollment) gallery =
      goodix_chicago_enrollment_unpack (data, size, &error);
    g_assert_no_error (error);
    g_assert_nonnull (gallery);
    g_assert_cmpuint (goodix_chicago_enrollment_get_count (gallery), >, 0);
    g_assert_cmpuint (goodix_chicago_enrollment_get_capacity (gallery), >, 0);

    /* Stage 3: matching, including the geometric scorer and late rejection. */
    guint32 score = goodix_chicago_match_score_template_type24 (
      gallery, view, GOODIX_CHICAGO_RUNTIME_SELECTOR_THRESHOLD);
    g_assert_cmpuint (score, <=, 1000);   /* a defined scale, not a wild value */
  }

  /* Stage 4: the print-data path the driver uses, and adaptive study on top. */
  {
    gsize cal_size = 0;
    const guint8 *cal_data = g_bytes_get_data (calibration, &cal_size);
    gsize packed_size = 0;
    const guint8 *packed_data = g_bytes_get_data (packed, &packed_size);
    guint8 sensor_id[GOODIX_CHICAGO_SENSOR_ID_LEN];
    g_autoptr (GBytes) gallery_bytes = NULL;
    g_autoptr (GVariant) print_data = NULL;
    GoodixChicagoMatchTemplateResult result;
    g_autoptr (GVariant) updated = NULL;

    memset (sensor_id, 0x5a, sizeof (sensor_id));
    memset (&result, 0, sizeof (result));
    gallery_bytes = g_bytes_new (packed_data, packed_size);
    print_data = g_variant_ref_sink (goodix_chicago_print_data_build (
      sensor_id, g_bytes_new (cal_data, cal_size), gallery_bytes));
    g_assert_nonnull (print_data);

    /* Matching the probe against its own packed form must succeed and report a score. */
    gboolean matched = goodix_chicago_runtime_match_print_data (
      probes[1], print_data, sensor_id, calibration, &result, &error);
    g_assert_no_error (error);
    g_assert_true (matched);
    g_assert_cmpint (result.study_eligibility_known, ==, result.score > 0);

    /* The score entry point must agree with the match entry point. */
    gint32 score = goodix_chicago_runtime_score_print_data (
      probes[1], print_data, sensor_id, calibration, &error);
    g_assert_no_error (error);
    g_assert_cmpint (score, ==, (gint32) result.score);

    /* Study is a successful no-op when the match is not eligible. */
    g_assert_true (goodix_chicago_runtime_study_print_data (
      probes[1], print_data, sensor_id, calibration, &result, &updated, &error));
    g_assert_no_error (error);
    if (!(result.study_eligibility_known && result.study_eligible))
      g_assert_null (updated);
  }

  for (guint i = 0; i < G_N_ELEMENTS (probes); i++)
    goodix_chicago_runtime_probe_free (probes[i]);

  g_print ("chicago chain: calibration, preprocess, feature, pack, match, study all ran\n");
}

static void
test_chain_rejects_flat_input (void)
{
  guint16 base[PIXELS], flat[PIXELS];
  g_autoptr (GBytes) calibration = NULL;
  g_autoptr (GoodixChicagoPreprocessor) preprocessor = NULL;
  g_autoptr (GError) error = NULL;
  GoodixChicagoRuntimeReject reject = GOODIX_CHICAGO_RUNTIME_REJECT_NONE;
  GoodixChicagoRuntimeProbe *probe = NULL;

  build_base_frame (base);
  calibration = goodix_chicago_calibration_generate (base);
  g_assert_nonnull (calibration);
  preprocessor = goodix_chicago_preprocessor_new (calibration, base, &error);
  g_assert_no_error (error);

  /* The chain's input stage must reject a frame with no signal rather than
   * silently producing features from noise. */
  for (guint i = 0; i < PIXELS; i++)
    flat[i] = 1500;
  probe = goodix_chicago_runtime_prepare_probe (preprocessor, flat, &reject, &error);
  g_assert_no_error (error);
  if (probe)
    {
      g_assert_cmpint (reject, !=, GOODIX_CHICAGO_RUNTIME_REJECT_NONE);
      goodix_chicago_runtime_probe_free (probe);
    }
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gxfp/chicago-chain/runs-end-to-end", test_chain_runs_end_to_end);
  g_test_add_func ("/gxfp/chicago-chain/rejects-flat-input", test_chain_rejects_flat_input);
  return g_test_run ();
}
