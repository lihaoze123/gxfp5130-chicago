#pragma once
#include "gxfp/algo/image/decoder.h"
/* Native ChicagoHS preprocessing backend. */
struct gxfp_chicago_backend;
int gxfp_chicago_backend_init(struct gxfp_chicago_backend **ctx,
                              const struct gxfp_decoded_image *dark);
int gxfp_chicago_backend_process(struct gxfp_chicago_backend *ctx,
                                 struct gxfp_decoded_image *image);
void gxfp_chicago_backend_free(struct gxfp_chicago_backend **ctx);
