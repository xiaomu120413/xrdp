/*
 * HarmonyOS Pasteboard rich text and URI access for cliprdr.
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

int
ohos_cliprdr_pasteboard_read_html(struct ohos_cliprdr *cliprdr,
                                  char **html, char **plain)
{
    int status;
    int rc;
    int index;
    int record_count;
    OH_UdmfData *data;
    OH_UdsHtml *primary;

    if (html == 0)
    {
        return 1;
    }
    *html = 0;
    if (plain != 0)
    {
        *plain = 0;
    }
    if (cliprdr == 0 || cliprdr->pasteboard == 0)
    {
        return 1;
    }

    data = ohos_cliprdr_pasteboard_get_data(cliprdr, "read html", &status);
    if (status != ERR_OK || data == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: Pasteboard read html failed status=%d(%s)",
            status, ohos_cliprdr_pasteboard_status_name(status));
        return 1;
    }

    primary = OH_UdsHtml_Create();
    if (primary != 0)
    {
        rc = OH_UdmfData_GetPrimaryHtml(data, primary);
        if (rc == UDMF_E_OK)
        {
            const char *content = OH_UdsHtml_GetContent(primary);
            const char *plain_content = OH_UdsHtml_GetPlainContent(primary);
            if (content != 0 && content[0] != '\0')
            {
                *html = g_strdup(content);
                if (plain != 0 && plain_content != 0 &&
                        plain_content[0] != '\0')
                {
                    *plain = g_strdup(plain_content);
                }
            }
        }
        OH_UdsHtml_Destroy(primary);
        if (*html != 0)
        {
            OH_UdmfData_Destroy(data);
            cliprdr->pasteboard_reads++;
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.cliprdr: Pasteboard read html source=primary html-bytes=%d plain-bytes=%d",
                (int)g_strlen(*html),
                plain == 0 || *plain == 0 ? 0 : (int)g_strlen(*plain));
            return 0;
        }
    }

    record_count = OH_UdmfData_GetRecordCount(data);
    for (index = 0; index < record_count; index++)
    {
        OH_UdmfRecord *record;
        OH_UdsHtml *entry;

        record = OH_UdmfData_GetRecord(data, (unsigned int)index);
        if (record == 0)
        {
            continue;
        }
        entry = OH_UdsHtml_Create();
        if (entry == 0)
        {
            continue;
        }
        rc = OH_UdmfRecord_GetHtml(record, entry);
        if (rc == UDMF_E_OK)
        {
            const char *content = OH_UdsHtml_GetContent(entry);
            const char *plain_content = OH_UdsHtml_GetPlainContent(entry);
            if (content != 0 && content[0] != '\0')
            {
                *html = g_strdup(content);
                if (plain != 0 && plain_content != 0 &&
                        plain_content[0] != '\0')
                {
                    *plain = g_strdup(plain_content);
                }
            }
        }
        OH_UdsHtml_Destroy(entry);
        if (*html != 0)
        {
            OH_UdmfData_Destroy(data);
            cliprdr->pasteboard_reads++;
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.cliprdr: Pasteboard read html source=record index=%d html-bytes=%d plain-bytes=%d",
                index, (int)g_strlen(*html),
                plain == 0 || *plain == 0 ? 0 : (int)g_strlen(*plain));
            return 0;
        }
    }

    OH_UdmfData_Destroy(data);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: Pasteboard read html found no html records=%d",
        record_count);
    return 1;
}

static int
ohos_cliprdr_read_uri_from_data(OH_UdmfData *data, char **uri)
{
    int index;
    int record_count;

    if (uri == 0 || data == 0)
    {
        return 1;
    }
    *uri = 0;
    record_count = OH_UdmfData_GetRecordCount(data);
    for (index = 0; index < record_count; index++)
    {
        OH_UdmfRecord *record = OH_UdmfData_GetRecord(data, (unsigned int)index);
        OH_UdsHyperlink *hyperlink;
        OH_UdsFileUri *file_uri;

        if (record == 0)
        {
            continue;
        }
        hyperlink = OH_UdsHyperlink_Create();
        if (hyperlink != 0)
        {
            if (OH_UdmfRecord_GetHyperlink(record, hyperlink) == UDMF_E_OK)
            {
                const char *url = OH_UdsHyperlink_GetUrl(hyperlink);
                if (url != 0 && url[0] != '\0')
                {
                    *uri = g_strdup(url);
                }
            }
            OH_UdsHyperlink_Destroy(hyperlink);
            if (*uri != 0)
            {
                return 0;
            }
        }
        file_uri = OH_UdsFileUri_Create();
        if (file_uri != 0)
        {
            if (OH_UdmfRecord_GetFileUri(record, file_uri) == UDMF_E_OK)
            {
                const char *file = OH_UdsFileUri_GetFileUri(file_uri);
                if (file != 0 && file[0] != '\0')
                {
                    *uri = g_strdup(file);
                }
            }
            OH_UdsFileUri_Destroy(file_uri);
            if (*uri != 0)
            {
                return 0;
            }
        }
    }
    return 1;
}

int
ohos_cliprdr_pasteboard_read_uri(struct ohos_cliprdr *cliprdr, char **uri)
{
    int status;
    OH_UdmfData *data;

    if (uri == 0)
    {
        return 1;
    }
    *uri = 0;
    if (cliprdr == 0 || cliprdr->pasteboard == 0)
    {
        return 1;
    }
    data = ohos_cliprdr_pasteboard_get_data(cliprdr, "read uri", &status);
    if (status != ERR_OK || data == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: Pasteboard read uri failed status=%d(%s)",
            status, ohos_cliprdr_pasteboard_status_name(status));
        return 1;
    }
    if (ohos_cliprdr_read_uri_from_data(data, uri) == 0)
    {
        OH_UdmfData_Destroy(data);
        cliprdr->pasteboard_reads++;
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: Pasteboard read uri source=udmf bytes=%d",
            (int)g_strlen(*uri));
        return 0;
    }
    OH_UdmfData_Destroy(data);

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: Pasteboard read uri found no uri data");
    return 1;
}

int
ohos_cliprdr_pasteboard_write_html(struct ohos_cliprdr *cliprdr,
                                   const char *html, const char *plain)
{
    int rc = UDMF_E_OK;
    OH_UdsHtml *html_data = 0;
    OH_UdsPlainText *plain_text = 0;
    OH_UdmfRecord *record = 0;
    OH_UdmfData *data = 0;
    const char *plain_value;

    if (cliprdr == 0 || cliprdr->pasteboard == 0 || html == 0)
    {
        return 1;
    }
    plain_value = (plain != 0 && plain[0] != '\0') ? plain : html;
    html_data = OH_UdsHtml_Create();
    plain_text = OH_UdsPlainText_Create();
    record = OH_UdmfRecord_Create();
    data = OH_UdmfData_Create();
    if (html_data == 0 || plain_text == 0 || record == 0 || data == 0)
    {
        goto fail;
    }
    rc = ohos_cliprdr_udmf_make_cross_app(data);
    if (rc != UDMF_E_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: UDMF html cross-app share setup failed rc=%d",
            rc);
        goto fail;
    }
    rc = OH_UdsHtml_SetContent(html_data, html);
    if (rc == UDMF_E_OK)
    {
        rc = OH_UdsHtml_SetPlainContent(html_data, plain_value);
    }
    if (rc == UDMF_E_OK)
    {
        rc = OH_UdmfRecord_AddHtml(record, html_data);
    }
    if (rc == UDMF_E_OK)
    {
        rc = OH_UdsPlainText_SetContent(plain_text, plain_value);
    }
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
        goto fail;
    }

    ohos_cliprdr_pasteboard_begin_remote_write(cliprdr);
    rc = OH_Pasteboard_SetData(cliprdr->pasteboard, data);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: Pasteboard SetData html record=html+plain html-bytes=%d plain-bytes=%d status=%d(%s)",
        (int)g_strlen(html), plain_value == 0 ? 0 : (int)g_strlen(plain_value),
        rc, ohos_cliprdr_pasteboard_status_name(rc));
    if (rc != ERR_OK)
    {
        ohos_cliprdr_pasteboard_cancel_remote_write(cliprdr);
        goto fail;
    }
    cliprdr->pasteboard_writes++;
    OH_UdsHtml_Destroy(html_data);
    OH_UdsPlainText_Destroy(plain_text);
    OH_UdmfRecord_Destroy(record);
    OH_UdmfData_Destroy(data);
    return 0;

fail:
    cliprdr->errors++;
    if (html_data != 0)
    {
        OH_UdsHtml_Destroy(html_data);
    }
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

int
ohos_cliprdr_pasteboard_write_uri(struct ohos_cliprdr *cliprdr, const char *uri)
{
    int rc = UDMF_E_OK;
    OH_UdsHyperlink *hyperlink = 0;
    OH_UdsFileUri *file_uri = 0;
    OH_UdsPlainText *plain_text = 0;
    OH_UdmfRecord *record = 0;
    OH_UdmfData *data = 0;

    if (cliprdr == 0 || cliprdr->pasteboard == 0 || uri == 0)
    {
        return 1;
    }
    record = OH_UdmfRecord_Create();
    data = OH_UdmfData_Create();
    plain_text = OH_UdsPlainText_Create();
    if (record == 0 || data == 0 || plain_text == 0)
    {
        goto fail;
    }
    rc = ohos_cliprdr_udmf_make_cross_app(data);
    if (rc != UDMF_E_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: UDMF uri cross-app share setup failed rc=%d",
            rc);
        goto fail;
    }
    if (ohos_cliprdr_starts_with(uri, "http://") ||
            ohos_cliprdr_starts_with(uri, "https://"))
    {
        hyperlink = OH_UdsHyperlink_Create();
        if (hyperlink == 0)
        {
            goto fail;
        }
        rc = OH_UdsHyperlink_SetUrl(hyperlink, uri);
        if (rc == UDMF_E_OK)
        {
            rc = OH_UdsHyperlink_SetDescription(hyperlink, uri);
        }
        if (rc == UDMF_E_OK)
        {
            rc = OH_UdmfRecord_AddHyperlink(record, hyperlink);
        }
    }
    else
    {
        file_uri = OH_UdsFileUri_Create();
        if (file_uri == 0)
        {
            goto fail;
        }
        rc = OH_UdsFileUri_SetFileUri(file_uri, uri);
        if (rc == UDMF_E_OK)
        {
            rc = OH_UdmfRecord_AddFileUri(record, file_uri);
        }
    }
    if (rc == UDMF_E_OK)
    {
        rc = OH_UdsPlainText_SetContent(plain_text, uri);
    }
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
        goto fail;
    }
    ohos_cliprdr_pasteboard_begin_remote_write(cliprdr);
    rc = OH_Pasteboard_SetData(cliprdr->pasteboard, data);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: Pasteboard SetData uri record=%s+plain bytes=%d status=%d(%s)",
        hyperlink != 0 ? "hyperlink" : "fileUri",
        (int)g_strlen(uri), rc, ohos_cliprdr_pasteboard_status_name(rc));
    if (rc != ERR_OK)
    {
        ohos_cliprdr_pasteboard_cancel_remote_write(cliprdr);
        goto fail;
    }
    cliprdr->pasteboard_writes++;
    if (hyperlink != 0)
    {
        OH_UdsHyperlink_Destroy(hyperlink);
    }
    if (file_uri != 0)
    {
        OH_UdsFileUri_Destroy(file_uri);
    }
    OH_UdsPlainText_Destroy(plain_text);
    OH_UdmfRecord_Destroy(record);
    OH_UdmfData_Destroy(data);
    return 0;

fail:
    cliprdr->errors++;
    if (hyperlink != 0)
    {
        OH_UdsHyperlink_Destroy(hyperlink);
    }
    if (file_uri != 0)
    {
        OH_UdsFileUri_Destroy(file_uri);
    }
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
