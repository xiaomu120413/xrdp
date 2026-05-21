/*
 * HarmonyOS Pasteboard access for the xrdp OHOS cliprdr backend.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "log.h"
#include "os_calls.h"
#include "string_calls.h"

#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include <database/udmf/udmf.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/uds.h>

const char *
ohos_cliprdr_pasteboard_status_name(int status)
{
    switch (status)
    {
        case ERR_OK:
            return "ERR_OK";
        case ERR_PERMISSION_ERROR:
            return "ERR_PERMISSION_ERROR";
        case ERR_INVALID_PARAMETER:
            return "ERR_INVALID_PARAMETER";
        case ERR_DEVICE_NOT_SUPPORTED:
            return "ERR_DEVICE_NOT_SUPPORTED";
        case ERR_INNER_ERROR:
            return "ERR_INNER_ERROR";
        case ERR_BUSY:
            return "ERR_BUSY";
        case ERR_PASTEBOARD_GET_DATA_FAILED:
            return "ERR_PASTEBOARD_GET_DATA_FAILED";
        default:
            return "unknown";
    }
}

static OH_UdmfData *
ohos_cliprdr_get_pasteboard_data(struct ohos_cliprdr *cliprdr,
                                 const char *reason, int *status)
{
    OH_UdmfData *data;
    int rc = ERR_OK;

    if (status != 0)
    {
        *status = ERR_OK;
    }
    if (cliprdr == 0 || cliprdr->pasteboard == 0)
    {
        if (status != 0)
        {
            *status = ERR_INVALID_PARAMETER;
        }
        return 0;
    }

    data = OH_Pasteboard_GetData(cliprdr->pasteboard, &rc);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: Pasteboard GetData reason=%s status=%d(%s) data=%p",
        reason == 0 ? "" : reason, rc, ohos_cliprdr_pasteboard_status_name(rc),
        data);
    if (status != 0)
    {
        *status = rc;
    }
    return data;
}

int
ohos_cliprdr_pasteboard_read_plain_text(struct ohos_cliprdr *cliprdr,
                                        char **text)
{
    int status;
    int rc;
    int index;
    int record_count;
    OH_UdmfData *data;
    OH_UdsPlainText *primary;

    if (text == 0)
    {
        return 1;
    }
    *text = 0;
    if (cliprdr == 0 || cliprdr->pasteboard == 0)
    {
        return 1;
    }

    data = ohos_cliprdr_get_pasteboard_data(cliprdr, "read plain text",
                                            &status);
    if (status != ERR_OK || data == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: Pasteboard read text failed status=%d(%s)",
            status, ohos_cliprdr_pasteboard_status_name(status));
        cliprdr->errors++;
        return 1;
    }

    primary = OH_UdsPlainText_Create();
    if (primary != 0)
    {
        rc = OH_UdmfData_GetPrimaryPlainText(data, primary);
        if (rc == UDMF_E_OK)
        {
            const char *content = OH_UdsPlainText_GetContent(primary);
            if (content != 0 && content[0] != '\0')
            {
                *text = g_strdup(content);
            }
        }
        OH_UdsPlainText_Destroy(primary);
        if (*text != 0)
        {
            OH_UdmfData_Destroy(data);
            cliprdr->pasteboard_reads++;
            return 0;
        }
    }

    record_count = OH_UdmfData_GetRecordCount(data);
    for (index = 0; index < record_count; index++)
    {
        OH_UdmfRecord *record;
        OH_UdsPlainText *plain_text;

        record = OH_UdmfData_GetRecord(data, (unsigned int)index);
        if (record == 0)
        {
            continue;
        }
        plain_text = OH_UdsPlainText_Create();
        if (plain_text == 0)
        {
            continue;
        }
        rc = OH_UdmfRecord_GetPlainText(record, plain_text);
        if (rc == UDMF_E_OK)
        {
            const char *content = OH_UdsPlainText_GetContent(plain_text);
            if (content != 0 && content[0] != '\0')
            {
                *text = g_strdup(content);
            }
        }
        OH_UdsPlainText_Destroy(plain_text);
        if (*text != 0)
        {
            OH_UdmfData_Destroy(data);
            cliprdr->pasteboard_reads++;
            return 0;
        }
    }

    OH_UdmfData_Destroy(data);
    return 1;
}

int
ohos_cliprdr_pasteboard_write_plain_text(struct ohos_cliprdr *cliprdr,
                                         const char *text)
{
    int rc = UDMF_E_OK;
    OH_UdsPlainText *plain_text = 0;
    OH_UdmfRecord *record = 0;
    OH_UdmfData *data = 0;

    if (cliprdr == 0 || cliprdr->pasteboard == 0 || text == 0)
    {
        return 1;
    }

    plain_text = OH_UdsPlainText_Create();
    record = OH_UdmfRecord_Create();
    data = OH_UdmfData_Create();
    if (plain_text == 0 || record == 0 || data == 0)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp.ohos.cliprdr: UDMF allocation failed");
        goto fail;
    }

    rc = OH_UdsPlainText_SetContent(plain_text, text);
    if (rc == UDMF_E_OK)
    {
        rc = OH_UdmfRecord_AddPlainText(record, plain_text);
    }
    if (rc == UDMF_E_OK)
    {
        rc = OH_UdmfData_AddRecord(data, record);
    }
    if (rc != UDMF_E_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: UDMF plain text setup failed rc=%d", rc);
        goto fail;
    }

    if (ohos_cliprdr_lock(cliprdr) == 0)
    {
        cliprdr->ignore_local_changes++;
        cliprdr->ignore_local_changes_until =
            g_get_elapsed_ms() + OHOS_CLIPRDR_ECHO_SUPPRESS_MS;
        ohos_cliprdr_unlock(cliprdr);
    }

    rc = OH_Pasteboard_SetData(cliprdr->pasteboard, data);
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: Pasteboard SetData text bytes=%d status=%d(%s)",
        text == 0 ? 0 : (int)g_strlen(text),
        rc, ohos_cliprdr_pasteboard_status_name(rc));
    if (rc != ERR_OK)
    {
        if (ohos_cliprdr_lock(cliprdr) == 0)
        {
            if (cliprdr->ignore_local_changes > 0)
            {
                cliprdr->ignore_local_changes--;
            }
            if (cliprdr->ignore_local_changes == 0)
            {
                cliprdr->ignore_local_changes_until = 0;
            }
            ohos_cliprdr_unlock(cliprdr);
        }
        goto fail;
    }

    cliprdr->pasteboard_writes++;
    if (plain_text != 0)
    {
        OH_UdsPlainText_Destroy(plain_text);
    }
    if (record != 0)
    {
        OH_UdmfRecord_Destroy(record);
    }
    if (data != 0)
    {
        OH_UdmfData_Destroy(data);
    }
    return 0;

fail:
    cliprdr->errors++;
    if (plain_text != 0)
    {
        OH_UdsPlainText_Destroy(plain_text);
    }
    if (record != 0)
    {
        OH_UdmfRecord_Destroy(record);
    }
    if (data != 0)
    {
        OH_UdmfData_Destroy(data);
    }
    return 1;
}

static void
ohos_cliprdr_on_pasteboard_finalize(void *context)
{
    (void)context;
}

static void
ohos_cliprdr_on_pasteboard_changed(void *context, Pasteboard_NotifyType type)
{
    struct ohos_cliprdr *cliprdr = (struct ohos_cliprdr *)context;
    unsigned int now;
    int suppress = 0;

    if (cliprdr == 0 || type != NOTIFY_LOCAL_DATA_CHANGE)
    {
        return;
    }

    now = g_get_elapsed_ms();
    if (ohos_cliprdr_lock(cliprdr) != 0)
    {
        return;
    }
    cliprdr->pasteboard_changes++;
    if (cliprdr->ignore_local_changes > 0 &&
            now <= cliprdr->ignore_local_changes_until)
    {
        cliprdr->ignore_local_changes--;
        if (cliprdr->ignore_local_changes == 0)
        {
            cliprdr->ignore_local_changes_until = 0;
        }
        cliprdr->suppressed_changes++;
        suppress = 1;
    }
    else
    {
        cliprdr->ignore_local_changes = 0;
        cliprdr->ignore_local_changes_until = 0;
        cliprdr->local_change_pending = 1;
    }
    ohos_cliprdr_unlock(cliprdr);

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: Pasteboard changed suppress=%d", suppress);
    if (!suppress && cliprdr->wake_obj != 0)
    {
        g_set_wait_obj(cliprdr->wake_obj);
    }
}

int
ohos_cliprdr_pasteboard_init(struct ohos_cliprdr *cliprdr)
{
    int rc;

    if (cliprdr == 0)
    {
        return 1;
    }

    cliprdr->pasteboard = OH_Pasteboard_Create();
    if (cliprdr->pasteboard == 0)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: failed to create HarmonyOS Pasteboard");
        cliprdr->errors++;
        return 1;
    }

    cliprdr->observer = OH_PasteboardObserver_Create();
    if (cliprdr->observer == 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: Pasteboard observer unavailable");
        cliprdr->errors++;
        return 1;
    }

    rc = OH_PasteboardObserver_SetData(cliprdr->observer, cliprdr,
                                       ohos_cliprdr_on_pasteboard_changed,
                                       ohos_cliprdr_on_pasteboard_finalize);
    if (rc != ERR_OK)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: Pasteboard observer setup failed rc=%d", rc);
        cliprdr->errors++;
        return 1;
    }

    rc = OH_Pasteboard_Subscribe(cliprdr->pasteboard, NOTIFY_LOCAL_DATA_CHANGE,
                                 cliprdr->observer);
    if (rc == ERR_OK)
    {
        cliprdr->pasteboard_subscribed = 1;
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: Pasteboard observer subscribed");
    }
    else
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: Pasteboard subscribe failed rc=%d(%s)",
            rc, ohos_cliprdr_pasteboard_status_name(rc));
        cliprdr->errors++;
    }
    return 0;
}

void
ohos_cliprdr_pasteboard_deinit(struct ohos_cliprdr *cliprdr)
{
    if (cliprdr == 0)
    {
        return;
    }
    if (cliprdr->pasteboard != 0 && cliprdr->observer != 0 &&
            cliprdr->pasteboard_subscribed)
    {
        (void)OH_Pasteboard_Unsubscribe(cliprdr->pasteboard,
                                        NOTIFY_LOCAL_DATA_CHANGE,
                                        cliprdr->observer);
    }
    if (cliprdr->observer != 0)
    {
        (void)OH_PasteboardObserver_Destroy(cliprdr->observer);
        cliprdr->observer = 0;
    }
    if (cliprdr->pasteboard != 0)
    {
        OH_Pasteboard_Destroy(cliprdr->pasteboard);
        cliprdr->pasteboard = 0;
    }
    cliprdr->pasteboard_subscribed = 0;
}
