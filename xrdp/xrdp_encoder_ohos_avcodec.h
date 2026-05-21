/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * HarmonyOS AVCodec H.264 encoder
 */

#ifndef _XRDP_ENCODER_OHOS_AVCODEC_H
#define _XRDP_ENCODER_OHOS_AVCODEC_H

#include "arch.h"

void *
xrdp_encoder_ohos_avcodec_create(void);
int
xrdp_encoder_ohos_avcodec_delete(void *handle);
int
xrdp_encoder_ohos_avcodec_encode(void *handle, int session, int left, int top,
                                  int width, int height, int twidth,
                                  int theight, int format, const char *data,
                                  short *crects, int num_crects,
                                  char *cdata, int *cdata_bytes,
                                  int connection_type, int *flags_ptr);

/**
 * Test whether a HarmonyOS hardware AVC encoder is available.
 *
 * @return Boolean (!= 0 -> working)
 */
int
xrdp_encoder_ohos_avcodec_install_ok(void);

#endif
