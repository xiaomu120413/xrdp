/*
 * HarmonyOS text clipboard backend for the xrdp OHOS module.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "log.h"
#include "ms-rdpbcgr.h"
#include "ms-rdpeclip.h"
#include "os_calls.h"
#include "string_calls.h"
#include "thread_calls.h"
#include "xup.h"

int
ohos_cliprdr_lock(struct ohos_cliprdr *cliprdr)
{
    if (cliprdr == 0 || cliprdr->lock == 0)
    {
        return 1;
    }
    return tc_mutex_lock(cliprdr->lock);
}

int
ohos_cliprdr_unlock(struct ohos_cliprdr *cliprdr)
{
    if (cliprdr == 0 || cliprdr->lock == 0)
    {
        return 1;
    }
    return tc_mutex_unlock(cliprdr->lock);
}

static void
ohos_cliprdr_reset_stats(struct ohos_cliprdr *cliprdr)
{
    cliprdr->local_format_lists_sent = 0;
    cliprdr->remote_format_lists_received = 0;
    cliprdr->local_requests_received = 0;
    cliprdr->remote_responses_received = 0;
    cliprdr->pasteboard_reads = 0;
    cliprdr->pasteboard_writes = 0;
    cliprdr->pasteboard_changes = 0;
    cliprdr->suppressed_changes = 0;
    cliprdr->errors = 0;
}

void
ohos_cliprdr_init(struct ohos_cliprdr *cliprdr, struct mod *mod,
                  tintptr wake_obj)
{
    if (cliprdr == 0)
    {
        return;
    }
    g_memset(cliprdr, 0, sizeof(struct ohos_cliprdr));
    cliprdr->mod = mod;
    cliprdr->channel_id = -1;
    cliprdr->capability_flags = CB_USE_LONG_FORMAT_NAMES |
                                 CB_STREAM_FILECLIP_ENABLED |
                                 CB_FILECLIP_NO_FILE_PATHS;
    cliprdr->remote_file_fd = -1;
    cliprdr->wake_obj = wake_obj;
    cliprdr->instance_id = ohos_cliprdr_next_instance_id();
    cliprdr->lock = tc_mutex_create();

    (void)ohos_cliprdr_pasteboard_init(cliprdr);
}

void
ohos_cliprdr_deinit(struct ohos_cliprdr *cliprdr)
{
    if (cliprdr == 0)
    {
        return;
    }
    ohos_cliprdr_pasteboard_deinit(cliprdr);
    ohos_cliprdr_file_transfer_reset(cliprdr);
    ohos_cliprdr_channel_reset(cliprdr);
    if (cliprdr->lock != 0)
    {
        tc_mutex_delete(cliprdr->lock);
    }
    g_memset(cliprdr, 0, sizeof(struct ohos_cliprdr));
    cliprdr->channel_id = -1;
}

int
ohos_cliprdr_connect(struct ohos_cliprdr *cliprdr)
{
    int rv;

    if (cliprdr == 0 || cliprdr->mod == 0)
    {
        return 1;
    }
    cliprdr->connected = 1;
    cliprdr->channel_ready = 0;
    cliprdr->remote_capability_flags = 0;
    cliprdr->remote_caps_received = 0;
    cliprdr->requested_format = 0;
    cliprdr->requested_kind = OHOS_CLIPRDR_REQUEST_NONE;
    cliprdr->pending_remote_format = 0;
    cliprdr->pending_remote_kind = OHOS_CLIPRDR_REQUEST_NONE;
    cliprdr->remote_text_format = 0;
    cliprdr->remote_html_format = 0;
    cliprdr->remote_uriw_format = 0;
    cliprdr->remote_uri_list_format = 0;
    cliprdr->remote_dib_format = 0;
    cliprdr->remote_dibv5_format = 0;
    cliprdr->remote_image_bmp_format = 0;
    cliprdr->remote_image_png_format = 0;
    cliprdr->remote_image_jpeg_format = 0;
    cliprdr->remote_image_webp_format = 0;
    cliprdr->remote_file_group_descriptor_format = 0;
    cliprdr->remote_file_contents_format = 0;
    ohos_cliprdr_file_transfer_reset(cliprdr);
    ohos_cliprdr_reset_stats(cliprdr);

    if (cliprdr->mod->server_chansrv_in_use != 0 &&
            cliprdr->mod->server_chansrv_in_use(cliprdr->mod))
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: chansrv owns clipboard channel");
        return 0;
    }
    if (cliprdr->mod->server_get_channel_id == 0 ||
            cliprdr->mod->server_send_to_channel == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: channel callbacks unavailable");
        return 0;
    }

    cliprdr->channel_id =
        cliprdr->mod->server_get_channel_id(cliprdr->mod,
                                            CLIPRDR_SVC_CHANNEL_NAME);
    if (cliprdr->channel_id < 0)
    {
        LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.cliprdr: cliprdr channel unavailable");
        return 0;
    }

    rv = ohos_cliprdr_send_capabilities(cliprdr);
    if (rv == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local caps sent flags=0x%8.8x",
            cliprdr->capability_flags);
        rv = ohos_cliprdr_send_monitor_ready(cliprdr);
    }
    if (rv == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: monitor ready sent");
    }
    if (rv == 0)
    {
        cliprdr->channel_ready = 1;
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: channel ready id=%d", cliprdr->channel_id);
        (void)ohos_cliprdr_send_local_format_list(cliprdr, "connect", 0);
    }
    else
    {
        cliprdr->errors++;
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: channel init failed id=%d rv=%d",
            cliprdr->channel_id, rv);
    }
    return rv;
}

void
ohos_cliprdr_disconnect(struct ohos_cliprdr *cliprdr)
{
    if (cliprdr == 0)
    {
        return;
    }
    cliprdr->connected = 0;
    cliprdr->channel_ready = 0;
    cliprdr->remote_capability_flags = 0;
    cliprdr->remote_caps_received = 0;
    cliprdr->channel_id = -1;
    cliprdr->requested_format = 0;
    cliprdr->requested_kind = OHOS_CLIPRDR_REQUEST_NONE;
    cliprdr->pending_remote_format = 0;
    cliprdr->pending_remote_kind = OHOS_CLIPRDR_REQUEST_NONE;
    cliprdr->remote_text_format = 0;
    cliprdr->remote_html_format = 0;
    cliprdr->remote_uriw_format = 0;
    cliprdr->remote_uri_list_format = 0;
    cliprdr->remote_dib_format = 0;
    cliprdr->remote_dibv5_format = 0;
    cliprdr->remote_image_bmp_format = 0;
    cliprdr->remote_image_png_format = 0;
    cliprdr->remote_image_jpeg_format = 0;
    cliprdr->remote_image_webp_format = 0;
    cliprdr->remote_file_group_descriptor_format = 0;
    cliprdr->remote_file_contents_format = 0;
    cliprdr->local_change_pending = 0;
    ohos_cliprdr_file_transfer_reset(cliprdr);
    ohos_cliprdr_channel_reset(cliprdr);
}

int
ohos_cliprdr_check_wait_objs(struct ohos_cliprdr *cliprdr)
{
    int send_local = 0;

    if (cliprdr == 0)
    {
        return 0;
    }
    if (ohos_cliprdr_lock(cliprdr) == 0)
    {
        send_local = cliprdr->local_change_pending;
        cliprdr->local_change_pending = 0;
        ohos_cliprdr_unlock(cliprdr);
    }

    if (send_local)
    {
        return ohos_cliprdr_send_local_format_list(cliprdr,
                                                   "pasteboard changed", 1);
    }
    return 0;
}
