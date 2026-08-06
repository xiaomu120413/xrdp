#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_rdpecam_internal.h"

#include "os_calls.h"
#include "parse.h"

#define OHOS_RDPECAM_SOFT_MAX_WIDTH 1920U
#define OHOS_RDPECAM_SOFT_MAX_HEIGHT 1080U
#define OHOS_RDPECAM_SOFT_MAX_FPS 30U
#define CAM_STREAM_SOURCE_COLOR 0x0001
#define CAM_STREAM_CATEGORY_CAPTURE 0x01

const char *
ohos_rdpecam_format_name(uint8_t format)
{
    switch (format)
    {
        case CAM_MEDIA_FORMAT_H264:
            return "H264";
        case CAM_MEDIA_FORMAT_MJPG:
            return "MJPG";
        case CAM_MEDIA_FORMAT_YUY2:
            return "YUY2";
        case CAM_MEDIA_FORMAT_NV12:
            return "NV12";
        case CAM_MEDIA_FORMAT_I420:
            return "I420";
        case CAM_MEDIA_FORMAT_RGB24:
            return "RGB24";
        case CAM_MEDIA_FORMAT_RGB32:
            return "RGB32";
        default:
            return "invalid";
    }
}

static int
ohos_rdpecam_media_rank(uint8_t format)
{
    switch (format)
    {
        case CAM_MEDIA_FORMAT_MJPG:
            return 7;
        case CAM_MEDIA_FORMAT_NV12:
            return 6;
        case CAM_MEDIA_FORMAT_YUY2:
            return 5;
        case CAM_MEDIA_FORMAT_I420:
            return 4;
        case CAM_MEDIA_FORMAT_RGB32:
            return 3;
        case CAM_MEDIA_FORMAT_RGB24:
            return 2;
        case CAM_MEDIA_FORMAT_H264:
            return 1;
        default:
            return 0;
    }
}

static int
ohos_rdpecam_media_valid(const struct ohos_rdpecam_media_type *media)
{
    return media != 0 && ohos_rdpecam_media_rank(media->format) > 0 &&
           (media->flags == 0x01 || media->flags == 0x02) &&
           media->width > 0 && media->height > 0 &&
           media->width <= XRDP_OHOS_FRAME_MAX_DIMENSION &&
           media->height <= XRDP_OHOS_FRAME_MAX_DIMENSION &&
           media->frame_rate_numerator > 0 &&
           media->frame_rate_denominator > 0;
}

static int
ohos_rdpecam_media_within_soft_limit(
    const struct ohos_rdpecam_media_type *media)
{
    uint64_t fps_scaled;
    if (!ohos_rdpecam_media_valid(media) ||
            media->width > OHOS_RDPECAM_SOFT_MAX_WIDTH ||
            media->height > OHOS_RDPECAM_SOFT_MAX_HEIGHT)
    {
        return 0;
    }
    fps_scaled = (uint64_t)media->frame_rate_numerator;
    return fps_scaled <=
           (uint64_t)OHOS_RDPECAM_SOFT_MAX_FPS *
           media->frame_rate_denominator;
}

static int
ohos_rdpecam_media_better(const struct ohos_rdpecam_media_type *candidate,
                          const struct ohos_rdpecam_media_type *selected)
{
    int candidate_soft;
    int selected_soft;
    int candidate_rank;
    int selected_rank;
    uint64_t candidate_area;
    uint64_t selected_area;
    if (!ohos_rdpecam_media_valid(candidate))
    {
        return 0;
    }
    if (!ohos_rdpecam_media_valid(selected))
    {
        return 1;
    }
    candidate_soft = ohos_rdpecam_media_within_soft_limit(candidate);
    selected_soft = ohos_rdpecam_media_within_soft_limit(selected);
    if (candidate_soft != selected_soft)
    {
        return candidate_soft > selected_soft;
    }
    candidate_rank = ohos_rdpecam_media_rank(candidate->format);
    selected_rank = ohos_rdpecam_media_rank(selected->format);
    if (candidate_rank != selected_rank)
    {
        return candidate_rank > selected_rank;
    }
    candidate_area = (uint64_t)candidate->width * candidate->height;
    selected_area = (uint64_t)selected->width * selected->height;
    return candidate_area > selected_area;
}

static int
ohos_rdpecam_read_media(struct stream *s,
                        struct ohos_rdpecam_media_type *media)
{
    int value;
    if (!s_check_rem(s, 26))
    {
        return 1;
    }
    g_memset(media, 0, sizeof(*media));
    in_uint8(s, value);
    media->format = (uint8_t)value;
    in_uint32_le(s, media->width);
    in_uint32_le(s, media->height);
    in_uint32_le(s, media->frame_rate_numerator);
    in_uint32_le(s, media->frame_rate_denominator);
    in_uint32_le(s, media->pixel_aspect_numerator);
    in_uint32_le(s, media->pixel_aspect_denominator);
    in_uint8(s, value);
    media->flags = (uint8_t)value;
    return ohos_rdpecam_media_valid(media) ? 0 : 1;
}

int
ohos_rdpecam_select_stream(struct stream *s, int *selected, int *offered)
{
    int index = 0;
    int selected_index = -1;
    if (s == 0 || selected == 0 || offered == 0 ||
            ((s->end - s->p) % 5) != 0 || !s_check_rem(s, 5))
    {
        return 1;
    }
    while (s_check_rem(s, 5))
    {
        int sources;
        int category;
        int ignored;
        in_uint16_le(s, sources);
        in_uint8(s, category);
        in_uint8(s, ignored);
        in_uint8(s, ignored);
        if (selected_index < 0 &&
                (sources & CAM_STREAM_SOURCE_COLOR) != 0 &&
                category == CAM_STREAM_CATEGORY_CAPTURE)
        {
            selected_index = index;
        }
        index++;
    }
    *selected = selected_index;
    *offered = index;
    return selected_index < 0 || selected_index > 255;
}

int
ohos_rdpecam_select_media(struct stream *s,
                          struct ohos_rdpecam_media_type *selected,
                          int *offered)
{
    struct ohos_rdpecam_media_type candidate;
    int count = 0;
    if (s == 0 || selected == 0 || offered == 0 ||
            ((s->end - s->p) % 26) != 0 || !s_check_rem(s, 26))
    {
        return 1;
    }
    g_memset(selected, 0, sizeof(*selected));
    while (s_check_rem(s, 26))
    {
        if (ohos_rdpecam_read_media(s, &candidate) != 0)
        {
            continue;
        }
        count++;
        if (ohos_rdpecam_media_better(&candidate, selected))
        {
            *selected = candidate;
        }
    }
    *offered = count;
    return !ohos_rdpecam_media_valid(selected);
}
