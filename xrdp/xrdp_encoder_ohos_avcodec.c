/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * HarmonyOS AVCodec H.264 encoder
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdbool.h>
#include <stdint.h>

#include <multimedia/player_framework/native_avbuffer.h>
#include <multimedia/player_framework/native_avcapability.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avcodec_videoencoder.h>
#include <multimedia/player_framework/native_averrors.h>
#include <multimedia/player_framework/native_avformat.h>

#include "xrdp.h"
#include "arch.h"
#include "os_calls.h"
#include "xrdp_encoder_ohos_avcodec.h"
#include "xrdp_tconfig.h"

#define OHOS_AVCODEC_MAX_ENCODERS 16
#define OHOS_AVCODEC_INPUT_TIMEOUT_US 8000
#define OHOS_AVCODEC_OUTPUT_TIMEOUT_US 8000
#define OHOS_AVCODEC_FOLLOWUP_OUTPUT_TIMEOUT_US 4000
#define OHOS_AVCODEC_OUTPUT_DEADLINE_US 24000
#define OHOS_AVCODEC_MAX_OUTPUT_ATTEMPTS 8
#define OHOS_AVCODEC_DEFAULT_BITRATE 20000000
#define OHOS_AVCODEC_DEFAULT_FRAMERATE 60

struct ohos_avcodec_encoder
{
    OH_AVCodec *codec;
    char *codec_config;
    int codec_config_bytes;
    int codec_config_capacity;
    int width;
    int height;
    int input_stride;
    int input_slice_height;
    int input_pixel_format;
    int frame_rate;
    int bitrate;
    int started;
    int64_t pts;
    uint64_t encode_calls;
    uint64_t output_frames;
    uint64_t no_output_frames;
    char codec_name[128];
};

struct ohos_avcodec_global
{
    struct ohos_avcodec_encoder encoders[OHOS_AVCODEC_MAX_ENCODERS];
    struct xrdp_tconfig_gfx_openh264_param
        openh264_param[NUM_CONNECTION_TYPES];
};

struct ohos_avcodec_bitrate_decision
{
    int requested;
    int max_requested;
    int candidate;
    int range_valid;
    int range_min;
    int range_max;
    int range_rc;
    int final_bitrate;
    int clamped;
};

/*****************************************************************************/
static int
ohos_avcodec_should_log(uint64_t count)
{
    return count <= 8 || (count % 60) == 0;
}

/*****************************************************************************/
static const char *
ohos_avcodec_connection_type_name(int connection_type)
{
    switch (connection_type)
    {
        case 0:
            return "default";
        case CONNECTION_TYPE_MODEM:
            return "modem";
        case CONNECTION_TYPE_BROADBAND_LOW:
            return "broadband_low";
        case CONNECTION_TYPE_SATELLITE:
            return "satellite";
        case CONNECTION_TYPE_BROADBAND_HIGH:
            return "broadband_high";
        case CONNECTION_TYPE_WAN:
            return "wan";
        case CONNECTION_TYPE_LAN:
            return "lan";
        case CONNECTION_TYPE_AUTODETECT:
            return "autodetect";
        default:
            return "unknown";
    }
}

/*****************************************************************************/
static int
ohos_avcodec_min_int(int a, int b)
{
    return a < b ? a : b;
}

/*****************************************************************************/
static int
ohos_avcodec_max_int(int a, int b)
{
    return a > b ? a : b;
}

/*****************************************************************************/
static int
ohos_avcodec_clamp_int(int value, int min_value, int max_value)
{
    if (max_value > min_value)
    {
        value = ohos_avcodec_max_int(value, min_value);
        value = ohos_avcodec_min_int(value, max_value);
    }
    return value;
}

/*****************************************************************************/
static uint32_t
ohos_avcodec_read_be32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

/*****************************************************************************/
static int
ohos_avcodec_has_start_code(const unsigned char *data, int bytes)
{
    int index;

    if (data == NULL || bytes < 4)
    {
        return 0;
    }
    for (index = 0; index + 3 < bytes; ++index)
    {
        if (data[index] == 0 && data[index + 1] == 0 &&
                data[index + 2] == 1)
        {
            return 1;
        }
        if (index + 4 < bytes && data[index] == 0 && data[index + 1] == 0 &&
                data[index + 2] == 0 && data[index + 3] == 1)
        {
            return 1;
        }
    }
    return 0;
}

/*****************************************************************************/
static int
ohos_avcodec_append_bytes(char *dst, int dst_capacity, int *dst_bytes,
                          const unsigned char *src, int src_bytes)
{
    if (src_bytes <= 0)
    {
        return 0;
    }
    if (src == NULL || dst == NULL || dst_bytes == NULL ||
            *dst_bytes < 0 || *dst_bytes + src_bytes > dst_capacity)
    {
        return 1;
    }
    g_memcpy(dst + *dst_bytes, src, src_bytes);
    *dst_bytes += src_bytes;
    return 0;
}

/*****************************************************************************/
static int
ohos_avcodec_append_start_code(char *dst, int dst_capacity, int *dst_bytes)
{
    static const unsigned char start_code[] = { 0, 0, 0, 1 };

    return ohos_avcodec_append_bytes(dst, dst_capacity, dst_bytes,
                                     start_code, sizeof(start_code));
}

/*****************************************************************************/
static int
ohos_avcodec_append_avcc_payload(char *dst, int dst_capacity, int *dst_bytes,
                                 const unsigned char *src, int src_bytes)
{
    int offset;
    int sps_count;
    int pps_count;
    int index;
    int nal_bytes;

    if (src == NULL || src_bytes < 7 || src[0] != 1)
    {
        return 1;
    }

    offset = 5;
    sps_count = src[offset++] & 0x1f;
    for (index = 0; index < sps_count; ++index)
    {
        if (offset + 2 > src_bytes)
        {
            return 1;
        }
        nal_bytes = ((int)src[offset] << 8) | (int)src[offset + 1];
        offset += 2;
        if (nal_bytes <= 0 || offset + nal_bytes > src_bytes)
        {
            return 1;
        }
        if (ohos_avcodec_append_start_code(dst, dst_capacity, dst_bytes) != 0 ||
                ohos_avcodec_append_bytes(dst, dst_capacity, dst_bytes,
                                          src + offset, nal_bytes) != 0)
        {
            return 1;
        }
        offset += nal_bytes;
    }
    if (offset >= src_bytes)
    {
        return 0;
    }
    pps_count = src[offset++];
    for (index = 0; index < pps_count; ++index)
    {
        if (offset + 2 > src_bytes)
        {
            return 1;
        }
        nal_bytes = ((int)src[offset] << 8) | (int)src[offset + 1];
        offset += 2;
        if (nal_bytes <= 0 || offset + nal_bytes > src_bytes)
        {
            return 1;
        }
        if (ohos_avcodec_append_start_code(dst, dst_capacity, dst_bytes) != 0 ||
                ohos_avcodec_append_bytes(dst, dst_capacity, dst_bytes,
                                          src + offset, nal_bytes) != 0)
        {
            return 1;
        }
        offset += nal_bytes;
    }
    return 0;
}

/*****************************************************************************/
static int
ohos_avcodec_append_length_prefixed_payload(char *dst, int dst_capacity,
                                            int *dst_bytes,
                                            const unsigned char *src,
                                            int src_bytes,
                                            int length_bytes)
{
    int offset;
    int nal_bytes;

    if (src == NULL || src_bytes <= length_bytes || length_bytes != 4)
    {
        return 1;
    }

    offset = 0;
    while (offset + length_bytes < src_bytes)
    {
        nal_bytes = (int)ohos_avcodec_read_be32(src + offset);
        offset += length_bytes;
        if (nal_bytes <= 0 || nal_bytes > src_bytes - offset)
        {
            return 1;
        }
        if (ohos_avcodec_append_start_code(dst, dst_capacity, dst_bytes) != 0 ||
                ohos_avcodec_append_bytes(dst, dst_capacity, dst_bytes,
                                          src + offset, nal_bytes) != 0)
        {
            return 1;
        }
        offset += nal_bytes;
    }
    return offset == src_bytes ? 0 : 1;
}

/*****************************************************************************/
static int
ohos_avcodec_append_h264_payload(char *dst, int dst_capacity, int *dst_bytes,
                                 const unsigned char *src, int src_bytes)
{
    if (src == NULL || src_bytes <= 0)
    {
        return 0;
    }

    if (ohos_avcodec_has_start_code(src, src_bytes))
    {
        return ohos_avcodec_append_bytes(dst, dst_capacity, dst_bytes,
                                         src, src_bytes);
    }
    if (src_bytes > 7 && src[0] == 1 &&
            ohos_avcodec_append_avcc_payload(dst, dst_capacity, dst_bytes,
                                             src, src_bytes) == 0)
    {
        return 0;
    }
    if (ohos_avcodec_append_length_prefixed_payload(
                dst, dst_capacity, dst_bytes, src, src_bytes, 4) == 0)
    {
        return 0;
    }

    return ohos_avcodec_append_bytes(dst, dst_capacity, dst_bytes,
                                     src, src_bytes);
}

/*****************************************************************************/
static int
ohos_avcodec_store_codec_config(struct ohos_avcodec_encoder *oe,
                                const unsigned char *src, int src_bytes)
{
    char *buffer;
    int bytes;
    int capacity;

    if (oe == NULL || src == NULL || src_bytes <= 0)
    {
        return 0;
    }

    capacity = src_bytes + 64;
    buffer = g_new(char, capacity);
    if (buffer == NULL)
    {
        return 1;
    }
    bytes = 0;
    if (ohos_avcodec_append_h264_payload(buffer, capacity, &bytes,
                                         src, src_bytes) != 0)
    {
        g_free(buffer);
        return 1;
    }

    g_free(oe->codec_config);
    oe->codec_config = buffer;
    oe->codec_config_bytes = bytes;
    oe->codec_config_capacity = capacity;
    LOG(LOG_LEVEL_INFO,
        "xrdp_encoder_ohos_avcodec: stored H264 parameter sets bytes=%d",
        bytes);
    return 0;
}

/*****************************************************************************/
static int
ohos_avcodec_select_hardware_encoder(char *name, int name_bytes)
{
    OH_AVCapability *capability;
    const char *codec_name;

    if (name == NULL || name_bytes <= 0)
    {
        return 1;
    }
    name[0] = '\0';

    capability = OH_AVCodec_GetCapabilityByCategory(
                     OH_AVCODEC_MIMETYPE_VIDEO_AVC, true, HARDWARE);
    if (capability == NULL)
    {
        return 1;
    }

    codec_name = OH_AVCapability_GetName(capability);
    if (codec_name == NULL || codec_name[0] == '\0')
    {
        return 1;
    }

    g_snprintf(name, name_bytes, "%s", codec_name);
    return 0;
}

/*****************************************************************************/
static int
ohos_avcodec_select_pixel_format(OH_AVCapability *capability)
{
    const int32_t *pixel_formats;
    uint32_t pixel_format_count;
    uint32_t index;
    int has_nv12;
    int has_nv21;

    has_nv12 = 0;
    has_nv21 = 0;
    pixel_formats = NULL;
    pixel_format_count = 0;
    if (capability != NULL &&
            OH_AVCapability_GetVideoSupportedPixelFormats(
                capability, &pixel_formats, &pixel_format_count) == AV_ERR_OK &&
            pixel_formats != NULL)
    {
        for (index = 0; index < pixel_format_count; ++index)
        {
            if (pixel_formats[index] == AV_PIXEL_FORMAT_NV12)
            {
                has_nv12 = 1;
            }
            else if (pixel_formats[index] == AV_PIXEL_FORMAT_NV21)
            {
                has_nv21 = 1;
            }
        }
    }

    if (has_nv12)
    {
        return AV_PIXEL_FORMAT_NV12;
    }
    if (has_nv21)
    {
        return AV_PIXEL_FORMAT_NV21;
    }
    return AV_PIXEL_FORMAT_NV12;
}

/*****************************************************************************/
static int
ohos_avcodec_select_framerate(OH_AVCapability *capability, int width,
                              int height, int requested)
{
    static const int fallbacks[] = { 60, 45, 30, 24, 15 };
    int index;

    requested = requested <= 0 ? OHOS_AVCODEC_DEFAULT_FRAMERATE : requested;
    if (capability == NULL ||
            OH_AVCapability_AreVideoSizeAndFrameRateSupported(
                capability, width, height, requested))
    {
        return requested;
    }

    for (index = 0; index < (int)(sizeof(fallbacks) / sizeof(fallbacks[0]));
            ++index)
    {
        if (OH_AVCapability_AreVideoSizeAndFrameRateSupported(
                    capability, width, height, fallbacks[index]))
        {
            return fallbacks[index];
        }
    }
    return requested;
}

/*****************************************************************************/
static int
ohos_avcodec_select_bitrate(OH_AVCapability *capability, int requested,
                            int max_requested,
                            struct ohos_avcodec_bitrate_decision *decision)
{
    OH_AVRange range;
    OH_AVErrCode range_rc;
    int bitrate;
    int candidate;

    range.minVal = 0;
    range.maxVal = 0;
    if (decision != NULL)
    {
        g_memset(decision, 0, sizeof(struct ohos_avcodec_bitrate_decision));
        decision->requested = requested;
        decision->max_requested = max_requested;
        decision->range_rc = -1;
    }

    bitrate = requested > 0 ? requested : OHOS_AVCODEC_DEFAULT_BITRATE;
    if (max_requested > 0 && max_requested < bitrate)
    {
        bitrate = max_requested;
    }
    candidate = bitrate;

    if (decision != NULL)
    {
        decision->candidate = candidate;
    }

    if (capability != NULL)
    {
        range_rc = OH_AVCapability_GetEncoderBitrateRange(capability, &range);
        if (decision != NULL)
        {
            decision->range_rc = range_rc;
            decision->range_min = range.minVal;
            decision->range_max = range.maxVal;
        }
        if (range_rc == AV_ERR_OK)
        {
            if (decision != NULL)
            {
                decision->range_valid = 1;
            }
            bitrate = ohos_avcodec_clamp_int(bitrate, range.minVal,
                                             range.maxVal);
        }
    }

    if (decision != NULL)
    {
        decision->final_bitrate = bitrate;
        decision->clamped = bitrate != candidate;
    }
    return bitrate;
}

/*****************************************************************************/
static void
ohos_avcodec_close_encoder(struct ohos_avcodec_encoder *oe)
{
    if (oe == NULL)
    {
        return;
    }
    if (oe->codec != NULL)
    {
        if (oe->started)
        {
            OH_VideoEncoder_Stop(oe->codec);
        }
        OH_VideoEncoder_Destroy(oe->codec);
        oe->codec = NULL;
    }
    g_free(oe->codec_config);
    oe->codec_config = NULL;
    oe->codec_config_bytes = 0;
    oe->codec_config_capacity = 0;
    oe->width = 0;
    oe->height = 0;
    oe->input_stride = 0;
    oe->input_slice_height = 0;
    oe->input_pixel_format = AV_PIXEL_FORMAT_NV12;
    oe->frame_rate = 0;
    oe->bitrate = 0;
    oe->started = 0;
    oe->pts = 0;
    oe->codec_name[0] = '\0';
}

/*****************************************************************************/
static void
ohos_avcodec_update_input_description(struct ohos_avcodec_encoder *oe,
                                      const char *reason)
{
    OH_AVFormat *description;
    int32_t value;

    if (oe == NULL || oe->codec == NULL)
    {
        return;
    }

    description = OH_VideoEncoder_GetInputDescription(oe->codec);
    if (description == NULL)
    {
        return;
    }

    value = 0;
    if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_VIDEO_STRIDE, &value) &&
            value > 0)
    {
        oe->input_stride = value;
    }
    value = 0;
    if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_VIDEO_SLICE_HEIGHT,
                                &value) && value > 0)
    {
        oe->input_slice_height = value;
    }
    value = 0;
    if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_PIXEL_FORMAT, &value) &&
            value > 0)
    {
        oe->input_pixel_format = value;
    }

    OH_AVFormat_Destroy(description);

    oe->input_stride = ohos_avcodec_max_int(oe->input_stride, oe->width);
    oe->input_slice_height =
        ohos_avcodec_max_int(oe->input_slice_height, oe->height);
    LOG(LOG_LEVEL_INFO,
        "xrdp_encoder_ohos_avcodec: input description after %s stride=%d slice=%d pixelFormat=%d",
        reason == NULL ? "unknown" : reason,
        oe->input_stride, oe->input_slice_height, oe->input_pixel_format);
}

/*****************************************************************************/
static void
ohos_avcodec_update_output_description(struct ohos_avcodec_encoder *oe,
                                       const char *reason)
{
    OH_AVFormat *description;
    uint8_t *codec_config;
    size_t codec_config_bytes;

    if (oe == NULL || oe->codec == NULL)
    {
        return;
    }

    description = OH_VideoEncoder_GetOutputDescription(oe->codec);
    if (description == NULL)
    {
        return;
    }
    codec_config = NULL;
    codec_config_bytes = 0;
    if (OH_AVFormat_GetBuffer(description, OH_MD_KEY_CODEC_CONFIG,
                              &codec_config, &codec_config_bytes) &&
            codec_config != NULL && codec_config_bytes > 0)
    {
        ohos_avcodec_store_codec_config(oe, codec_config,
                                        (int)codec_config_bytes);
    }
    OH_AVFormat_Destroy(description);
    LOG_DEVEL(LOG_LEVEL_INFO,
              "xrdp_encoder_ohos_avcodec: output description after %s",
              reason == NULL ? "unknown" : reason);
}

/*****************************************************************************/
static void
ohos_avcodec_request_i_frame(struct ohos_avcodec_encoder *oe)
{
    OH_AVFormat *format;

    if (oe == NULL || oe->codec == NULL || !oe->started)
    {
        return;
    }

    format = OH_AVFormat_Create();
    if (format == NULL)
    {
        return;
    }
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_REQUEST_I_FRAME, 1);
    OH_VideoEncoder_SetParameter(oe->codec, format);
    OH_AVFormat_Destroy(format);
}

/*****************************************************************************/
static int
ohos_avcodec_configure_encoder(struct ohos_avcodec_global *og,
                               struct ohos_avcodec_encoder *oe,
                               int width, int height, int connection_type)
{
    OH_AVCapability *capability;
    OH_AVFormat *format;
    OH_AVCodec *codec;
    OH_AVErrCode rc;
    const char *codec_name;
    int pixel_format;
    int frame_rate;
    int bitrate;
    int max_bitrate;
    int ct;
    bool is_valid;
    struct ohos_avcodec_bitrate_decision bitrate_decision;

    ct = connection_type;
    if (ct > CONNECTION_TYPE_LAN || ct < CONNECTION_TYPE_MODEM)
    {
        ct = CONNECTION_TYPE_LAN;
    }

    capability = OH_AVCodec_GetCapabilityByCategory(
                     OH_AVCODEC_MIMETYPE_VIDEO_AVC, true, HARDWARE);
    if (capability == NULL)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: no hardware AVC encoder capability");
        return 1;
    }

    codec_name = OH_AVCapability_GetName(capability);
    if (codec_name == NULL || codec_name[0] == '\0')
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: hardware AVC encoder has no name");
        return 1;
    }

    pixel_format = ohos_avcodec_select_pixel_format(capability);
    frame_rate = ohos_avcodec_select_framerate(
                     capability, width, height,
                     (int)(og->openh264_param[ct].MaxFrameRate + 0.5f));
    bitrate = ohos_avcodec_select_bitrate(
                  capability,
                  og->openh264_param[ct].TargetBitrate,
                  og->openh264_param[ct].MaxBitrate,
                  &bitrate_decision);
    max_bitrate = og->openh264_param[ct].MaxBitrate > 0 ?
                  og->openh264_param[ct].MaxBitrate : bitrate;

    LOG(LOG_LEVEL_INFO,
        "xrdp_encoder_ohos_avcodec: bitrate decision size=%dx%d connection=%s(%d->%d) configTarget=%d configMax=%d configMaxFps=%.3f selectedFps=%d candidate=%d capabilityRangeValid=%d capabilityRangeRc=%d capabilityMin=%d capabilityMax=%d final=%d maxBitrate=%d clamped=%d",
        width, height,
        ohos_avcodec_connection_type_name(connection_type),
        connection_type, ct,
        og->openh264_param[ct].TargetBitrate,
        og->openh264_param[ct].MaxBitrate,
        og->openh264_param[ct].MaxFrameRate,
        frame_rate,
        bitrate_decision.candidate,
        bitrate_decision.range_valid,
        bitrate_decision.range_rc,
        bitrate_decision.range_min,
        bitrate_decision.range_max,
        bitrate_decision.final_bitrate,
        max_bitrate,
        bitrate_decision.clamped);
    if (bitrate_decision.clamped &&
            bitrate_decision.candidate >= 1000000 &&
            bitrate_decision.final_bitrate < bitrate_decision.candidate / 4)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp_encoder_ohos_avcodec: bitrate was heavily clamped candidate=%d final=%d range=%d..%d; if the range is reported in kbps, the encoder is being configured too low for %dx%d",
            bitrate_decision.candidate,
            bitrate_decision.final_bitrate,
            bitrate_decision.range_min,
            bitrate_decision.range_max,
            width, height);
    }

    codec = OH_VideoEncoder_CreateByName(codec_name);
    if (codec == NULL)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: create encoder failed name=%s",
            codec_name);
        return 1;
    }
    is_valid = false;
    rc = OH_VideoEncoder_IsValid(codec, &is_valid);
    if (rc != AV_ERR_OK || !is_valid)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: encoder invalid name=%s rc=%d valid=%d",
            codec_name, rc, is_valid ? 1 : 0);
        OH_VideoEncoder_Destroy(codec);
        return 1;
    }

    format = OH_AVFormat_CreateVideoFormat(OH_AVCODEC_MIMETYPE_VIDEO_AVC,
                                           width, height);
    if (format == NULL)
    {
        OH_VideoEncoder_Destroy(codec);
        return 1;
    }

    OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, pixel_format);
    OH_AVFormat_SetLongValue(format, OH_MD_KEY_BITRATE, bitrate);
    OH_AVFormat_SetLongValue(format, OH_MD_KEY_MAX_BITRATE, max_bitrate);
    OH_AVFormat_SetDoubleValue(format, OH_MD_KEY_FRAME_RATE, frame_rate);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE,
                            BITRATE_MODE_CBR);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_PROFILE, AVC_PROFILE_BASELINE);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_I_FRAME_INTERVAL, 1000);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_ENABLE_SYNC_MODE, 1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENABLE_LOW_LATENCY, 1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODER_ENABLE_B_FRAME, 0);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODER_ENABLE_PTS_BASED_RATECONTROL,
                            1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_RANGE_FLAG, 1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_COLOR_PRIMARIES,
                            COLOR_PRIMARY_BT709);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_TRANSFER_CHARACTERISTICS,
                            TRANSFER_CHARACTERISTIC_BT709);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_MATRIX_COEFFICIENTS,
                            MATRIX_COEFFICIENT_BT709);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_MAX_INPUT_SIZE,
                            width * height * 3 / 2);

    rc = OH_VideoEncoder_Configure(codec, format);
    OH_AVFormat_Destroy(format);
    if (rc != AV_ERR_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: configure failed name=%s rc=%d size=%dx%d pixelFormat=%d",
            codec_name, rc, width, height, pixel_format);
        OH_VideoEncoder_Destroy(codec);
        return 1;
    }

    rc = OH_VideoEncoder_Prepare(codec);
    if (rc != AV_ERR_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: prepare failed name=%s rc=%d",
            codec_name, rc);
        OH_VideoEncoder_Destroy(codec);
        return 1;
    }

    rc = OH_VideoEncoder_Start(codec);
    if (rc != AV_ERR_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: start failed name=%s rc=%d",
            codec_name, rc);
        OH_VideoEncoder_Destroy(codec);
        return 1;
    }

    oe->codec = codec;
    oe->width = width;
    oe->height = height;
    oe->input_stride = width;
    oe->input_slice_height = height;
    oe->input_pixel_format = pixel_format;
    oe->frame_rate = frame_rate;
    oe->bitrate = bitrate;
    oe->started = 1;
    oe->pts = 0;
    g_snprintf(oe->codec_name, sizeof(oe->codec_name), "%s", codec_name);

    ohos_avcodec_update_input_description(oe, "start");
    ohos_avcodec_update_output_description(oe, "start");
    ohos_avcodec_request_i_frame(oe);

    LOG(LOG_LEVEL_INFO,
        "xrdp_encoder_ohos_avcodec: hardware H264 encoder ready name=%s size=%dx%d fps=%d bitrate=%d pixelFormat=%d range=full color=bt709 transfer=bt709 matrix=bt709",
        oe->codec_name, width, height, frame_rate, bitrate, pixel_format);
    return 0;
}

/*****************************************************************************/
static int
ohos_avcodec_copy_nv12_input(struct ohos_avcodec_encoder *oe,
                             uint8_t *dst, int dst_capacity,
                             const char *src, int width, int height,
                             int twidth, int theight)
{
    const uint8_t *src_y;
    const uint8_t *src_uv;
    uint8_t *dst_y;
    uint8_t *dst_uv;
    int src_uv_offset;
    int dst_uv_offset;
    int needed;
    int row;
    int col;

    if (oe == NULL || dst == NULL || src == NULL ||
            width <= 0 || height <= 0 || twidth < width || theight < height ||
            (width & 1) != 0 || (height & 1) != 0)
    {
        return 1;
    }

    needed = oe->input_stride * oe->input_slice_height +
             oe->input_stride * (height / 2);
    if (dst_capacity < needed)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: input buffer too small capacity=%d required=%d stride=%d slice=%d",
            dst_capacity, needed, oe->input_stride, oe->input_slice_height);
        return 1;
    }

    g_memset(dst, 0, needed);
    src_y = (const uint8_t *)src;
    src_uv_offset = twidth * theight;
    src_uv = (const uint8_t *)src + src_uv_offset;
    dst_y = dst;
    dst_uv_offset = oe->input_stride * oe->input_slice_height;
    dst_uv = dst + dst_uv_offset;

    for (row = 0; row < height; ++row)
    {
        g_memcpy(dst_y + row * oe->input_stride,
                 src_y + row * twidth, width);
    }

    if (oe->input_pixel_format == AV_PIXEL_FORMAT_NV21)
    {
        for (row = 0; row < height / 2; ++row)
        {
            const uint8_t *src_row = src_uv + row * twidth;
            uint8_t *dst_row = dst_uv + row * oe->input_stride;
            for (col = 0; col < width; col += 2)
            {
                dst_row[col] = src_row[col + 1];
                dst_row[col + 1] = src_row[col];
            }
        }
    }
    else
    {
        for (row = 0; row < height / 2; ++row)
        {
            g_memcpy(dst_uv + row * oe->input_stride,
                     src_uv + row * twidth, width);
        }
    }
    return needed;
}

/*****************************************************************************/
static int
ohos_avcodec_drain_output(struct ohos_avcodec_encoder *oe,
                          char *cdata, int cdata_capacity,
                          int *cdata_bytes)
{
    OH_AVErrCode rc;
    OH_AVBuffer *output;
    OH_AVCodecBufferAttr attr;
    uint8_t *addr;
    int capacity;
    int payload_offset;
    int payload_bytes;
    int waited_us;
    int timeout_us;
    int attempt;
    uint32_t output_index;

    if (oe == NULL || oe->codec == NULL || cdata == NULL ||
            cdata_bytes == NULL || cdata_capacity <= 0)
    {
        return 1;
    }

    *cdata_bytes = 0;
    waited_us = 0;
    for (attempt = 0;
            attempt < OHOS_AVCODEC_MAX_OUTPUT_ATTEMPTS &&
            waited_us < OHOS_AVCODEC_OUTPUT_DEADLINE_US;
            ++attempt)
    {
        output_index = 0;
        timeout_us = attempt == 0 ? OHOS_AVCODEC_OUTPUT_TIMEOUT_US :
                     OHOS_AVCODEC_FOLLOWUP_OUTPUT_TIMEOUT_US;
        timeout_us = ohos_avcodec_min_int(
                         timeout_us,
                         OHOS_AVCODEC_OUTPUT_DEADLINE_US - waited_us);

        rc = OH_VideoEncoder_QueryOutputBuffer(
                 oe->codec, &output_index, timeout_us);
        if (rc == AV_ERR_STREAM_CHANGED)
        {
            ohos_avcodec_update_output_description(oe, "stream-changed");
            continue;
        }
        if (rc == AV_ERR_TRY_AGAIN_LATER)
        {
            waited_us += timeout_us;
            continue;
        }
        if (rc != AV_ERR_OK)
        {
            LOG(LOG_LEVEL_ERROR,
                "xrdp_encoder_ohos_avcodec: query output failed rc=%d frame=%llu",
                rc, (unsigned long long)oe->encode_calls);
            return 1;
        }

        output = OH_VideoEncoder_GetOutputBuffer(oe->codec, output_index);
        g_memset(&attr, 0, sizeof(attr));
        if (output == NULL ||
                OH_AVBuffer_GetBufferAttr(output, &attr) != AV_ERR_OK)
        {
            OH_VideoEncoder_FreeOutputBuffer(oe->codec, output_index);
            LOG(LOG_LEVEL_ERROR,
                "xrdp_encoder_ohos_avcodec: output buffer invalid frame=%llu",
                (unsigned long long)oe->encode_calls);
            return 1;
        }

        addr = OH_AVBuffer_GetAddr(output);
        capacity = OH_AVBuffer_GetCapacity(output);
        payload_offset = attr.offset;
        payload_bytes = attr.size;
        if (addr == NULL || capacity < 0 || payload_offset < 0 ||
                payload_bytes < 0 || payload_offset + payload_bytes > capacity)
        {
            OH_VideoEncoder_FreeOutputBuffer(oe->codec, output_index);
            LOG(LOG_LEVEL_ERROR,
                "xrdp_encoder_ohos_avcodec: output payload invalid capacity=%d offset=%d size=%d flags=0x%x",
                capacity, payload_offset, payload_bytes, attr.flags);
            return 1;
        }

        if ((attr.flags & AVCODEC_BUFFER_FLAGS_CODEC_DATA) != 0)
        {
            ohos_avcodec_store_codec_config(oe, addr + payload_offset,
                                            payload_bytes);
            OH_VideoEncoder_FreeOutputBuffer(oe->codec, output_index);
            continue;
        }

        if ((attr.flags & AVCODEC_BUFFER_FLAGS_SYNC_FRAME) != 0 &&
                oe->codec_config != NULL && oe->codec_config_bytes > 0)
        {
            if (ohos_avcodec_append_bytes(cdata, cdata_capacity, cdata_bytes,
                                          (unsigned char *)oe->codec_config,
                                          oe->codec_config_bytes) != 0)
            {
                OH_VideoEncoder_FreeOutputBuffer(oe->codec, output_index);
                LOG(LOG_LEVEL_ERROR,
                    "xrdp_encoder_ohos_avcodec: no room for codec config bytes=%d capacity=%d",
                    oe->codec_config_bytes, cdata_capacity);
                return 1;
            }
        }

        if (ohos_avcodec_append_h264_payload(
                    cdata, cdata_capacity, cdata_bytes,
                    addr + payload_offset, payload_bytes) != 0)
        {
            OH_VideoEncoder_FreeOutputBuffer(oe->codec, output_index);
            LOG(LOG_LEVEL_ERROR,
                "xrdp_encoder_ohos_avcodec: no room for frame payload bytes=%d capacity=%d written=%d",
                payload_bytes, cdata_capacity, *cdata_bytes);
            return 1;
        }

        OH_VideoEncoder_FreeOutputBuffer(oe->codec, output_index);
        if ((attr.flags & AVCODEC_BUFFER_FLAGS_INCOMPLETE_FRAME) == 0 &&
                *cdata_bytes > 0)
        {
            oe->output_frames++;
            return 0;
        }
    }

    oe->no_output_frames++;
    if (ohos_avcodec_should_log(oe->no_output_frames))
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp_encoder_ohos_avcodec: no output frame=%llu waitedUs=%d noOutput=%llu",
            (unsigned long long)oe->encode_calls, waited_us,
            (unsigned long long)oe->no_output_frames);
    }
    return 1;
}

/*****************************************************************************/
void *
xrdp_encoder_ohos_avcodec_create(void)
{
    struct ohos_avcodec_global *og;
    struct xrdp_tconfig_gfx gfxconfig;
    char gfx_config_path[256];
    int index;
    int rv;

    LOG_DEVEL(LOG_LEVEL_TRACE, "xrdp_encoder_ohos_avcodec_create:");
    og = g_new0(struct ohos_avcodec_global, 1);
    if (og == NULL)
    {
        return NULL;
    }
    g_memset(&gfxconfig, 0, sizeof(gfxconfig));
    xrdp_make_runtime_path(gfx_config_path, sizeof(gfx_config_path),
                           "XRDP_CFG_PATH", XRDP_CFG_PATH, "gfx.toml");
    rv = tconfig_load_gfx(gfx_config_path, &gfxconfig);
    if (rv != 0)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: failed to load GFX config %s rv=%d",
            gfx_config_path, rv);
        g_free(og);
        return NULL;
    }
    g_memcpy(&og->openh264_param, &gfxconfig.openh264_param,
             sizeof(struct xrdp_tconfig_gfx_openh264_param) *
             NUM_CONNECTION_TYPES);
    for (index = 0; index < NUM_CONNECTION_TYPES; ++index)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp_encoder_ohos_avcodec: gfx OpenH264 config connection=%s(%d) frameSkip=%d target=%d max=%d maxFps=%.3f",
            ohos_avcodec_connection_type_name(index),
            index,
            og->openh264_param[index].EnableFrameSkip,
            og->openh264_param[index].TargetBitrate,
            og->openh264_param[index].MaxBitrate,
            og->openh264_param[index].MaxFrameRate);
    }
    return og;
}

/*****************************************************************************/
int
xrdp_encoder_ohos_avcodec_delete(void *handle)
{
    struct ohos_avcodec_global *og;
    int index;

    if (handle == NULL)
    {
        return 0;
    }
    og = (struct ohos_avcodec_global *)handle;
    for (index = 0; index < OHOS_AVCODEC_MAX_ENCODERS; ++index)
    {
        ohos_avcodec_close_encoder(&(og->encoders[index]));
    }
    g_free(og);
    return 0;
}

/*****************************************************************************/
int
xrdp_encoder_ohos_avcodec_encode(void *handle, int session, int left, int top,
                                  int width, int height, int twidth,
                                  int theight, int format, const char *data,
                                  short *crects, int num_crects,
                                  char *cdata, int *cdata_bytes,
                                  int connection_type, int *flags_ptr)
{
    struct ohos_avcodec_global *og;
    struct ohos_avcodec_encoder *oe;
    OH_AVBuffer *input;
    OH_AVCodecBufferAttr attr;
    OH_AVErrCode rc;
    uint8_t *dst;
    int capacity;
    int input_bytes;
    uint32_t input_index;

    (void)left;
    (void)top;
    (void)format;
    (void)crects;
    (void)num_crects;

    if (flags_ptr != NULL)
    {
        *flags_ptr = 0;
    }
    if (handle == NULL || data == NULL || cdata == NULL ||
            cdata_bytes == NULL || width <= 0 || height <= 0 ||
            (width & 1) != 0 || (height & 1) != 0)
    {
        return 1;
    }

    og = (struct ohos_avcodec_global *)handle;
    oe = &(og->encoders[session % OHOS_AVCODEC_MAX_ENCODERS]);
    if (oe->codec == NULL || oe->width != width || oe->height != height)
    {
        ohos_avcodec_close_encoder(oe);
        if (ohos_avcodec_configure_encoder(
                    og, oe, width, height, connection_type) != 0)
        {
            return 1;
        }
    }

    oe->encode_calls++;
    input_index = 0;
    rc = OH_VideoEncoder_QueryInputBuffer(
             oe->codec, &input_index, OHOS_AVCODEC_INPUT_TIMEOUT_US);
    if (rc != AV_ERR_OK)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp_encoder_ohos_avcodec: input unavailable rc=%d frame=%llu",
            rc, (unsigned long long)oe->encode_calls);
        return 1;
    }

    input = OH_VideoEncoder_GetInputBuffer(oe->codec, input_index);
    dst = input == NULL ? NULL : OH_AVBuffer_GetAddr(input);
    capacity = input == NULL ? -1 : OH_AVBuffer_GetCapacity(input);
    if (dst == NULL || capacity <= 0)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: input buffer invalid capacity=%d frame=%llu",
            capacity, (unsigned long long)oe->encode_calls);
        return 1;
    }

    input_bytes = ohos_avcodec_copy_nv12_input(
                      oe, dst, capacity, data, width, height, twidth, theight);
    if (input_bytes <= 0)
    {
        return 1;
    }

    g_memset(&attr, 0, sizeof(attr));
    oe->pts += oe->frame_rate > 0 ? 1000000 / oe->frame_rate : 16666;
    attr.pts = oe->pts;
    attr.size = input_bytes;
    attr.offset = 0;
    attr.flags = AVCODEC_BUFFER_FLAGS_NONE;
    rc = OH_AVBuffer_SetBufferAttr(input, &attr);
    if (rc != AV_ERR_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: set input attr failed rc=%d frame=%llu",
            rc, (unsigned long long)oe->encode_calls);
        return 1;
    }

    rc = OH_VideoEncoder_PushInputBuffer(oe->codec, input_index);
    if (rc != AV_ERR_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp_encoder_ohos_avcodec: push input failed rc=%d frame=%llu",
            rc, (unsigned long long)oe->encode_calls);
        return 1;
    }

    if (ohos_avcodec_drain_output(oe, cdata, *cdata_bytes, cdata_bytes) != 0)
    {
        return 1;
    }

    if (ohos_avcodec_should_log(oe->output_frames))
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp_encoder_ohos_avcodec: encoded frame=%llu output=%llu bytes=%d size=%dx%d stride=%d slice=%d",
            (unsigned long long)oe->encode_calls,
            (unsigned long long)oe->output_frames,
            *cdata_bytes, width, height,
            oe->input_stride, oe->input_slice_height);
    }
    return 0;
}

/*****************************************************************************/
int
xrdp_encoder_ohos_avcodec_install_ok(void)
{
    char codec_name[128];
    OH_AVCodec *codec;
    OH_AVErrCode rc;
    bool is_valid;

    if (ohos_avcodec_select_hardware_encoder(
                codec_name, sizeof(codec_name)) != 0)
    {
        return 0;
    }

    codec = OH_VideoEncoder_CreateByName(codec_name);
    if (codec == NULL)
    {
        return 0;
    }
    is_valid = false;
    rc = OH_VideoEncoder_IsValid(codec, &is_valid);
    OH_VideoEncoder_Destroy(codec);
    if (rc != AV_ERR_OK || !is_valid)
    {
        return 0;
    }

    LOG(LOG_LEVEL_INFO,
        "xrdp_encoder_ohos_avcodec: hardware H264 encoder available name=%s",
        codec_name);
    return 1;
}
