/* Native ChicagoHS preprocessing; requires per-device calibration. */
#include "gxfp/algo/image/chicago_backend.h"
#include "gxfp/algo/image/chicago_native.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum gxfp_chicago_backend_kind { CHICAGO_BACKEND_NONE, CHICAGO_BACKEND_NATIVE };

struct gxfp_chicago_backend {
    enum gxfp_chicago_backend_kind kind;
    uint16_t dark[5120];
    uint16_t coeff[5120];
    struct gxfp_chicago_adaptive adaptive;
};

/* The Windows file is a 16-byte header followed by a 0x224b0 payload whose
 * first matrix sits at offset 8. Everything is validated before use. */
#define CHICAGO_CALIBRATION_PAYLOAD 0x224b0u
#define CHICAGO_CALIBRATION_MATRIX1 8u
#define CHICAGO_CALIBRATION_VERSION 0x22490u

static int calibration_load(const char *path, uint16_t *calibration)
{
    static const char expected[] = "Preprocess_v_1.01.01";
    unsigned char *blob;
    FILE *file;
    long size;
    int result = -EINVAL;

    file = fopen(path, "rb");
    if (!file)
        return -errno;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return -EIO;
    }
    if ((unsigned long)size != 16u + CHICAGO_CALIBRATION_PAYLOAD) {
        fclose(file);
        return -EINVAL;
    }
    blob = malloc((size_t)size);
    if (!blob) {
        fclose(file);
        return -ENOMEM;
    }
    if (fread(blob, 1, (size_t)size, file) != (size_t)size) {
        free(blob);
        fclose(file);
        return -EIO;
    }
    fclose(file);
    if (memcmp(blob + 16 + CHICAGO_CALIBRATION_VERSION, expected, sizeof(expected)) == 0) {
        for (size_t i = 0; i < 5120; i++)
            calibration[i] = (uint16_t)(blob[16 + CHICAGO_CALIBRATION_MATRIX1 + 2 * i]
                                        | (blob[16 + CHICAGO_CALIBRATION_MATRIX1 + 2 * i + 1] << 8));
        result = 0;
    }
    free(blob);
    return result;
}

int gxfp_chicago_backend_init(struct gxfp_chicago_backend **ctx,
                              const struct gxfp_decoded_image *dark)
{
    const char *mode = getenv("GXFP_CHICAGO_PREPROCESS");
    const char *path;
    struct gxfp_chicago_backend *backend;
    int result;

    if (!ctx || !dark || !dark->pixels || dark->rows != 64 || dark->cols != 80)
        return -EINVAL;
    gxfp_chicago_backend_free(ctx);
    if (!mode || !strcmp(mode, "fpn"))
        return 0;
    backend = calloc(1, sizeof(*backend));
    if (!backend)
        return -ENOMEM;
    if (!strcmp(mode, "native")) {
        path = getenv("GXFP_CHICAGO_CALIB");
        if (!path || path[0] != '/') {
            fprintf(stderr, "gxfp chicago: native backend needs an absolute GXFP_CHICAGO_CALIB\n");
            free(backend);
            return -EINVAL;
        }
        result = calibration_load(path, backend->coeff);
        if (result) {
            fprintf(stderr, "gxfp chicago: calibration %s rejected (%s)\n", path, strerror(-result));
            free(backend);
            return result;
        }
        /* Keep the raw calibration: the adaptive reference is seeded with the
         * matrix itself, not with the Q13 map derived from it. */
        uint16_t *calibration = malloc(5120 * sizeof(*calibration));
        if (!calibration) {
            free(backend);
            return -ENOMEM;
        }
        memcpy(calibration, backend->coeff, 5120 * sizeof(*calibration));
        result = gxfp_chicago_coeff_from_calibration(backend->coeff, backend->coeff, 5120);
        if (!result)
            gxfp_chicago_adaptive_init(&backend->adaptive, calibration);
        free(calibration);
        if (result) {
            free(backend);
            return result;
        }
        memcpy(backend->dark, dark->pixels, sizeof(backend->dark));
        backend->kind = CHICAGO_BACKEND_NATIVE;
    } else {
        fprintf(stderr, "gxfp chicago: unknown GXFP_CHICAGO_PREPROCESS=%s\n", mode);
        free(backend);
        return -EINVAL;
    }
    *ctx = backend;
    return 0;
}

int gxfp_chicago_backend_process(struct gxfp_chicago_backend *ctx,
                                 struct gxfp_decoded_image *image)
{
    uint8_t processed[5120];
    int result;

    if (!ctx || !image || !image->pixels || image->rows != 64 || image->cols != 80)
        return -EINVAL;
    if (ctx->kind != CHICAGO_BACKEND_NATIVE)
        return -EINVAL;
    result = gxfp_chicago_process(&ctx->adaptive, image->pixels, ctx->dark, processed);
    if (result)
        return result;
    /* ceil(v * 4095 / 255) so libfprint's 12-to-8-bit conversion recovers v. */
    for (size_t i = 0; i < 5120; i++)
        image->pixels[i] = (uint16_t)((processed[i] * 4095u + 254u) / 255u);
    return 0;
}

void gxfp_chicago_backend_free(struct gxfp_chicago_backend **ctx)
{
    struct gxfp_chicago_backend *backend;
    if (!ctx || !*ctx)
        return;
    backend = *ctx;
    free(backend);
    *ctx = NULL;
}
