/*
 * OHOS system cursor icon loader.
 *
 * Built-in OHOS cursor styles are backed by the system mouse_icon assets.
 * Load those assets once and convert them to RDP cursor bitmaps.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "log.h"
#include "os_calls.h"
#include "string_calls.h"

#include <multimedia/image_framework/image/pixelmap_native.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ohos_cursor_system.h"

#define OHOS_CURSOR_ICON_DIR \
    "/system/etc/multimodalinput/mouse_icon/"
#define OHOS_CURSOR_ICON_URI_PREFIX "file://"
#define OHOS_CURSOR_ICON_TARGET_SIZE 32

#define OHOS_CURSOR_STYLE_TRANSPARENT (-101)

#define OHOS_CURSOR_ALIGN_SW 6
#define OHOS_CURSOR_ALIGN_NW 7
#define OHOS_CURSOR_ALIGN_CENTER 8
#define OHOS_CURSOR_ALIGN_NW_RIGHT 9

typedef struct OH_ImageSourceNative OH_ImageSourceNative;
typedef struct OH_DecodingOptions OH_DecodingOptions;

Image_ErrorCode
OH_DecodingOptions_Create(OH_DecodingOptions **options);

Image_ErrorCode
OH_DecodingOptions_SetPixelFormat(OH_DecodingOptions *options,
                                  int32_t pixel_format);

Image_ErrorCode
OH_DecodingOptions_Release(OH_DecodingOptions *options);

Image_ErrorCode
OH_ImageSourceNative_CreateFromUri(char *uri, size_t uri_size,
                                   OH_ImageSourceNative **source);

Image_ErrorCode
OH_ImageSourceNative_CreatePixelmap(OH_ImageSourceNative *source,
                                    OH_DecodingOptions *options,
                                    OH_PixelmapNative **pixelmap);

Image_ErrorCode
OH_ImageSourceNative_Release(OH_ImageSourceNative *source);

struct ohos_cursor_system_icon
{
    int style;
    int alignment;
    const char *file_name;
};

struct ohos_cursor_system_cache
{
    int state;
    struct ohos_cursor_image image;
};

static const struct ohos_cursor_system_icon g_ohos_cursor_system_icons[] =
{
    {0, OHOS_CURSOR_ALIGN_NW, "Default.svg"},
    {1, OHOS_CURSOR_ALIGN_CENTER, "East.svg"},
    {2, OHOS_CURSOR_ALIGN_CENTER, "West.svg"},
    {3, OHOS_CURSOR_ALIGN_CENTER, "South.svg"},
    {4, OHOS_CURSOR_ALIGN_CENTER, "North.svg"},
    {5, OHOS_CURSOR_ALIGN_CENTER, "West_East.svg"},
    {6, OHOS_CURSOR_ALIGN_CENTER, "North_South.svg"},
    {7, OHOS_CURSOR_ALIGN_CENTER, "North_East.svg"},
    {8, OHOS_CURSOR_ALIGN_CENTER, "North_West.svg"},
    {9, OHOS_CURSOR_ALIGN_CENTER, "South_East.svg"},
    {10, OHOS_CURSOR_ALIGN_CENTER, "South_West.svg"},
    {11, OHOS_CURSOR_ALIGN_CENTER, "North_East_South_West.svg"},
    {12, OHOS_CURSOR_ALIGN_CENTER, "North_West_South_East.svg"},
    {13, OHOS_CURSOR_ALIGN_CENTER, "Cross.svg"},
    {14, OHOS_CURSOR_ALIGN_NW, "Copy.svg"},
    {15, OHOS_CURSOR_ALIGN_NW, "Forbid.svg"},
    {16, OHOS_CURSOR_ALIGN_SW, "Colorsucker.svg"},
    {17, OHOS_CURSOR_ALIGN_CENTER, "Hand_Grabbing.svg"},
    {18, OHOS_CURSOR_ALIGN_CENTER, "Hand_Open.svg"},
    {19, OHOS_CURSOR_ALIGN_NW_RIGHT, "Hand_Pointing.svg"},
    {20, OHOS_CURSOR_ALIGN_NW, "Help.svg"},
    {21, OHOS_CURSOR_ALIGN_CENTER, "Move.svg"},
    {22, OHOS_CURSOR_ALIGN_CENTER, "Resize_Left_Right.svg"},
    {23, OHOS_CURSOR_ALIGN_CENTER, "Resize_Up_Down.svg"},
    {24, OHOS_CURSOR_ALIGN_CENTER, "Screenshot_Cross.svg"},
    {25, OHOS_CURSOR_ALIGN_CENTER, "Screenshot_Cursor.svg"},
    {26, OHOS_CURSOR_ALIGN_CENTER, "Text_Cursor.svg"},
    {27, OHOS_CURSOR_ALIGN_CENTER, "Zoom_In.svg"},
    {28, OHOS_CURSOR_ALIGN_CENTER, "Zoom_Out.svg"},
    {29, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_East.svg"},
    {30, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_West.svg"},
    {31, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_South.svg"},
    {32, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_North.svg"},
    {33, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_North_South.svg"},
    {34, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_North_East.svg"},
    {35, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_North_West.svg"},
    {36, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_South_East.svg"},
    {37, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_South_West.svg"},
    {38, OHOS_CURSOR_ALIGN_CENTER, "MID_Btn_North_South_West_East.svg"},
    {39, OHOS_CURSOR_ALIGN_CENTER, "Horizontal_Text_Cursor.svg"},
    {40, OHOS_CURSOR_ALIGN_CENTER, "Cursor_Cross.svg"},
    {41, OHOS_CURSOR_ALIGN_CENTER, "Cursor_Circle.png"},
    {42, OHOS_CURSOR_ALIGN_CENTER, "Loading.svg"},
    {43, OHOS_CURSOR_ALIGN_NW, "Loading_Left.svg"},
    {44, OHOS_CURSOR_ALIGN_CENTER, "MIDDLE_BTN_EAST_WEST.svg"},
    {45, OHOS_CURSOR_ALIGN_NW, "Loading_Left.svg"},
    {46, OHOS_CURSOR_ALIGN_CENTER, "Loading_Right.svg"},
    {47, OHOS_CURSOR_ALIGN_CENTER, "Custom_Cursor_Circle.svg"},
    {48, OHOS_CURSOR_ALIGN_CENTER, "ScreenRecorder_Cursor.svg"},
    {49, OHOS_CURSOR_ALIGN_CENTER, "Laser_Cursor.svg"},
    {50, OHOS_CURSOR_ALIGN_CENTER, "Laser_Cursor_Dot.svg"},
    {51, OHOS_CURSOR_ALIGN_CENTER, "Laser_Cursor_Dot_Red.svg"}
};

static struct ohos_cursor_system_cache
    g_ohos_cursor_system_cache[sizeof(g_ohos_cursor_system_icons) /
                               sizeof(g_ohos_cursor_system_icons[0])];

int
ohos_cursor_system_style_is_null(int style)
{
    return style == OHOS_CURSOR_STYLE_TRANSPARENT;
}

static int
ohos_cursor_system_find_icon(int style)
{
    int index;

    for (index = 0;
         index < (int)(sizeof(g_ohos_cursor_system_icons) /
                       sizeof(g_ohos_cursor_system_icons[0]));
         index++)
    {
        if (g_ohos_cursor_system_icons[index].style == style)
        {
            return index;
        }
    }
    return -1;
}

static void
ohos_cursor_system_apply_hotspot(struct ohos_cursor_image *image,
                                 int alignment)
{
    if (image == 0)
    {
        return;
    }

    switch (alignment)
    {
        case OHOS_CURSOR_ALIGN_CENTER:
            image->hot_x = image->width / 2;
            image->hot_y = image->height / 2;
            break;

        case OHOS_CURSOR_ALIGN_SW:
            image->hot_x = 0;
            image->hot_y = image->height - 1;
            break;

        case OHOS_CURSOR_ALIGN_NW_RIGHT:
            image->hot_x = (image->width * 5) / 33;
            image->hot_y = 0;
            break;

        case OHOS_CURSOR_ALIGN_NW:
        default:
            image->hot_x = 0;
            image->hot_y = 0;
            break;
    }
}

static int
ohos_cursor_system_source_to_pixelmap(OH_ImageSourceNative *source,
                                      OH_PixelmapNative **pixelmap)
{
    Image_ErrorCode rc;
    OH_DecodingOptions *options = 0;

    if (source == 0 || pixelmap == 0)
    {
        return 1;
    }

    *pixelmap = 0;
    rc = OH_DecodingOptions_Create(&options);
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_DecodingOptions_SetPixelFormat(options, PIXEL_FORMAT_BGRA_8888);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_ImageSourceNative_CreatePixelmap(source, options, pixelmap);
    }
    if (options != 0)
    {
        OH_DecodingOptions_Release(options);
    }
    if (rc != IMAGE_SUCCESS || *pixelmap == 0)
    {
        if (*pixelmap != 0)
        {
            OH_PixelmapNative_Release(*pixelmap);
            *pixelmap = 0;
        }
        return 1;
    }
    return 0;
}

static int
ohos_cursor_system_load_icon(const struct ohos_cursor_system_icon *icon,
                             struct ohos_cursor_image *image)
{
    char uri[256];
    Image_ErrorCode rc;
    OH_ImageSourceNative *source = 0;
    OH_PixelmapNative *pixelmap = 0;
    int rv;

    if (icon == 0 || image == 0 || icon->file_name == 0)
    {
        return 1;
    }

    if (snprintf(uri, sizeof(uri), "%s%s%s",
                 OHOS_CURSOR_ICON_URI_PREFIX, OHOS_CURSOR_ICON_DIR,
                 icon->file_name) >= (int)sizeof(uri))
    {
        return 1;
    }

    rc = OH_ImageSourceNative_CreateFromUri(uri, (size_t)g_strlen(uri),
                                            &source);
    if (rc != IMAGE_SUCCESS || source == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cursor: system icon source failed style=%d uri=%s rc=%d",
            icon->style, uri, rc);
        return 1;
    }

    rv = ohos_cursor_system_source_to_pixelmap(source, &pixelmap);
    if (rv == 0)
    {
        rv = ohos_cursor_image_from_pixelmap_target(pixelmap, icon->style,
                                                    OHOS_CURSOR_ICON_TARGET_SIZE,
                                                    OHOS_CURSOR_ICON_TARGET_SIZE,
                                                    image);
        if (rv == 0)
        {
            ohos_cursor_system_apply_hotspot(image, icon->alignment);
        }
    }

    if (pixelmap != 0)
    {
        OH_PixelmapNative_Release(pixelmap);
    }
    OH_ImageSourceNative_Release(source);

    if (rv != 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cursor: system icon decode failed style=%d file=%s",
            icon->style, icon->file_name);
    }
    return rv;
}

int
ohos_cursor_system_get_image(int style,
                             const struct ohos_cursor_image **image)
{
    int index;
    struct ohos_cursor_system_cache *cache;

    if (image == 0)
    {
        return 1;
    }
    *image = 0;

    index = ohos_cursor_system_find_icon(style);
    if (index < 0)
    {
        return 1;
    }

    cache = &g_ohos_cursor_system_cache[index];
    if (cache->state == 1)
    {
        *image = &cache->image;
        return 0;
    }
    if (cache->state < 0)
    {
        return 1;
    }

    ohos_cursor_image_init(&cache->image);
    if (ohos_cursor_system_load_icon(&g_ohos_cursor_system_icons[index],
                                     &cache->image) != 0)
    {
        ohos_cursor_image_deinit(&cache->image);
        cache->state = -1;
        return 1;
    }

    cache->state = 1;
    *image = &cache->image;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cursor: loaded system icon style=%d file=%s size=%dx%d hot=(%d,%d)",
        style, g_ohos_cursor_system_icons[index].file_name,
        cache->image.width, cache->image.height,
        cache->image.hot_x, cache->image.hot_y);
    return 0;
}
