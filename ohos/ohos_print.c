/*
 * HarmonyOS native print helpers for the xrdp OHOS backend.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_print.h"

#include "log.h"
#include "os_calls.h"
#include "string_calls.h"

#include <BasicServicesKit/ohprint.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define OHOS_PRINT_DEFAULT_SPOOL_PATH "/data/storage/el2/base/files/xrdp/print"
#define OHOS_PRINT_MAX_FILE_BYTES (100LL * 1024LL * 1024LL)

static int g_ohos_print_initialized = 0;

static const char *
ohos_print_error_name(Print_ErrorCode code)
{
    switch (code)
    {
        case PRINT_ERROR_NONE:
            return "PRINT_ERROR_NONE";
        case PRINT_ERROR_NO_PERMISSION:
            return "PRINT_ERROR_NO_PERMISSION";
        case PRINT_ERROR_INVALID_PARAMETER:
            return "PRINT_ERROR_INVALID_PARAMETER";
        case PRINT_ERROR_GENERIC_FAILURE:
            return "PRINT_ERROR_GENERIC_FAILURE";
        case PRINT_ERROR_RPC_FAILURE:
            return "PRINT_ERROR_RPC_FAILURE";
        case PRINT_ERROR_SERVER_FAILURE:
            return "PRINT_ERROR_SERVER_FAILURE";
        case PRINT_ERROR_INVALID_EXTENSION:
            return "PRINT_ERROR_INVALID_EXTENSION";
        case PRINT_ERROR_INVALID_PRINTER:
            return "PRINT_ERROR_INVALID_PRINTER";
        case PRINT_ERROR_INVALID_PRINT_JOB:
            return "PRINT_ERROR_INVALID_PRINT_JOB";
        case PRINT_ERROR_FILE_IO:
            return "PRINT_ERROR_FILE_IO";
        case PRINT_ERROR_UNKNOWN:
            return "PRINT_ERROR_UNKNOWN";
        default:
            return "PRINT_ERROR_UNRECOGNIZED";
    }
}

static int
ohos_print_is_directory(const char *path)
{
    struct stat st;

    if (path == 0 || path[0] == '\0')
    {
        return 0;
    }
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
ohos_print_ensure_directory(const char *path)
{
    char current[PATH_MAX];
    int current_len = 0;
    int index;

    if (path == 0 || path[0] == '\0' || ohos_print_is_directory(path))
    {
        return 0;
    }
    if (path[0] != '/')
    {
        return 1;
    }

    current[0] = '/';
    current[1] = '\0';
    current_len = 1;
    index = 1;

    while (path[index] != '\0')
    {
        int start = index;
        int part_len;

        while (path[index] != '\0' && path[index] != '/')
        {
            index++;
        }
        part_len = index - start;
        if (part_len > 0)
        {
            if (current_len > 1)
            {
                if (current_len + 1 >= PATH_MAX)
                {
                    return 1;
                }
                current[current_len++] = '/';
                current[current_len] = '\0';
            }
            if (current_len + part_len >= PATH_MAX)
            {
                return 1;
            }
            g_memcpy(current + current_len, path + start, part_len);
            current_len += part_len;
            current[current_len] = '\0';
            if (!ohos_print_is_directory(current))
            {
                if (mkdir(current, 0770) != 0 && errno != EEXIST)
                {
                    LOG(LOG_LEVEL_WARNING,
                        "xrdp.ohos.print: mkdir failed path=%s errno=%d",
                        current, errno);
                    return 1;
                }
            }
        }
        while (path[index] == '/')
        {
            index++;
        }
    }

    return ohos_print_is_directory(path) ? 0 : 1;
}

static const char *
ohos_print_spool_path(void)
{
    const char *path = g_getenv("XRDP_OHOS_PRINT_SPOOL_PATH");

    if (path != 0 && path[0] != '\0')
    {
        return path;
    }
    return OHOS_PRINT_DEFAULT_SPOOL_PATH;
}

static int
ohos_print_is_safe_file_name(const char *name)
{
    const unsigned char *p;

    if (name == 0 || name[0] == '\0' ||
            g_strcmp(name, ".") == 0 || g_strcmp(name, "..") == 0)
    {
        return 0;
    }

    for (p = (const unsigned char *)name; *p != '\0'; ++p)
    {
        if (*p < 0x20 || *p == '/' || *p == '\\' || *p == ':')
        {
            return 0;
        }
    }
    return 1;
}

static int
ohos_print_path_inside_root(const char *root, const char *path)
{
    size_t root_len;

    if (root == 0 || path == 0)
    {
        return 0;
    }
    root_len = strlen(root);
    while (root_len > 1 && root[root_len - 1] == '/')
    {
        root_len--;
    }
    return strlen(path) > root_len &&
           g_strncmp(path, root, root_len) == 0 &&
           path[root_len] == '/';
}

static int
ohos_print_resolve_spool_file(const char *file_name,
                              char *resolved_file, int resolved_file_bytes)
{
    const char *spool_path = ohos_print_spool_path();
    char candidate[PATH_MAX];
    char root_real[PATH_MAX];
    char file_real[PATH_MAX];
    struct stat st;

    g_memset(&st, 0, sizeof(st));
    if (!ohos_print_is_safe_file_name(file_name))
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: rejected unsafe spool file name");
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (ohos_print_ensure_directory(spool_path) != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: spool directory unavailable path=%s",
            spool_path);
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (realpath(spool_path, root_real) == 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: realpath spool directory failed path=%s errno=%d",
            spool_path, errno);
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    if (g_snprintf(candidate, sizeof(candidate), "%s/%s",
                   root_real, file_name) >= (int)sizeof(candidate))
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (realpath(candidate, file_real) == 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: spool file not found name=%s errno=%d",
            file_name, errno);
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (!ohos_print_path_inside_root(root_real, file_real))
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: rejected spool file outside root path=%s",
            file_real);
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (stat(file_real, &st) != 0 || !S_ISREG(st.st_mode) ||
            st.st_size <= 0 || st.st_size > OHOS_PRINT_MAX_FILE_BYTES)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: rejected spool file path=%s size=%lld errno=%d",
            file_real, (long long)st.st_size, errno);
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (g_snprintf(resolved_file, resolved_file_bytes, "%s",
                   file_real) >= resolved_file_bytes)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    return XRDP_OHOS_BACKEND_STATUS_OK;
}

static int
ohos_print_bytes_look_like_text(const unsigned char *data, size_t size)
{
    size_t index;

    if (data == 0 || size == 0)
    {
        return 0;
    }
    for (index = 0; index < size; index++)
    {
        unsigned char c = data[index];
        if (c == '\t' || c == '\n' || c == '\r')
        {
            continue;
        }
        if (c >= 0x20 && c <= 0x7e)
        {
            continue;
        }
        return 0;
    }
    return 1;
}

static Print_DocumentFormat
ohos_print_detect_document_format(const char *path, const char **format_name)
{
    unsigned char header[512];
    int fd;
    ssize_t bytes;

    g_memset(header, 0, sizeof(header));
    if (format_name != 0)
    {
        *format_name = "auto";
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        return DOCUMENT_FORMAT_AUTO;
    }
    do
    {
        bytes = read(fd, header, sizeof(header));
    } while (bytes < 0 && errno == EINTR);
    (void)close(fd);

    if (bytes <= 0)
    {
        return DOCUMENT_FORMAT_AUTO;
    }
    if (bytes >= 4 && g_memcmp(header, "%PDF", 4) == 0)
    {
        if (format_name != 0)
        {
            *format_name = "pdf";
        }
        return DOCUMENT_FORMAT_PDF;
    }
    if (bytes >= 4 && g_memcmp(header, "%!PS", 4) == 0)
    {
        if (format_name != 0)
        {
            *format_name = "postscript";
        }
        return DOCUMENT_FORMAT_POSTSCRIPT;
    }
    if (bytes >= 3 && header[0] == 0xff &&
            header[1] == 0xd8 && header[2] == 0xff)
    {
        if (format_name != 0)
        {
            *format_name = "jpeg";
        }
        return DOCUMENT_FORMAT_JPEG;
    }
    if (ohos_print_bytes_look_like_text(header, (size_t)bytes))
    {
        if (format_name != 0)
        {
            *format_name = "text";
        }
        return DOCUMENT_FORMAT_TEXT;
    }
    return DOCUMENT_FORMAT_AUTO;
}

static int
ohos_print_copy_id(char *printer_id, int printer_id_bytes, const char *value)
{
    if (printer_id == 0 || printer_id_bytes <= 0 || value == 0 ||
            value[0] == '\0')
    {
        return 0;
    }
    if (g_snprintf(printer_id, printer_id_bytes, "%s", value) >=
            printer_id_bytes)
    {
        printer_id[0] = '\0';
        return 0;
    }
    return 1;
}

static int
ohos_print_resolve_printer_id(const char *requested,
                              char *printer_id, int printer_id_bytes)
{
    Print_StringList printer_ids;
    Print_ErrorCode rc;
    char first[256];
    char fallback_default[256];
    char matched[256];
    uint32_t index;

    if (printer_id != 0 && printer_id_bytes > 0)
    {
        printer_id[0] = '\0';
    }
    if (requested != 0 && requested[0] != '\0')
    {
        Print_PrinterInfo *info = 0;
        rc = OH_Print_QueryPrinterInfo((char *)requested, &info);
        if (rc == PRINT_ERROR_NONE)
        {
            const char *id = (info != 0 && info->printerId != 0 &&
                              info->printerId[0] != '\0') ?
                             info->printerId : requested;
            int copied = ohos_print_copy_id(printer_id, printer_id_bytes, id);
            if (info != 0)
            {
                OH_Print_ReleasePrinterInfo(info);
            }
            return copied ? XRDP_OHOS_BACKEND_STATUS_OK :
                   XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
        }
    }

    g_memset(&printer_ids, 0, sizeof(printer_ids));
    rc = OH_Print_QueryPrinterList(&printer_ids);
    if (rc != PRINT_ERROR_NONE)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: OH_Print_QueryPrinterList failed error=%s code=%u",
            ohos_print_error_name(rc), (unsigned int)rc);
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }

    first[0] = '\0';
    fallback_default[0] = '\0';
    matched[0] = '\0';
    for (index = 0; index < printer_ids.count; index++)
    {
        const char *list_id = printer_ids.list != 0 ? printer_ids.list[index] : 0;
        Print_PrinterInfo *info = 0;
        const char *current_id;
        const char *current_name;

        if (list_id == 0 || list_id[0] == '\0')
        {
            continue;
        }
        rc = OH_Print_QueryPrinterInfo((char *)list_id, &info);
        if (rc != PRINT_ERROR_NONE)
        {
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.print: OH_Print_QueryPrinterInfo failed id=%s error=%s code=%u",
                list_id, ohos_print_error_name(rc), (unsigned int)rc);
        }
        current_id = (rc == PRINT_ERROR_NONE && info != 0 &&
                      info->printerId != 0 && info->printerId[0] != '\0') ?
                     info->printerId : list_id;
        current_name = (rc == PRINT_ERROR_NONE && info != 0 &&
                        info->printerName != 0) ? info->printerName : "";

        if (first[0] == '\0')
        {
            (void)g_snprintf(first, sizeof(first), "%s", current_id);
        }
        if (fallback_default[0] == '\0' && rc == PRINT_ERROR_NONE &&
                info != 0 && info->isDefaultPrinter)
        {
            (void)g_snprintf(fallback_default, sizeof(fallback_default),
                             "%s", current_id);
        }
        if (requested != 0 && requested[0] != '\0' &&
                (g_strcmp(current_id, requested) == 0 ||
                 g_strcmp(list_id, requested) == 0 ||
                 (current_name[0] != '\0' &&
                  g_strcasecmp(current_name, requested) == 0)))
        {
            (void)g_snprintf(matched, sizeof(matched), "%s", current_id);
        }

        if (info != 0)
        {
            OH_Print_ReleasePrinterInfo(info);
        }
        if (matched[0] != '\0')
        {
            break;
        }
    }
    OH_Print_ReleasePrinterList(&printer_ids);

    if (matched[0] != '\0')
    {
        return ohos_print_copy_id(printer_id, printer_id_bytes, matched) ?
               XRDP_OHOS_BACKEND_STATUS_OK :
               XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (fallback_default[0] != '\0')
    {
        return ohos_print_copy_id(printer_id, printer_id_bytes,
                                  fallback_default) ?
               XRDP_OHOS_BACKEND_STATUS_OK :
               XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (first[0] != '\0')
    {
        return ohos_print_copy_id(printer_id, printer_id_bytes, first) ?
               XRDP_OHOS_BACKEND_STATUS_OK :
               XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    LOG(LOG_LEVEL_WARNING, "xrdp.ohos.print: no HarmonyOS printer found");
    return XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION;
}

int
ohos_print_ensure_initialized(void)
{
    Print_ErrorCode rc;

    if (g_ohos_print_initialized)
    {
        return XRDP_OHOS_BACKEND_STATUS_OK;
    }

    rc = OH_Print_Init();
    if (rc != PRINT_ERROR_NONE)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: OH_Print_Init failed error=%s code=%u",
            ohos_print_error_name(rc), (unsigned int)rc);
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }
    g_ohos_print_initialized = 1;
    LOG(LOG_LEVEL_INFO, "xrdp.ohos.print: OH_Print_Init ok");
    return XRDP_OHOS_BACKEND_STATUS_OK;
}

void
ohos_print_release(void)
{
    if (!g_ohos_print_initialized)
    {
        return;
    }
    (void)OH_Print_Release();
    g_ohos_print_initialized = 0;
    LOG(LOG_LEVEL_INFO, "xrdp.ohos.print: OH_Print_Release ok");
}

int
ohos_print_probe(char *printer_id, int printer_id_bytes)
{
    int status = ohos_print_ensure_initialized();

    if (status != XRDP_OHOS_BACKEND_STATUS_OK)
    {
        return status;
    }
    status = ohos_print_resolve_printer_id(0, printer_id, printer_id_bytes);
    if (status == XRDP_OHOS_BACKEND_STATUS_OK)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.print: HarmonyOS printer available id=%s",
            printer_id == 0 ? "" : printer_id);
    }
    return status;
}

int
ohos_print_make_spool_file(char *file_name, int file_name_bytes,
                           char *resolved_file, int resolved_file_bytes)
{
    const char *spool_path = ohos_print_spool_path();
    char root_real[PATH_MAX];
    int attempt;

    if (file_name == 0 || file_name_bytes <= 0 ||
            resolved_file == 0 || resolved_file_bytes <= 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    file_name[0] = '\0';
    resolved_file[0] = '\0';

    if (ohos_print_ensure_directory(spool_path) != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: spool directory unavailable path=%s",
            spool_path);
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (realpath(spool_path, root_real) == 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: realpath spool directory failed path=%s errno=%d",
            spool_path, errno);
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    for (attempt = 0; attempt < 16; attempt++)
    {
        uint32_t nonce = 0;
        char candidate_name[128];
        char candidate_file[PATH_MAX];

        g_random((char *)&nonce, sizeof(nonce));
        if (nonce == 0)
        {
            nonce = (uint32_t)(attempt + 1);
        }
        if (g_snprintf(candidate_name, sizeof(candidate_name),
                       "xrdp-rdpdr-print-%d-%08x.prn",
                       g_getpid(), nonce) >= (int)sizeof(candidate_name))
        {
            return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
        }
        if (g_snprintf(candidate_file, sizeof(candidate_file), "%s/%s",
                       root_real, candidate_name) >=
                (int)sizeof(candidate_file))
        {
            return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
        }
        if (g_file_exist(candidate_file))
        {
            continue;
        }
        if (g_snprintf(file_name, file_name_bytes, "%s",
                       candidate_name) >= file_name_bytes ||
                g_snprintf(resolved_file, resolved_file_bytes, "%s",
                           candidate_file) >= resolved_file_bytes)
        {
            file_name[0] = '\0';
            resolved_file[0] = '\0';
            return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
        }
        return XRDP_OHOS_BACKEND_STATUS_OK;
    }

    LOG(LOG_LEVEL_WARNING,
        "xrdp.ohos.print: failed to allocate spool file name");
    return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
}

int
ohos_print_spool_file(const char *file_name, const char *printer_id)
{
    char resolved_file[PATH_MAX];
    char resolved_printer[256];
    char job_name[512];
    uint32_t fd_list[1];
    Print_PrintJob native_job;
    Print_DocumentFormat format;
    Print_ErrorCode rc;
    const char *format_name;
    int fd;
    int status;

    status = ohos_print_resolve_spool_file(file_name, resolved_file,
                                           sizeof(resolved_file));
    if (status != XRDP_OHOS_BACKEND_STATUS_OK)
    {
        return status;
    }
    status = ohos_print_ensure_initialized();
    if (status != XRDP_OHOS_BACKEND_STATUS_OK)
    {
        return status;
    }
    status = ohos_print_resolve_printer_id(printer_id, resolved_printer,
                                           sizeof(resolved_printer));
    if (status != XRDP_OHOS_BACKEND_STATUS_OK)
    {
        return status;
    }

    format = ohos_print_detect_document_format(resolved_file, &format_name);
    fd = open(resolved_file, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: open spool print file failed path=%s errno=%d",
            resolved_file, errno);
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    (void)g_snprintf(job_name, sizeof(job_name), "xrdp print spool - %s",
                     file_name == 0 ? "" : file_name);
    fd_list[0] = (uint32_t)fd;
    g_memset(&native_job, 0, sizeof(native_job));
    native_job.jobName = job_name;
    native_job.fdList = fd_list;
    native_job.fdListCount = 1;
    native_job.printerId = resolved_printer;
    native_job.copyNumber = 1;
    native_job.colorMode = COLOR_MODE_AUTO;
    native_job.duplexMode = DUPLEX_MODE_ONE_SIDED;
    native_job.orientationMode = ORIENTATION_MODE_NONE;
    native_job.printQuality = PRINT_QUALITY_NORMAL;
    native_job.documentFormat = format;

    rc = OH_Print_ConnectPrinter(resolved_printer);
    if (rc != PRINT_ERROR_NONE)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: OH_Print_ConnectPrinter retuned printer=%s error=%s code=%u",
            resolved_printer, ohos_print_error_name(rc), (unsigned int)rc);
    }

    rc = OH_Print_StartPrintJob(&native_job);
    (void)close(fd);
    if (rc != PRINT_ERROR_NONE)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.print: OH_Print_StartPrintJob failed file=%s printer=%s format=%s error=%s code=%u",
            resolved_file, resolved_printer, format_name,
            ohos_print_error_name(rc), (unsigned int)rc);
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.print: OH_Print_StartPrintJob ok file=%s printer=%s format=%s",
        resolved_file, resolved_printer, format_name);
    return XRDP_OHOS_BACKEND_STATUS_OK;
}
