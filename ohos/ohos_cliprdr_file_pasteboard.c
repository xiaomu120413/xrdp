/*
 * Pasteboard writer for remote file clipboard payloads.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "log.h"
#include "os_calls.h"

#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include <database/udmf/udmf.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/uds.h>

static void
ohos_cliprdr_destroy_file_uri_records(OH_UdmfRecord **records,
                                      OH_UdsFileUri **file_uris,
                                      OH_UdsPlainText **plain_texts,
                                      int count)
{
    int index;

    for (index = 0; index < count; index++)
    {
        if (plain_texts != 0 && plain_texts[index] != 0)
        {
            OH_UdsPlainText_Destroy(plain_texts[index]);
        }
        if (file_uris != 0 && file_uris[index] != 0)
        {
            OH_UdsFileUri_Destroy(file_uris[index]);
        }
        if (records != 0 && records[index] != 0)
        {
            OH_UdmfRecord_Destroy(records[index]);
        }
    }
}

int
ohos_cliprdr_write_remote_file_uris(struct ohos_cliprdr *cliprdr)
{
    OH_UdmfData *data = 0;
    OH_UdmfRecord **records = 0;
    OH_UdsFileUri **file_uris = 0;
    OH_UdsPlainText **plain_texts = 0;
    int index;
    int uri_count = 0;
    int created = 0;
    int rc = ERR_OK;

    for (index = 0; index < cliprdr->remote_file_count; index++)
    {
        if (cliprdr->remote_files[index].uri != 0)
        {
            uri_count++;
        }
    }
    if (uri_count <= 0)
    {
        return 1;
    }
    data = OH_UdmfData_Create();
    records = (OH_UdmfRecord **)g_malloc(sizeof(OH_UdmfRecord *) *
                                         uri_count, 1);
    file_uris = (OH_UdsFileUri **)g_malloc(sizeof(OH_UdsFileUri *) *
                                           uri_count, 1);
    plain_texts = (OH_UdsPlainText **)g_malloc(sizeof(OH_UdsPlainText *) *
                                               uri_count, 1);
    if (data == 0 || records == 0 || file_uris == 0 || plain_texts == 0)
    {
        rc = ERR_INNER_ERROR;
        goto done;
    }
    rc = ohos_cliprdr_udmf_make_cross_app(data);
    if (rc != UDMF_E_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: UDMF file cross-app share setup failed rc=%d",
            rc);
        goto done;
    }
    for (index = 0; index < cliprdr->remote_file_count; index++)
    {
        const char *uri = cliprdr->remote_files[index].uri;

        if (uri == 0)
        {
            continue;
        }
        records[created] = OH_UdmfRecord_Create();
        file_uris[created] = OH_UdsFileUri_Create();
        plain_texts[created] = OH_UdsPlainText_Create();
        if (records[created] == 0 || file_uris[created] == 0 ||
                plain_texts[created] == 0)
        {
            rc = ERR_INNER_ERROR;
            created++;
            goto done;
        }
        rc = OH_UdsFileUri_SetFileUri(file_uris[created], uri);
        if (rc == UDMF_E_OK)
        {
            rc = OH_UdmfRecord_AddFileUri(records[created],
                                          file_uris[created]);
        }
        if (rc == UDMF_E_OK)
        {
            rc = OH_UdsPlainText_SetContent(plain_texts[created], uri);
        }
        if (rc == UDMF_E_OK)
        {
            rc = OH_UdmfRecord_AddPlainText(records[created],
                                            plain_texts[created]);
        }
        if (rc == UDMF_E_OK)
        {
            rc = OH_UdmfData_AddRecord(data, records[created]);
        }
        created++;
        if (rc != UDMF_E_OK)
        {
            goto done;
        }
    }
    ohos_cliprdr_pasteboard_begin_remote_write(cliprdr);
    rc = OH_Pasteboard_SetData(cliprdr->pasteboard, data);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: Pasteboard SetData remote files count=%d status=%d(%s)",
        uri_count, rc, ohos_cliprdr_pasteboard_status_name(rc));
    if (rc != ERR_OK)
    {
        ohos_cliprdr_pasteboard_cancel_remote_write(cliprdr);
    }
    else
    {
        cliprdr->pasteboard_writes++;
    }

done:
    ohos_cliprdr_destroy_file_uri_records(records, file_uris, plain_texts,
                                          created);
    if (data != 0)
    {
        OH_UdmfData_Destroy(data);
    }
    g_free(records);
    g_free(file_uris);
    g_free(plain_texts);
    return rc == ERR_OK ? 0 : 1;
}
