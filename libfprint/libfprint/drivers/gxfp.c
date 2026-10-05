/* SPDX-License-Identifier: LGPL-2.1-or-later
 * GXFP5130 eSPI transport with host-side native Chicago enrollment/matching.
 * The image/transport implementations and Chicago backend retain their own notices.
 */
#define FP_COMPONENT "gxfp"
#include "drivers_api.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "gxfp/algo/common.h"
#include "gxfp/flow/session.h"
#include "gxfp/algo/match/goodix-chicago-runtime.h"
#include "gxfp/algo/match/goodix-chicago-template.h"

#define PSK_PATH "/var/lib/fprintd/gxfp/psk_raw32.bin"
#define CALIB_PATH "/var/lib/fprintd/gxfp/goodix_calib.dat"
typedef enum { IDLE, ACTIVATING, WAIT_DOWN, CAPTURING, WAIT_UP } Phase;
struct _FpiDeviceGxfp {
  FpDevice parent;
  struct gxfp_session sess;
  GSource *pump;
  GSource *deadline;
  Phase phase;
  gboolean finish_after_lift;
  GBytes *calibration;
  guint8 sensor_id[16];
  GoodixChicagoPreprocessor *preprocessor;
  GoodixChicagoRuntimeProbe *raw_probe;
  GoodixChicagoRuntimeProbe *deferred;
  GoodixChicagoEnrollment *enrollment;
  GoodixChicagoEngineEnrollmentPolicy policy;
};
G_DECLARE_FINAL_TYPE (FpiDeviceGxfp, fpi_device_gxfp, FPI, DEVICE_GXFP, FpDevice)
G_DEFINE_TYPE (FpiDeviceGxfp, fpi_device_gxfp, FP_TYPE_DEVICE)
static void events (FpiDeviceGxfp *, struct gxfp_session_events *);
static void schedule (FpiDeviceGxfp *, gint);

static void destroy_source (GSource **source)
{
  if (*source) { g_source_destroy (*source); *source = NULL; }
}
static void clear_action (FpiDeviceGxfp *self)
{
  g_clear_pointer (&self->enrollment, goodix_chicago_enrollment_free);
  g_clear_pointer (&self->raw_probe, goodix_chicago_runtime_probe_free);
  g_clear_pointer (&self->deferred, goodix_chicago_runtime_probe_free);
  g_clear_pointer (&self->preprocessor, goodix_chicago_preprocessor_free);
  self->finish_after_lift = FALSE;
}
static void complete (FpiDeviceGxfp *self, GError *error)
{
  FpDevice *dev = FP_DEVICE (self);
  FpiDeviceAction action = fpi_device_get_current_action (dev);
  FpPrint *print = NULL;
  if (action == FPI_DEVICE_ACTION_ENROLL && !error) {
    g_autoptr(GBytes) packed = goodix_chicago_enrollment_pack (self->enrollment, &error);
    if (packed) {
      g_autoptr(GVariant) data = g_variant_ref_sink (goodix_chicago_print_data_build (
        self->sensor_id, self->calibration, packed));
      fpi_device_get_enroll_data (dev, &print);
      fpi_print_set_type (print, FPI_PRINT_RAW);
      fpi_print_set_device_stored (print, FALSE);
      g_object_set (print, "fpi-data", data, NULL);
      g_object_ref (print);
    }
  }
  self->phase = IDLE;
  destroy_source (&self->pump);
  destroy_source (&self->deadline);
  struct gxfp_session_events ev;
  gxfp_session_events_clear (&ev);
  gxfp_session_request_deactivate (&self->sess, &ev);
  clear_action (self);
  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NONE);
  if (action == FPI_DEVICE_ACTION_ENROLL) fpi_device_enroll_complete (dev, print, error);
  else if (action == FPI_DEVICE_ACTION_VERIFY) fpi_device_verify_complete (dev, error);
  else if (action == FPI_DEVICE_ACTION_IDENTIFY) fpi_device_identify_complete (dev, error);
  else if (error) g_error_free (error);
}
static void change (FpiDeviceGxfp *self, Phase phase, enum gxfp_session_state state)
{
  struct gxfp_session_events ev;
  self->phase = phase;
  gxfp_session_events_clear (&ev);
  gxfp_session_change_state (&self->sess, state, &ev);
  events (self, &ev);
}
static int observe (void *userdata, const struct gxfp_decoded_image *image, int dark)
{
  FpiDeviceGxfp *self = userdata;
  g_autoptr(GError) error = NULL;
  if (image->rows != 64 || image->cols != 80) return -EINVAL;
  if (dark) {
    g_clear_pointer (&self->preprocessor, goodix_chicago_preprocessor_free);
    self->preprocessor = goodix_chicago_preprocessor_new (self->calibration, image->pixels, &error);
    if (!self->preprocessor) { fp_err ("Chicago initialization: %s", error->message); return -EINVAL; }
  } else {
    GoodixChicagoRuntimeReject reject;
    g_clear_pointer (&self->raw_probe, goodix_chicago_runtime_probe_free);
    if (!self->preprocessor) return -EINVAL;
    self->raw_probe = goodix_chicago_runtime_prepare_probe (self->preprocessor,
                                                           image->pixels, &reject, &error);
    if (error) { fp_err ("Chicago raw probe: %s", error->message); return -EINVAL; }
    if (!self->raw_probe) fp_dbg ("Chicago bad capture reason=%d", reject);
  }
  return 0;
}
static gboolean insert (GoodixChicagoEnrollment *enrollment,
                        const GoodixChicagoSubtemplateView *v,
                        GoodixChicagoEnrollmentResult *result, GError **error)
{
  if (!goodix_chicago_enrollment_get_count (enrollment))
    return goodix_chicago_enrollment_insert_first (enrollment, v->records, v->record_count,
      v->active_count, v->quality, v->coverage, v->metric_data, result, error);
  return goodix_chicago_enrollment_insert_next (enrollment, v->records, v->record_count,
      v->active_count, v->quality, v->coverage, v->metric_data, result, error);
}
static void enroll_probe (FpiDeviceGxfp *self, GoodixChicagoRuntimeProbe **probe_ptr)
{
  GoodixChicagoRuntimeProbe *probe = *probe_ptr;
  g_autoptr(GError) error = NULL;
  GoodixChicagoEnrollmentResult result;
  guint rejected = 0;
  if (!insert (self->enrollment, goodix_chicago_runtime_probe_get_view (probe), &result, &error)) {
    complete (self, g_steal_pointer (&error)); return;
  }
  gboolean accepted = goodix_chicago_engine_enrollment_policy_accept (
    &self->policy, result.position_x, result.position_y, &rejected);
  if (self->policy.defer_current_sample) {
    g_assert (self->deferred == NULL);
    g_autoptr(GBytes) packed = goodix_chicago_runtime_probe_pack (probe, &error);
    /* Keep the probe alive until restoration; the caller transfers it below. */
    if (!packed || !goodix_chicago_enrollment_drop_last (self->enrollment, &error)) {
      complete (self, g_steal_pointer (&error)); return;
    }
    self->deferred = g_steal_pointer (probe_ptr);
  }
  if (self->policy.restore_deferred_sample) {
    if (!self->deferred || !insert (self->enrollment,
        goodix_chicago_runtime_probe_get_view (self->deferred), &result, &error)) {
      if (!error) error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL, "Missing deferred sample");
      complete (self, g_steal_pointer (&error)); return;
    }
    g_clear_pointer (&self->deferred, goodix_chicago_runtime_probe_free);
  }
  fp_dbg ("Chicago enrollment accepted=%u/12 records=%u position-retry=%u",
    self->policy.used, goodix_chicago_enrollment_get_count (self->enrollment), rejected);
  if (accepted) fpi_device_enroll_progress (FP_DEVICE (self), self->policy.used, NULL, NULL);
  else {
    const gchar *message = rejected == 1 ? "Move finger lower" : rejected == 2 ? "Move finger higher" :
                           rejected == 3 ? "Move finger right" : "Move finger left";
    fpi_device_enroll_progress (FP_DEVICE (self), self->policy.used, NULL,
      /* These are directional coverage tips, not a request to return to
       * the center. fprintd discards our message and translates CENTER_FINGER
       * into enroll-finger-not-centered, which misguides enrollment. */
      fpi_device_retry_new_msg (FP_DEVICE_RETRY_GENERAL, "%s", message));
  }
  self->finish_after_lift = goodix_chicago_engine_enrollment_policy_complete (&self->policy);
  change (self, WAIT_UP, GXFP_SESSION_STATE_AWAIT_FINGER_OFF);
}
static void match_probe (FpiDeviceGxfp *self, GoodixChicagoRuntimeProbe *probe)
{
  FpDevice *dev = FP_DEVICE (self);
  g_autoptr(GError) error = NULL;
  g_autoptr(FpPrint) scanned = g_object_ref_sink (fp_print_new (dev));
  g_autoptr(GBytes) packed = goodix_chicago_runtime_probe_pack (probe, &error);
  if (!packed) { complete (self, g_steal_pointer (&error)); return; }
  g_autoptr(GVariant) scan_data = g_variant_ref_sink (
    goodix_chicago_print_data_build (self->sensor_id, self->calibration, packed));
  fpi_print_set_type (scanned, FPI_PRINT_RAW);
  fpi_print_set_device_stored (scanned, FALSE);
  g_object_set (scanned, "fpi-data", scan_data, NULL);
  FpPrint *matched = NULL, *verify = NULL;
  GPtrArray *prints = NULL;
  gboolean identify = fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_IDENTIFY;
  if (identify) fpi_device_get_identify_data (dev, &prints);
  else fpi_device_get_verify_data (dev, &verify);
  guint count = identify ? prints->len : 1;
  GoodixChicagoMatchTemplateResult best = {0};
  for (guint i = 0; i < count; i++) {
    FpPrint *candidate = identify ? g_ptr_array_index (prints, i) : verify;
    g_autoptr(GVariant) data = NULL;
    GoodixChicagoMatchTemplateResult result;
    g_object_get (candidate, "fpi-data", &data, NULL);
    if (!data || !goodix_chicago_runtime_match_print_data (probe, data,
        self->sensor_id, self->calibration, &result, &error)) {
      if (!error) error = fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_INVALID, "Chicago template required; re-enroll");
      complete (self, g_steal_pointer (&error)); return;
    }
    fp_dbg ("Chicago verify gallery=%u score=%d selected=%d study=%d", i, result.score,
            result.selected_index, result.study_eligible);
    if (result.score > 0 && (!matched || result.score > best.score)) { matched = candidate; best = result; }
  }
  if (matched) {
    g_autoptr(GVariant) data = NULL, updated = NULL;
    g_object_get (matched, "fpi-data", &data, NULL);
    if (!goodix_chicago_runtime_study_print_data (probe, data, self->sensor_id,
        self->calibration, &best, &updated, &error)) {
      /* A learning failure must not invalidate the verified biometric result. */
      fp_warn ("Chicago template study failed: %s", error->message);
      g_clear_error (&error);
    } else if (updated) {
      g_object_set (matched, "fpi-data", updated, NULL);
      fp_dbg ("Chicago template study updated matched print");
    }
  }
  if (identify) fpi_device_identify_report (dev, matched, scanned, NULL);
  else fpi_device_verify_report (dev, matched ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL, scanned, NULL);
  if (identify) {
    /* Enrollment's duplicate check starts enrollment as soon as identify
     * completes. Do not let that reinitialize the dark frame under the finger. */
    self->finish_after_lift = TRUE;
    change (self, WAIT_UP, GXFP_SESSION_STATE_AWAIT_FINGER_OFF);
  } else complete (self, NULL);
}
static void image_ready (FpiDeviceGxfp *self)
{
  struct gxfp_decoded_image image = {0};
  g_autoptr(GError) error = NULL;
  g_autoptr(GoodixChicagoRuntimeProbe) probe = NULL;
  guint8 enhanced[5120];
  if (gxfp_session_take_image (&self->sess, &image) < 0) {
    complete (self, fpi_device_error_new (FP_DEVICE_ERROR_GENERAL)); return;
  }
  if (image.rows == 64 && image.cols == 80 && self->raw_probe) {
    for (guint i=0;i<5120;i++) enhanced[i] = (guint32)image.pixels[i]*255u/4095u;
    probe = gxfp_chicago_runtime_prepare_enhanced_probe (enhanced, self->raw_probe, &error);
  }
  gxfp_decoded_image_free (&image);
  if (error) { complete (self, g_steal_pointer (&error)); return; }
  gboolean enroll = fpi_device_get_current_action (FP_DEVICE(self)) == FPI_DEVICE_ACTION_ENROLL;
  if (!probe) {
    GError *retry = fpi_device_retry_new_msg (FP_DEVICE_RETRY_GENERAL, "Poor fingerprint capture; try again");
    if (enroll) {
      fpi_device_enroll_progress (FP_DEVICE (self), self->policy.used, NULL, retry);
      change (self, WAIT_UP, GXFP_SESSION_STATE_AWAIT_FINGER_OFF);
    } else {
      if (fpi_device_get_current_action (FP_DEVICE(self)) == FPI_DEVICE_ACTION_IDENTIFY)
        fpi_device_identify_report (FP_DEVICE(self), NULL, NULL, retry);
      else fpi_device_verify_report (FP_DEVICE(self), FPI_MATCH_ERROR, NULL, retry);
      complete (self, NULL);
    }
    return;
  }
  const GoodixChicagoSubtemplateView *v = goodix_chicago_runtime_probe_get_view(probe);
  fp_dbg ("Chicago capture features=%u quality=%u coverage=%u",v->record_count,v->quality,v->coverage);
  if (enroll) {
    enroll_probe (self, &probe);
  } else match_probe (self, probe);
}
static void events (FpiDeviceGxfp *self, struct gxfp_session_events *ev)
{
  if (ev->cancel_tick) destroy_source (&self->pump);
  if (ev->session_error) {
    complete (self, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL, "%s", ev->error_msg)); return;
  }
  if (ev->activate_complete && self->phase == ACTIVATING) {
    fpi_device_report_finger_status (FP_DEVICE(self), FP_FINGER_STATUS_NEEDED);
    change (self, WAIT_DOWN, GXFP_SESSION_STATE_AWAIT_FINGER_ON);
  }
  if (ev->finger_status_changed) {
    fpi_device_report_finger_status (FP_DEVICE(self), ev->finger_present ?
      FP_FINGER_STATUS_PRESENT : FP_FINGER_STATUS_NEEDED);
    if (ev->finger_present && self->phase == WAIT_DOWN) change (self, CAPTURING, GXFP_SESSION_STATE_CAPTURE);
    else if (!ev->finger_present && self->phase == WAIT_UP) {
      if (self->finish_after_lift) complete (self, NULL);
      else change (self, WAIT_DOWN, GXFP_SESSION_STATE_AWAIT_FINGER_ON);
    }
  }
  if (ev->image_ready && self->phase == CAPTURING) image_ready (self);
  if (ev->request_tick && self->phase != IDLE) schedule (self, ev->tick_delay_ms);
}
static void pump_cb (FpDevice *dev, gpointer data)
{
  FpiDeviceGxfp *self = data;
  self->pump = NULL;
  if (self->phase == IDLE) return;
  if (fpi_device_action_is_cancelled(dev)) {
    complete (self, g_error_new_literal (G_IO_ERROR,G_IO_ERROR_CANCELLED,"Fingerprint operation cancelled")); return;
  }
  struct gxfp_session_events ev;
  gxfp_session_events_clear (&ev);
  gxfp_session_pump (&self->sess,0,&ev); events (self,&ev);
  if (self->phase == IDLE) return;
  int ready = gxfp_session_poll_readable (&self->sess,0);
  if (ready == 0 || ready == -EAGAIN) {
    gxfp_session_events_clear (&ev);
    gxfp_session_on_fd (&self->sess,GXFP_SESSION_IO_IN,0,&ev); events(self,&ev);
  }
}
static void schedule (FpiDeviceGxfp *self, gint delay)
{
  if (!self->pump && self->phase != IDLE)
    self->pump = fpi_device_add_timeout(FP_DEVICE(self),delay,pump_cb,self,NULL);
}
static void deadline_cb (FpDevice *dev, gpointer data)
{
  FpiDeviceGxfp *self = data; (void)dev;
  self->deadline = NULL;
  complete(self,fpi_device_error_new_msg(FP_DEVICE_ERROR_GENERAL,"Fingerprint operation timed out"));
}
static void start (FpDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP(dev);
  clear_action (self);
  if (fpi_device_get_current_action(dev) == FPI_DEVICE_ACTION_IDENTIFY) {
    GPtrArray *prints = NULL;
    fpi_device_get_identify_data (dev, &prints);
    /* No host gallery and no sensor storage: there is nothing to identify.
     * In particular, first enrollment must not consume an extra touch and
     * immediately recapture a dark frame while that finger is still down. */
    if (prints->len == 0) {
      fpi_device_identify_report (dev, NULL, NULL, NULL);
      complete (self, NULL);
      return;
    }
  }
  if (fpi_device_get_current_action(dev) == FPI_DEVICE_ACTION_ENROLL) {
    self->enrollment = goodix_chicago_enrollment_new();
    goodix_chicago_engine_enrollment_policy_init(&self->policy);
  }
  self->phase = ACTIVATING;
  self->deadline = fpi_device_add_timeout(dev, fpi_device_get_current_action(dev)==FPI_DEVICE_ACTION_ENROLL ?
                                        600000 : 90000,deadline_cb,self,NULL);
  struct gxfp_session_events ev; gxfp_session_events_clear(&ev);
  int r = gxfp_session_activate(&self->sess,g_getenv("FP_GXFP_LOG") != NULL,&ev);
  events(self,&ev);
  if (r<0 && !ev.session_error) complete(self,fpi_device_error_new(FP_DEVICE_ERROR_GENERAL));
  else if (self->phase != IDLE) schedule(self,0);
}
static void dev_probe (FpDevice *dev)
{
  /* New storage namespace; existing SIGFM prints remain untouched. */
  fpi_device_probe_complete(dev,"chicago-v1",NULL,NULL);
}
static void dev_open (FpDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP(dev);
  g_autoptr(GError) error = NULL;
  uint8_t *psk = NULL; size_t length = 0; char message[256]={0};
  const gchar *path=fpi_device_get_udev_data(dev,FPI_DEVICE_UDEV_SUBTYPE_CHARDEV);
  const gchar *psk_path=g_getenv("FP_GXFP_PSK"), *calib_path=g_getenv("GXFP_CHICAGO_CALIB");
  if (!path || g_strcmp0(g_getenv("GXFP_CHICAGO_PREPROCESS"),"native") != 0) {
    error=fpi_device_error_new_msg(FP_DEVICE_ERROR_NOT_SUPPORTED,"Chicago driver requires native preprocessing"); goto out;
  }
  int r=gxfp_read_file_all(psk_path ? psk_path : PSK_PATH,&psk,&length);
  if(r<0) { error=fpi_device_error_new_msg(FP_DEVICE_ERROR_GENERAL,"Cannot read sensor key: %s",g_strerror(-r)); goto out; }
  r=gxfp_session_open(&self->sess,path,psk,length,g_getenv("FP_GXFP_LOG") != NULL,message,sizeof(message));
  if(r<0) { error=fpi_device_error_new_msg(FP_DEVICE_ERROR_GENERAL,"%s",message); goto out; }
  if(gxfp_session_get_sensor_id(&self->sess,self->sensor_id)<0) {
    error=fpi_device_error_new_msg(FP_DEVICE_ERROR_NOT_SUPPORTED,"Chicago OTP identity unavailable"); goto out;
  }
  g_clear_pointer(&self->calibration,g_bytes_unref);
  self->calibration=goodix_chicago_calibration_load(calib_path ? calib_path : CALIB_PATH,self->sensor_id,&error);
  if(!self->calibration) goto out;
  gxfp_session_set_image_observer(&self->sess,observe,self);
  fp_dbg("Chicago native matcher ready: 12-stage enrollment, selector=207, format=chicago-v1");
out:
  free(psk);
  if(error) gxfp_session_dispose(&self->sess);
  fpi_device_open_complete(dev,g_steal_pointer(&error));
}
static void dev_close (FpDevice *dev)
{
  FpiDeviceGxfp *self=FPI_DEVICE_GXFP(dev);
  destroy_source(&self->pump); destroy_source(&self->deadline);
  clear_action(self); self->phase=IDLE;
  gxfp_session_dispose(&self->sess); gxfp_session_init(&self->sess);
  g_clear_pointer(&self->calibration,g_bytes_unref);
  fpi_device_close_complete(dev,NULL);
}
static void dev_cancel (FpDevice *dev)
{
  FpiDeviceGxfp *self=FPI_DEVICE_GXFP(dev);
  if(self->phase != IDLE) complete(self,g_error_new_literal(G_IO_ERROR,G_IO_ERROR_CANCELLED,"Fingerprint operation cancelled"));
}
static void dispose (GObject *object)
{
  FpiDeviceGxfp *self=FPI_DEVICE_GXFP(object);
  destroy_source(&self->pump); destroy_source(&self->deadline); clear_action(self);
  g_clear_pointer(&self->calibration,g_bytes_unref); gxfp_session_dispose(&self->sess);
  G_OBJECT_CLASS(fpi_device_gxfp_parent_class)->dispose(object);
}
static const FpIdEntry ids[] = {
  { .udev_types=FPI_DEVICE_UDEV_SUBTYPE_CHARDEV, .chardev_acpi_id="GXFP5130" }, { .udev_types=0 }
};
static void fpi_device_gxfp_class_init (FpiDeviceGxfpClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose=dispose;
  FpDeviceClass *dev=FP_DEVICE_CLASS(klass);
  dev->id=FP_COMPONENT; dev->full_name="Goodix GXFP5130 ChicagoHS native matcher";
  dev->type=FP_DEVICE_TYPE_UDEV; dev->id_table=ids; dev->scan_type=FP_SCAN_TYPE_PRESS;
  dev->nr_enroll_stages=GOODIX_CHICAGO_ENGINE_REQUIRED_SAMPLES;
  dev->probe=dev_probe; dev->open=dev_open; dev->close=dev_close;
  dev->enroll=start; dev->verify=start; dev->identify=start; dev->cancel=dev_cancel;
  fpi_device_class_auto_initialize_features(dev);
}
static void fpi_device_gxfp_init (FpiDeviceGxfp *self)
{
  gxfp_session_init(&self->sess);
}
