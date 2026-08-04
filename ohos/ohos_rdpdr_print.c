/*
 * Minimal RDPDR printer channel support for the xrdp OHOS backend.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_rdpdr_print.h"

#include "log.h"
#include "ms-erref.h"
#include "ms-rdpbcgr.h"
#include "ms-rdpefs.h"
#include "ohos_print.h"
#include "os_calls.h"
#include "parse.h"
#include "string_calls.h"
#include "xrdp_constants.h"
#include "xup.h"

#include <limits.h>

#define OHOS_RDPDR_MAX_PDU_BYTES 1048576
#define OHOS_RDPDR_MAX_PRINT_JOB_BYTES (100LL * 1024LL * 1024LL)
#define OHOS_RDPDR_SERVER_MAJOR_VERSION 0x0001
#define OHOS_RDPDR_SERVER_MINOR_VERSION 0x000c
#define OHOS_RDPDR_CAP_GENERAL_LENGTH 44
#define OHOS_RDPDR_CAP_SIMPLE_LENGTH 8
#define OHOS_RDPDR_GENERAL_CAP_VERSION 2
#define OHOS_RDPDR_PRINTER_CAP_VERSION 1
#define OHOS_RDPDR_PRINT_BRIDGE_HOST "127.0.0.1"
#define OHOS_RDPDR_PRINT_BRIDGE_PORT "29100"
#define OHOS_RDPDR_PRINT_BRIDGE_QUEUE_MAX 4
#define OHOS_RDPDR_PRINT_BRIDGE_RECV_BYTES 32768
#define OHOS_RDPDR_PRINT_BRIDGE_WRITE_BYTES 1024
#define OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE 0
#define OHOS_RDPDR_PRINT_BRIDGE_STATE_CREATE 1
#define OHOS_RDPDR_PRINT_BRIDGE_STATE_WRITE 2
#define OHOS_RDPDR_PRINT_BRIDGE_STATE_CLOSE 3
#define OHOS_RDPDR_PRINTER_ANNOUNCE_FLAG_ASCII 0x00000001U
#define OHOS_RDPDR_PRINTER_ANNOUNCE_FLAG_DEFAULTPRINTER 0x00000002U
#define OHOS_RDPDR_PRINTER_ANNOUNCE_FLAG_XPSFORMAT 0x00000010U
#define OHOS_RDPDR_PRINT_GENERIC_WRITE 0x40000000U
#define OHOS_RDPDR_PRINT_FILE_SHARE_READ 0x00000001U
#define OHOS_RDPDR_PRINT_FILE_SHARE_WRITE 0x00000002U
#define OHOS_RDPDR_PRINT_FILE_OPEN 0x00000001U

static int
ohos_rdpdr_print_probe(struct ohos_rdpdr_print *rdpdr, const char *reason);

static int
ohos_rdpdr_print_bridge_pump(struct ohos_rdpdr_print *rdpdr,
                             const char *reason);

static void
ohos_rdpdr_print_bridge_clear_queue(struct ohos_rdpdr_print *rdpdr);

static void
ohos_rdpdr_print_bridge_fail_active(struct ohos_rdpdr_print *rdpdr,
                                    const char *reason);

static int
ohos_rdpdr_print_is_printer_device(const struct ohos_rdpdr_print *rdpdr,
                                   uint32_t device_id);

static uint32_t
ohos_rdpdr_print_get_u32_le(const char *data)
{
    const unsigned char *u = (const unsigned char *)data;

    return ((uint32_t)u[0]) | (((uint32_t)u[1]) << 8) |
           (((uint32_t)u[2]) << 16) | (((uint32_t)u[3]) << 24);
}

static void
ohos_rdpdr_print_copy_announce_string(const char *data, uint32_t bytes,
                                      int is_ascii, char *out,
                                      int out_bytes)
{
    uint32_t index;
    int out_pos = 0;

    if (out == 0 || out_bytes <= 0)
    {
        return;
    }
    out[0] = '\0';
    if (data == 0 || bytes == 0)
    {
        return;
    }

    if (is_ascii)
    {
        for (index = 0; index < bytes && out_pos < out_bytes - 1; index++)
        {
            unsigned char c = (unsigned char)data[index];
            if (c == 0)
            {
                break;
            }
            out[out_pos++] = (c >= 32 && c < 127) ? (char)c : '?';
        }
    }
    else
    {
        for (index = 0; index + 1 < bytes && out_pos < out_bytes - 1;
                index += 2)
        {
            unsigned char lo = (unsigned char)data[index];
            unsigned char hi = (unsigned char)data[index + 1];
            if (lo == 0 && hi == 0)
            {
                break;
            }
            out[out_pos++] = (hi == 0 && lo >= 32 && lo < 127) ?
                             (char)lo : '?';
        }
    }

    out[out_pos] = '\0';
}

static int
ohos_rdpdr_print_parse_printer_announce(const char *data, uint32_t data_len,
                                        uint32_t *flags, char *driver_name,
                                        int driver_name_bytes,
                                        char *printer_name,
                                        int printer_name_bytes)
{
    uint32_t pnp_name_len;
    uint32_t driver_name_len;
    uint32_t printer_name_len;
    uint32_t cached_fields_len;
    uint32_t offset;
    uint32_t announce_flags;
    int is_ascii;

    if (flags != 0)
    {
        *flags = 0;
    }
    if (driver_name != 0 && driver_name_bytes > 0)
    {
        driver_name[0] = '\0';
    }
    if (printer_name != 0 && printer_name_bytes > 0)
    {
        printer_name[0] = '\0';
    }
    if (data == 0 || data_len < 24)
    {
        return 1;
    }

    announce_flags = ohos_rdpdr_print_get_u32_le(data);
    pnp_name_len = ohos_rdpdr_print_get_u32_le(data + 8);
    driver_name_len = ohos_rdpdr_print_get_u32_le(data + 12);
    printer_name_len = ohos_rdpdr_print_get_u32_le(data + 16);
    cached_fields_len = ohos_rdpdr_print_get_u32_le(data + 20);
    offset = 24;
    if (pnp_name_len > data_len - offset)
    {
        return 1;
    }
    offset += pnp_name_len;
    if (driver_name_len > data_len - offset)
    {
        return 1;
    }
    is_ascii = (announce_flags & OHOS_RDPDR_PRINTER_ANNOUNCE_FLAG_ASCII) != 0;
    ohos_rdpdr_print_copy_announce_string(data + offset, driver_name_len,
                                          is_ascii, driver_name,
                                          driver_name_bytes);
    offset += driver_name_len;
    if (printer_name_len > data_len - offset)
    {
        return 1;
    }
    ohos_rdpdr_print_copy_announce_string(data + offset, printer_name_len,
                                          is_ascii, printer_name,
                                          printer_name_bytes);
    offset += printer_name_len;
    if (cached_fields_len > data_len - offset)
    {
        return 1;
    }
    if (flags != 0)
    {
        *flags = announce_flags;
    }
    return 0;
}

struct ohos_rdpdr_print_bridge_job
{
    struct ohos_rdpdr_print_bridge_job *next;
    char file_name[128];
    char path[OHOS_RDPDR_MAX_PRINT_PATH];
    long long bytes;
};

static void
ohos_rdpdr_print_reset_stats(struct ohos_rdpdr_print *rdpdr)
{
    rdpdr->device_announces = 0;
    rdpdr->printer_devices = 0;
    rdpdr->rejected_devices = 0;
    rdpdr->io_requests = 0;
    rdpdr->write_requests = 0;
    rdpdr->print_jobs = 0;
    rdpdr->failed_print_jobs = 0;
    rdpdr->bridge_jobs_received = 0;
    rdpdr->bridge_jobs_forwarded = 0;
    rdpdr->bridge_jobs_failed = 0;
    rdpdr->bridge_jobs_dropped = 0;
    rdpdr->bridge_bytes_forwarded = 0;
    rdpdr->pn_packets = 0;
    rdpdr->errors = 0;
    rdpdr->client_extended_pdu = 0;
    rdpdr->local_printer_id[0] = '\0';
    rdpdr->printer_device_count = 0;
    rdpdr->active_device_id = 0;
    rdpdr->active_file_id = 0;
    rdpdr->active_job_fd = -1;
    rdpdr->active_job_file_name[0] = '\0';
    rdpdr->active_job_path[0] = '\0';
    rdpdr->active_job_bytes = 0;
    ohos_rdpdr_print_bridge_clear_queue(rdpdr);
    if (rdpdr->bridge_active_state != OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE)
    {
        ohos_rdpdr_print_bridge_fail_active(rdpdr, "reset-stats");
    }
}

static void
ohos_rdpdr_print_channel_reset(struct ohos_rdpdr_print *rdpdr)
{
    if (rdpdr == 0)
    {
        return;
    }
    free_stream(rdpdr->dechunker_s);
    rdpdr->dechunker_s = 0;
}

static int
ohos_rdpdr_print_send_channel_data(struct ohos_rdpdr_print *rdpdr,
                                   char *data, int data_len)
{
    int rv = 0;
    int pos = 0;
    int pdu_len = 0;
    int flags;

    if (rdpdr == 0 || rdpdr->mod == 0 ||
            rdpdr->mod->server_send_to_channel == 0 ||
            rdpdr->channel_id < 0 || data == 0 || data_len <= 0)
    {
        return 1;
    }

    for (pos = 0; rv == 0 && pos < data_len; pos += pdu_len)
    {
        pdu_len = data_len - pos;
        if (pdu_len > CHANNEL_CHUNK_LENGTH)
        {
            pdu_len = CHANNEL_CHUNK_LENGTH;
        }

        if (pos == 0)
        {
            flags = ((pos + pdu_len) == data_len) ?
                    (XR_CHANNEL_FLAG_FIRST | XR_CHANNEL_FLAG_LAST) :
                    (XR_CHANNEL_FLAG_FIRST | XR_CHANNEL_FLAG_SHOW_PROTOCOL);
        }
        else if ((pos + pdu_len) == data_len)
        {
            flags = XR_CHANNEL_FLAG_LAST | XR_CHANNEL_FLAG_SHOW_PROTOCOL;
        }
        else
        {
            flags = XR_CHANNEL_FLAG_SHOW_PROTOCOL;
        }

        rv = rdpdr->mod->server_send_to_channel(rdpdr->mod,
                                                rdpdr->channel_id,
                                                data + pos, pdu_len,
                                                data_len, flags);
    }

    return rv;
}

static int
ohos_rdpdr_print_send_stream(struct ohos_rdpdr_print *rdpdr,
                             struct stream *s, const char *name)
{
    int bytes;
    int rv;

    if (s == 0)
    {
        return 1;
    }
    s_mark_end(s);
    bytes = (int)(s->end - s->data);
    rv = ohos_rdpdr_print_send_channel_data(rdpdr, s->data, bytes);
    LOG(rv == 0 ? LOG_LEVEL_DEBUG : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpdr: send %s bytes=%d rv=%d",
        name == 0 ? "pdu" : name, bytes, rv);
    return rv;
}

static void
ohos_rdpdr_print_bridge_reset_recv(struct ohos_rdpdr_print *rdpdr)
{
    rdpdr->bridge_recv_fd = -1;
    rdpdr->bridge_recv_file_name[0] = '\0';
    rdpdr->bridge_recv_path[0] = '\0';
    rdpdr->bridge_recv_bytes = 0;
}

static void
ohos_rdpdr_print_bridge_close_client(struct ohos_rdpdr_print *rdpdr)
{
    if (rdpdr->bridge_client_wait_obj != 0)
    {
        g_delete_wait_obj_from_socket(rdpdr->bridge_client_wait_obj);
        rdpdr->bridge_client_wait_obj = 0;
    }
    if (rdpdr->bridge_client_sck > 0)
    {
        g_sck_close(rdpdr->bridge_client_sck);
        rdpdr->bridge_client_sck = -1;
    }
}

static void
ohos_rdpdr_print_bridge_delete_file(const char *path)
{
    if (path != 0 && path[0] != '\0')
    {
        (void)g_file_delete(path);
    }
}

static void
ohos_rdpdr_print_bridge_free_job(struct ohos_rdpdr_print_bridge_job *job,
                                 int delete_file)
{
    if (job == 0)
    {
        return;
    }
    if (delete_file)
    {
        ohos_rdpdr_print_bridge_delete_file(job->path);
    }
    g_free(job);
}

static void
ohos_rdpdr_print_bridge_clear_queue(struct ohos_rdpdr_print *rdpdr)
{
    struct ohos_rdpdr_print_bridge_job *job;

    while (rdpdr->bridge_queue_head != 0)
    {
        job = rdpdr->bridge_queue_head;
        rdpdr->bridge_queue_head = job->next;
        ohos_rdpdr_print_bridge_free_job(job, 1);
        rdpdr->bridge_jobs_dropped++;
    }
    rdpdr->bridge_queue_tail = 0;
    rdpdr->bridge_queue_count = 0;
}

static void
ohos_rdpdr_print_bridge_reset_active(struct ohos_rdpdr_print *rdpdr)
{
    rdpdr->bridge_active_job = 0;
    rdpdr->bridge_active_fd = -1;
    rdpdr->bridge_active_device_id = 0;
    rdpdr->bridge_active_file_id = 0;
    rdpdr->bridge_active_completion_id = 0;
    rdpdr->bridge_active_last_write_bytes = 0;
    rdpdr->bridge_active_offset = 0;
    rdpdr->bridge_active_state = OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE;
}

static uint32_t
ohos_rdpdr_print_bridge_next_completion_id(struct ohos_rdpdr_print *rdpdr)
{
    rdpdr->bridge_next_completion_id++;
    if (rdpdr->bridge_next_completion_id == 0)
    {
        rdpdr->bridge_next_completion_id = 1;
    }
    return rdpdr->bridge_next_completion_id;
}

static void
ohos_rdpdr_print_bridge_insert_device_io(struct stream *s,
                                         uint32_t device_id,
                                         uint32_t file_id,
                                         uint32_t completion_id,
                                         enum IRP_MJ major_function)
{
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_DEVICE_IOREQUEST);
    out_uint32_le(s, device_id);
    out_uint32_le(s, file_id);
    out_uint32_le(s, completion_id);
    out_uint32_le(s, major_function);
    out_uint32_le(s, IRP_MN_NONE);
}

static int
ohos_rdpdr_print_bridge_send_create(struct ohos_rdpdr_print *rdpdr)
{
    struct stream *s;
    uint32_t completion_id;
    int rv;

    if (rdpdr->bridge_active_job == 0)
    {
        return 1;
    }

    completion_id = ohos_rdpdr_print_bridge_next_completion_id(rdpdr);
    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 64);
    ohos_rdpdr_print_bridge_insert_device_io(
        s, rdpdr->bridge_active_device_id, 0, completion_id, IRP_MJ_CREATE);
    out_uint32_le(s, OHOS_RDPDR_PRINT_GENERIC_WRITE);
    out_uint64_le(s, 0);
    out_uint32_le(s, 0);
    out_uint32_le(s, OHOS_RDPDR_PRINT_FILE_SHARE_READ |
                  OHOS_RDPDR_PRINT_FILE_SHARE_WRITE);
    out_uint32_le(s, OHOS_RDPDR_PRINT_FILE_OPEN);
    out_uint32_le(s, 0);
    out_uint32_le(s, 2);
    out_uint16_le(s, 0);

    rdpdr->bridge_active_completion_id = completion_id;
    rdpdr->bridge_active_state = OHOS_RDPDR_PRINT_BRIDGE_STATE_CREATE;
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "bridge printer create");
    free_stream(s);
    LOG(rv == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpdr: bridge create sent device=0x%08x completion=0x%08x file=%s bytes=%lld rv=%d",
        (unsigned int)rdpdr->bridge_active_device_id,
        (unsigned int)completion_id,
        rdpdr->bridge_active_job->file_name,
        rdpdr->bridge_active_job->bytes, rv);
    return rv;
}

static int
ohos_rdpdr_print_bridge_send_close(struct ohos_rdpdr_print *rdpdr)
{
    struct stream *s;
    uint32_t completion_id;
    int rv;

    completion_id = ohos_rdpdr_print_bridge_next_completion_id(rdpdr);
    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 64);
    ohos_rdpdr_print_bridge_insert_device_io(
        s, rdpdr->bridge_active_device_id, rdpdr->bridge_active_file_id,
        completion_id, IRP_MJ_CLOSE);
    out_uint8s(s, 32);

    rdpdr->bridge_active_completion_id = completion_id;
    rdpdr->bridge_active_state = OHOS_RDPDR_PRINT_BRIDGE_STATE_CLOSE;
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "bridge printer close");
    free_stream(s);
    LOG(rv == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpdr: bridge close sent device=0x%08x file_id=0x%08x completion=0x%08x sent=%llu rv=%d",
        (unsigned int)rdpdr->bridge_active_device_id,
        (unsigned int)rdpdr->bridge_active_file_id,
        (unsigned int)completion_id,
        (unsigned long long)rdpdr->bridge_active_offset, rv);
    return rv;
}

static int
ohos_rdpdr_print_bridge_send_next_write(struct ohos_rdpdr_print *rdpdr)
{
    char buffer[OHOS_RDPDR_PRINT_BRIDGE_WRITE_BYTES];
    struct stream *s;
    uint32_t completion_id;
    uint32_t bytes_to_send;
    uint64_t remaining;
    int bytes_requested;
    int bytes_read;
    int rv;

    if (rdpdr->bridge_active_job == 0 || rdpdr->bridge_active_fd < 0)
    {
        return 1;
    }
    if (rdpdr->bridge_active_offset >=
            (uint64_t)rdpdr->bridge_active_job->bytes)
    {
        return ohos_rdpdr_print_bridge_send_close(rdpdr);
    }

    remaining = (uint64_t)rdpdr->bridge_active_job->bytes -
                rdpdr->bridge_active_offset;
    bytes_requested = remaining > sizeof(buffer) ?
                      (int)sizeof(buffer) : (int)remaining;
    bytes_read = g_file_read(rdpdr->bridge_active_fd, buffer,
                             bytes_requested);
    if (bytes_read <= 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: bridge read spool failed file=%s offset=%llu expected=%lld read=%d",
            rdpdr->bridge_active_job->file_name,
            (unsigned long long)rdpdr->bridge_active_offset,
            rdpdr->bridge_active_job->bytes, bytes_read);
        return 1;
    }
    bytes_to_send = (uint32_t)bytes_read;
    completion_id = ohos_rdpdr_print_bridge_next_completion_id(rdpdr);

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 64 + bytes_to_send);
    ohos_rdpdr_print_bridge_insert_device_io(
        s, rdpdr->bridge_active_device_id, rdpdr->bridge_active_file_id,
        completion_id, IRP_MJ_WRITE);
    out_uint32_le(s, bytes_to_send);
    out_uint64_le(s, rdpdr->bridge_active_offset);
    out_uint8s(s, 20);
    out_uint8a(s, buffer, bytes_to_send);

    rdpdr->bridge_active_completion_id = completion_id;
    rdpdr->bridge_active_last_write_bytes = bytes_to_send;
    rdpdr->bridge_active_state = OHOS_RDPDR_PRINT_BRIDGE_STATE_WRITE;
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "bridge printer write");
    free_stream(s);
    LOG(rv == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpdr: bridge write sent device=0x%08x file_id=0x%08x completion=0x%08x offset=%llu bytes=%u rv=%d",
        (unsigned int)rdpdr->bridge_active_device_id,
        (unsigned int)rdpdr->bridge_active_file_id,
        (unsigned int)completion_id,
        (unsigned long long)rdpdr->bridge_active_offset,
        (unsigned int)bytes_to_send, rv);
    return rv;
}

static void
ohos_rdpdr_print_bridge_fail_active(struct ohos_rdpdr_print *rdpdr,
                                    const char *reason)
{
    struct ohos_rdpdr_print_bridge_job *job = rdpdr->bridge_active_job;

    if (rdpdr->bridge_active_fd >= 0)
    {
        (void)g_file_close(rdpdr->bridge_active_fd);
    }
    if (job != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: bridge job failed reason=%s file=%s bytes=%lld offset=%llu state=%d",
            reason == 0 ? "" : reason, job->file_name, job->bytes,
            (unsigned long long)rdpdr->bridge_active_offset,
            rdpdr->bridge_active_state);
        ohos_rdpdr_print_bridge_free_job(job, 1);
        rdpdr->bridge_jobs_failed++;
    }
    ohos_rdpdr_print_bridge_reset_active(rdpdr);
}

static void
ohos_rdpdr_print_bridge_complete_active(struct ohos_rdpdr_print *rdpdr)
{
    struct ohos_rdpdr_print_bridge_job *job = rdpdr->bridge_active_job;

    if (rdpdr->bridge_active_fd >= 0)
    {
        (void)g_file_close(rdpdr->bridge_active_fd);
    }
    if (job != 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpdr: bridge job forwarded file=%s bytes=%lld device=0x%08x file_id=0x%08x",
            job->file_name, job->bytes,
            (unsigned int)rdpdr->bridge_active_device_id,
            (unsigned int)rdpdr->bridge_active_file_id);
        ohos_rdpdr_print_bridge_free_job(job, 1);
        rdpdr->bridge_jobs_forwarded++;
    }
    ohos_rdpdr_print_bridge_reset_active(rdpdr);
}

static int
ohos_rdpdr_print_bridge_begin_next_job(struct ohos_rdpdr_print *rdpdr,
                                       const char *reason)
{
    struct ohos_rdpdr_print_bridge_job *job;
    int fd;
    int rv;

    if (rdpdr->bridge_active_state != OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE ||
            rdpdr->bridge_queue_head == 0)
    {
        return 0;
    }
    if (!rdpdr->connected || !rdpdr->channel_ready ||
            rdpdr->printer_device_count == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.rdpdr: bridge waiting reason=%s connected=%d ready=%d printers=%u queued=%u",
            reason == 0 ? "" : reason, rdpdr->connected,
            rdpdr->channel_ready, rdpdr->printer_device_count,
            rdpdr->bridge_queue_count);
        return 0;
    }

    job = rdpdr->bridge_queue_head;
    rdpdr->bridge_queue_head = job->next;
    if (rdpdr->bridge_queue_head == 0)
    {
        rdpdr->bridge_queue_tail = 0;
    }
    job->next = 0;
    rdpdr->bridge_queue_count--;

    fd = g_file_open_ro(job->path);
    if (fd < 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: bridge open spool failed file=%s path=%s",
            job->file_name, job->path);
        ohos_rdpdr_print_bridge_free_job(job, 1);
        rdpdr->bridge_jobs_failed++;
        return 1;
    }

    rdpdr->bridge_active_job = job;
    rdpdr->bridge_active_fd = fd;
    rdpdr->bridge_active_device_id = rdpdr->printer_device_ids[0];
    rdpdr->bridge_active_file_id = 0;
    rdpdr->bridge_active_offset = 0;
    rdpdr->bridge_active_last_write_bytes = 0;
    rv = ohos_rdpdr_print_bridge_send_create(rdpdr);
    if (rv != 0)
    {
        ohos_rdpdr_print_bridge_fail_active(rdpdr, "send-create");
        return 1;
    }
    return 0;
}

static int
ohos_rdpdr_print_bridge_pump(struct ohos_rdpdr_print *rdpdr,
                             const char *reason)
{
    if (rdpdr == 0)
    {
        return 0;
    }
    while (rdpdr->bridge_active_state ==
            OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE &&
            rdpdr->bridge_queue_head != 0 &&
            rdpdr->connected && rdpdr->channel_ready &&
            rdpdr->printer_device_count > 0)
    {
        unsigned int before = rdpdr->bridge_queue_count;
        (void)ohos_rdpdr_print_bridge_begin_next_job(rdpdr, reason);
        if (rdpdr->bridge_active_state !=
                OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE ||
                rdpdr->bridge_queue_count == before)
        {
            break;
        }
    }
    return 0;
}

static int
ohos_rdpdr_print_bridge_enqueue_recv(struct ohos_rdpdr_print *rdpdr)
{
    struct ohos_rdpdr_print_bridge_job *job;

    if (rdpdr->bridge_recv_bytes <= 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: bridge dropped empty local print job file=%s",
            rdpdr->bridge_recv_file_name);
        ohos_rdpdr_print_bridge_delete_file(rdpdr->bridge_recv_path);
        rdpdr->bridge_jobs_dropped++;
        ohos_rdpdr_print_bridge_reset_recv(rdpdr);
        return 1;
    }
    if (rdpdr->bridge_queue_count >= OHOS_RDPDR_PRINT_BRIDGE_QUEUE_MAX)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: bridge queue full dropping file=%s bytes=%lld queued=%u",
            rdpdr->bridge_recv_file_name, rdpdr->bridge_recv_bytes,
            rdpdr->bridge_queue_count);
        ohos_rdpdr_print_bridge_delete_file(rdpdr->bridge_recv_path);
        rdpdr->bridge_jobs_dropped++;
        ohos_rdpdr_print_bridge_reset_recv(rdpdr);
        return 1;
    }

    job = (struct ohos_rdpdr_print_bridge_job *)
          g_malloc(sizeof(struct ohos_rdpdr_print_bridge_job), 1);
    if (job == 0)
    {
        ohos_rdpdr_print_bridge_delete_file(rdpdr->bridge_recv_path);
        rdpdr->bridge_jobs_failed++;
        ohos_rdpdr_print_bridge_reset_recv(rdpdr);
        return 1;
    }
    g_strncpy(job->file_name, rdpdr->bridge_recv_file_name,
              sizeof(job->file_name) - 1);
    g_strncpy(job->path, rdpdr->bridge_recv_path, sizeof(job->path) - 1);
    job->bytes = rdpdr->bridge_recv_bytes;
    job->next = 0;
    if (rdpdr->bridge_queue_tail != 0)
    {
        rdpdr->bridge_queue_tail->next = job;
    }
    else
    {
        rdpdr->bridge_queue_head = job;
    }
    rdpdr->bridge_queue_tail = job;
    rdpdr->bridge_queue_count++;
    rdpdr->bridge_jobs_received++;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: bridge queued local print job file=%s bytes=%lld queued=%u printers=%u",
        job->file_name, job->bytes, rdpdr->bridge_queue_count,
        rdpdr->printer_device_count);
    ohos_rdpdr_print_bridge_reset_recv(rdpdr);
    return 0;
}

static int
ohos_rdpdr_print_bridge_finish_recv(struct ohos_rdpdr_print *rdpdr)
{
    int rv;

    if (rdpdr->bridge_recv_fd >= 0)
    {
        (void)g_file_close(rdpdr->bridge_recv_fd);
        rdpdr->bridge_recv_fd = -1;
    }
    ohos_rdpdr_print_bridge_close_client(rdpdr);
    rv = ohos_rdpdr_print_bridge_enqueue_recv(rdpdr);
    rv |= ohos_rdpdr_print_bridge_pump(rdpdr, "local-print-job");
    return rv;
}

static int
ohos_rdpdr_print_bridge_fail_recv(struct ohos_rdpdr_print *rdpdr,
                                  const char *reason)
{
    LOG(LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpdr: bridge receive failed reason=%s file=%s bytes=%lld",
        reason == 0 ? "" : reason, rdpdr->bridge_recv_file_name,
        rdpdr->bridge_recv_bytes);
    if (rdpdr->bridge_recv_fd >= 0)
    {
        (void)g_file_close(rdpdr->bridge_recv_fd);
        rdpdr->bridge_recv_fd = -1;
    }
    ohos_rdpdr_print_bridge_close_client(rdpdr);
    ohos_rdpdr_print_bridge_delete_file(rdpdr->bridge_recv_path);
    rdpdr->bridge_jobs_failed++;
    ohos_rdpdr_print_bridge_reset_recv(rdpdr);
    return 1;
}

static int
ohos_rdpdr_print_bridge_begin_recv(struct ohos_rdpdr_print *rdpdr,
                                   int client_sck)
{
    int status;
    int fd;
    tintptr wait_obj;

    status = ohos_print_make_spool_file(rdpdr->bridge_recv_file_name,
                                        sizeof(rdpdr->bridge_recv_file_name),
                                        rdpdr->bridge_recv_path,
                                        sizeof(rdpdr->bridge_recv_path));
    if (status != XRDP_OHOS_BACKEND_STATUS_OK)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: bridge allocate spool failed status=%d",
            status);
        return 1;
    }
    fd = g_file_open_ex(rdpdr->bridge_recv_path, 0, 1, 1, 1);
    if (fd < 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: bridge open receive spool failed path=%s",
            rdpdr->bridge_recv_path);
        ohos_rdpdr_print_bridge_delete_file(rdpdr->bridge_recv_path);
        ohos_rdpdr_print_bridge_reset_recv(rdpdr);
        return 1;
    }
    (void)g_sck_set_non_blocking(client_sck);
    wait_obj = g_create_wait_obj_from_socket((tintptr)client_sck, 0);
    if (wait_obj == 0)
    {
        (void)g_file_close(fd);
        ohos_rdpdr_print_bridge_delete_file(rdpdr->bridge_recv_path);
        ohos_rdpdr_print_bridge_reset_recv(rdpdr);
        return 1;
    }

    rdpdr->bridge_client_sck = client_sck;
    rdpdr->bridge_client_wait_obj = wait_obj;
    rdpdr->bridge_recv_fd = fd;
    rdpdr->bridge_recv_bytes = 0;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: bridge accepted local print job socket=%d file=%s path=%s",
        client_sck, rdpdr->bridge_recv_file_name, rdpdr->bridge_recv_path);
    return 0;
}

static int
ohos_rdpdr_print_bridge_write_recv_data(struct ohos_rdpdr_print *rdpdr,
                                        const char *data, int bytes)
{
    int total_written = 0;

    if (rdpdr->bridge_recv_bytes + bytes >
            OHOS_RDPDR_MAX_PRINT_JOB_BYTES)
    {
        return 1;
    }
    while (total_written < bytes)
    {
        int written = g_file_write(rdpdr->bridge_recv_fd,
                                   data + total_written,
                                   bytes - total_written);
        if (written <= 0)
        {
            return 1;
        }
        total_written += written;
    }
    rdpdr->bridge_recv_bytes += total_written;
    return 0;
}

static int
ohos_rdpdr_print_bridge_read_client(struct ohos_rdpdr_print *rdpdr)
{
    char buffer[OHOS_RDPDR_PRINT_BRIDGE_RECV_BYTES];
    int rv = 0;

    if (rdpdr == 0 || rdpdr->bridge_client_sck <= 0)
    {
        return 0;
    }

    for (;;)
    {
        int bytes = g_sck_recv(rdpdr->bridge_client_sck, buffer,
                               (unsigned int)sizeof(buffer), 0);
        if (bytes > 0)
        {
            if (ohos_rdpdr_print_bridge_write_recv_data(rdpdr, buffer,
                                                        bytes) != 0)
            {
                return ohos_rdpdr_print_bridge_fail_recv(rdpdr,
                                                         "write-spool");
            }
            continue;
        }
        if (bytes == 0)
        {
            return ohos_rdpdr_print_bridge_finish_recv(rdpdr);
        }
        if (g_sck_last_error_would_block(rdpdr->bridge_client_sck))
        {
            break;
        }
        rv = ohos_rdpdr_print_bridge_fail_recv(rdpdr, "socket-read");
        break;
    }
    return rv;
}

static int
ohos_rdpdr_print_bridge_accept_client(struct ohos_rdpdr_print *rdpdr)
{
    int rv = 0;

    if (rdpdr == 0 || rdpdr->bridge_listen_sck <= 0)
    {
        return 0;
    }

    while (g_sck_can_recv(rdpdr->bridge_listen_sck, 0))
    {
        int client_sck = g_sck_accept(rdpdr->bridge_listen_sck);
        if (client_sck <= 0)
        {
            if (!g_sck_last_error_would_block(rdpdr->bridge_listen_sck))
            {
                LOG(LOG_LEVEL_WARNING,
                    "xrdp.ohos.rdpdr: bridge accept failed listener=%d",
                    rdpdr->bridge_listen_sck);
                rv = 1;
            }
            break;
        }

        if (rdpdr->bridge_client_sck > 0 || rdpdr->bridge_recv_fd >= 0)
        {
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.rdpdr: bridge busy dropping incoming local print socket=%d",
                client_sck);
            g_sck_close(client_sck);
            rdpdr->bridge_jobs_dropped++;
            continue;
        }
        if (ohos_rdpdr_print_bridge_begin_recv(rdpdr, client_sck) != 0)
        {
            g_sck_close(client_sck);
            rdpdr->bridge_jobs_failed++;
            rv = 1;
            continue;
        }
        rv |= ohos_rdpdr_print_bridge_read_client(rdpdr);
    }
    return rv;
}

static int
ohos_rdpdr_print_bridge_start_listener(struct ohos_rdpdr_print *rdpdr)
{
    int sck;
    tintptr wait_obj;

    if (rdpdr == 0)
    {
        return 1;
    }
    if (rdpdr->bridge_listen_sck > 0)
    {
        return 0;
    }

    sck = g_tcp_socket();
    if (sck < 0)
    {
        return 1;
    }
    (void)g_sck_set_reuseaddr(sck);
    (void)g_tcp_set_no_delay(sck);
    if (g_tcp_bind_address(sck, OHOS_RDPDR_PRINT_BRIDGE_PORT,
                           OHOS_RDPDR_PRINT_BRIDGE_HOST) != 0 ||
            g_sck_listen(sck) != 0 ||
            g_sck_set_non_blocking(sck) != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: bridge listen failed address=%s:%s",
            OHOS_RDPDR_PRINT_BRIDGE_HOST, OHOS_RDPDR_PRINT_BRIDGE_PORT);
        g_sck_close(sck);
        return 1;
    }
    wait_obj = g_create_wait_obj_from_socket((tintptr)sck, 0);
    if (wait_obj == 0)
    {
        g_sck_close(sck);
        return 1;
    }
    rdpdr->bridge_listen_sck = sck;
    rdpdr->bridge_listen_wait_obj = wait_obj;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: bridge listening address=%s:%s",
        OHOS_RDPDR_PRINT_BRIDGE_HOST, OHOS_RDPDR_PRINT_BRIDGE_PORT);
    return 0;
}

static void
ohos_rdpdr_print_bridge_stop_listener(struct ohos_rdpdr_print *rdpdr)
{
    if (rdpdr == 0)
    {
        return;
    }
    ohos_rdpdr_print_bridge_close_client(rdpdr);
    if (rdpdr->bridge_recv_fd >= 0)
    {
        (void)g_file_close(rdpdr->bridge_recv_fd);
        rdpdr->bridge_recv_fd = -1;
        ohos_rdpdr_print_bridge_delete_file(rdpdr->bridge_recv_path);
        ohos_rdpdr_print_bridge_reset_recv(rdpdr);
    }
    if (rdpdr->bridge_listen_wait_obj != 0)
    {
        g_delete_wait_obj_from_socket(rdpdr->bridge_listen_wait_obj);
        rdpdr->bridge_listen_wait_obj = 0;
    }
    if (rdpdr->bridge_listen_sck > 0)
    {
        g_sck_close(rdpdr->bridge_listen_sck);
        rdpdr->bridge_listen_sck = -1;
    }
}

static int
ohos_rdpdr_print_send_server_announce(struct ohos_rdpdr_print *rdpdr)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 64);
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_SERVER_ANNOUNCE);
    out_uint16_le(s, OHOS_RDPDR_SERVER_MAJOR_VERSION);
    out_uint16_le(s, OHOS_RDPDR_SERVER_MINOR_VERSION);
    out_uint32_le(s, rdpdr->server_client_id);
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "server announce");
    free_stream(s);
    return rv;
}

static int
ohos_rdpdr_print_send_client_id_confirm(struct ohos_rdpdr_print *rdpdr)
{
    struct stream *s;
    uint16_t minor;
    int rv;

    minor = rdpdr->client_rdp_version == 0 ?
            OHOS_RDPDR_SERVER_MINOR_VERSION : rdpdr->client_rdp_version;
    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 64);
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_CLIENTID_CONFIRM);
    out_uint16_le(s, OHOS_RDPDR_SERVER_MAJOR_VERSION);
    out_uint16_le(s, minor);
    out_uint32_le(s, rdpdr->client_id);
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "client id confirm");
    free_stream(s);
    return rv;
}

static void
ohos_rdpdr_print_write_general_cap(struct stream *s,
                                   struct ohos_rdpdr_print *rdpdr)
{
    uint16_t minor = rdpdr->client_rdp_version == 0 ?
                     OHOS_RDPDR_SERVER_MINOR_VERSION :
                     rdpdr->client_rdp_version;

    out_uint16_le(s, CAP_GENERAL_TYPE);
    out_uint16_le(s, OHOS_RDPDR_CAP_GENERAL_LENGTH);
    out_uint32_le(s, OHOS_RDPDR_GENERAL_CAP_VERSION);
    out_uint32_le(s, 0); /* osType */
    out_uint32_le(s, 0); /* osVersion */
    out_uint16_le(s, OHOS_RDPDR_SERVER_MAJOR_VERSION);
    out_uint16_le(s, minor);
    out_uint32_le(s, 0xffff); /* ioCode1 */
    out_uint32_le(s, 0); /* ioCode2 */
    out_uint32_le(s, RDPDR_USER_LOGGEDON_PDU);
    out_uint32_le(s, 0); /* extraFlags1 */
    out_uint32_le(s, 0); /* extraFlags2 */
    out_uint32_le(s, RDPDR_DTYP_PRINT); /* SpecialTypeDeviceCap */
}

static void
ohos_rdpdr_print_write_simple_cap(struct stream *s, uint16_t type,
                                  uint32_t version)
{
    out_uint16_le(s, type);
    out_uint16_le(s, OHOS_RDPDR_CAP_SIMPLE_LENGTH);
    out_uint32_le(s, version);
}

static int
ohos_rdpdr_print_send_capability(struct ohos_rdpdr_print *rdpdr)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 128);
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_SERVER_CAPABILITY);
    out_uint16_le(s, 2); /* general, printer */
    out_uint16_le(s, 0);
    ohos_rdpdr_print_write_general_cap(s, rdpdr);
    ohos_rdpdr_print_write_simple_cap(s, CAP_PRINTER_TYPE,
                                      OHOS_RDPDR_PRINTER_CAP_VERSION);
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "server capability");
    free_stream(s);
    return rv;
}

static int
ohos_rdpdr_print_send_user_logged_on(struct ohos_rdpdr_print *rdpdr)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 16);
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_USER_LOGGEDON);
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "user logged on");
    free_stream(s);
    return rv;
}

static int
ohos_rdpdr_print_send_device_reply(struct ohos_rdpdr_print *rdpdr,
                                   uint32_t device_id, enum NTSTATUS status)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 32);
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_DEVICE_REPLY);
    out_uint32_le(s, device_id);
    out_uint32_le(s, status);
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "device reply");
    free_stream(s);
    return rv;
}

static int
ohos_rdpdr_print_send_io_completion(struct ohos_rdpdr_print *rdpdr,
                                    uint32_t device_id,
                                    uint32_t completion_id,
                                    enum NTSTATUS status,
                                    const char *name)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 32);
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_DEVICE_IOCOMPLETION);
    out_uint32_le(s, device_id);
    out_uint32_le(s, completion_id);
    out_uint32_le(s, (uint32_t)status);
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, name);
    free_stream(s);
    return rv;
}

static int
ohos_rdpdr_print_send_create_completion(struct ohos_rdpdr_print *rdpdr,
                                        uint32_t device_id,
                                        uint32_t completion_id,
                                        enum NTSTATUS status,
                                        uint32_t file_id)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 32);
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_DEVICE_IOCOMPLETION);
    out_uint32_le(s, device_id);
    out_uint32_le(s, completion_id);
    out_uint32_le(s, (uint32_t)status);
    out_uint32_le(s, file_id);
    out_uint8(s, status == STATUS_SUCCESS ? 1 : 0);
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "printer create completion");
    free_stream(s);
    return rv;
}

static int
ohos_rdpdr_print_send_write_completion(struct ohos_rdpdr_print *rdpdr,
                                       uint32_t device_id,
                                       uint32_t completion_id,
                                       enum NTSTATUS status,
                                       uint32_t bytes_written)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 32);
    out_uint16_le(s, RDPDR_CTYP_CORE);
    out_uint16_le(s, PAKID_CORE_DEVICE_IOCOMPLETION);
    out_uint32_le(s, device_id);
    out_uint32_le(s, completion_id);
    out_uint32_le(s, (uint32_t)status);
    out_uint32_le(s, bytes_written);
    rv = ohos_rdpdr_print_send_stream(rdpdr, s, "printer write completion");
    free_stream(s);
    return rv;
}

static int
ohos_rdpdr_print_is_printer_device(const struct ohos_rdpdr_print *rdpdr,
                                   uint32_t device_id)
{
    unsigned int index;

    if (rdpdr == 0)
    {
        return 0;
    }
    for (index = 0; index < rdpdr->printer_device_count; index++)
    {
        if (rdpdr->printer_device_ids[index] == device_id)
        {
            return 1;
        }
    }
    return 0;
}

static int
ohos_rdpdr_print_register_printer_device(struct ohos_rdpdr_print *rdpdr,
                                         uint32_t device_id,
                                         int is_default)
{
    unsigned int index;

    if (ohos_rdpdr_print_is_printer_device(rdpdr, device_id))
    {
        if (is_default && rdpdr->printer_device_count > 1 &&
                rdpdr->printer_device_ids[0] != device_id)
        {
            uint32_t previous = rdpdr->printer_device_ids[0];
            rdpdr->printer_device_ids[0] = device_id;
            for (index = 1; index < rdpdr->printer_device_count; index++)
            {
                if (rdpdr->printer_device_ids[index] == device_id)
                {
                    rdpdr->printer_device_ids[index] = previous;
                    break;
                }
            }
        }
        return 0;
    }
    if (rdpdr->printer_device_count >= OHOS_RDPDR_MAX_PRINTER_DEVICES)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: printer device table full id=0x%08x",
            (unsigned int)device_id);
        return 1;
    }
    if (is_default)
    {
        for (index = rdpdr->printer_device_count; index > 0; index--)
        {
            rdpdr->printer_device_ids[index] =
                rdpdr->printer_device_ids[index - 1];
        }
        rdpdr->printer_device_ids[0] = device_id;
        rdpdr->printer_device_count++;
    }
    else
    {
        rdpdr->printer_device_ids[rdpdr->printer_device_count++] = device_id;
    }
    return 0;
}

static void
ohos_rdpdr_print_unregister_printer_device(struct ohos_rdpdr_print *rdpdr,
                                           uint32_t device_id)
{
    unsigned int index;

    if (rdpdr == 0)
    {
        return;
    }
    for (index = 0; index < rdpdr->printer_device_count; index++)
    {
        if (rdpdr->printer_device_ids[index] == device_id)
        {
            unsigned int tail = rdpdr->printer_device_count - 1;
            rdpdr->printer_device_ids[index] = rdpdr->printer_device_ids[tail];
            rdpdr->printer_device_ids[tail] = 0;
            rdpdr->printer_device_count--;
            return;
        }
    }
}

static void
ohos_rdpdr_print_reset_active_job(struct ohos_rdpdr_print *rdpdr)
{
    rdpdr->active_device_id = 0;
    rdpdr->active_file_id = 0;
    rdpdr->active_job_fd = -1;
    rdpdr->active_job_file_name[0] = '\0';
    rdpdr->active_job_path[0] = '\0';
    rdpdr->active_job_bytes = 0;
}

static enum NTSTATUS
ohos_rdpdr_print_finish_active_job(struct ohos_rdpdr_print *rdpdr,
                                   const char *reason, int submit)
{
    enum NTSTATUS status = STATUS_SUCCESS;

    if (rdpdr == 0 || rdpdr->active_job_fd < 0)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    (void)g_file_close(rdpdr->active_job_fd);
    rdpdr->active_job_fd = -1;

    if (submit)
    {
        int print_status;

        if (rdpdr->active_job_bytes <= 0)
        {
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.rdpdr: printer job empty reason=%s file=%s",
                reason == 0 ? "" : reason, rdpdr->active_job_file_name);
            rdpdr->failed_print_jobs++;
            status = STATUS_UNSUCCESSFUL;
        }
        else
        {
            print_status = ohos_print_spool_file(rdpdr->active_job_file_name,
                                                 rdpdr->local_printer_id);
            if (print_status == XRDP_OHOS_BACKEND_STATUS_OK)
            {
                rdpdr->print_jobs++;
                LOG(LOG_LEVEL_INFO,
                    "xrdp.ohos.rdpdr: printer job submitted reason=%s file=%s bytes=%lld printer=%s",
                    reason == 0 ? "" : reason, rdpdr->active_job_file_name,
                    rdpdr->active_job_bytes, rdpdr->local_printer_id);
            }
            else
            {
                rdpdr->failed_print_jobs++;
                status = STATUS_UNSUCCESSFUL;
                LOG(LOG_LEVEL_WARNING,
                    "xrdp.ohos.rdpdr: printer job submit failed reason=%s file=%s bytes=%lld status=%d",
                    reason == 0 ? "" : reason, rdpdr->active_job_file_name,
                    rdpdr->active_job_bytes, print_status);
            }
        }
    }
    else
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpdr: printer job discarded reason=%s file=%s bytes=%lld",
            reason == 0 ? "" : reason, rdpdr->active_job_file_name,
            rdpdr->active_job_bytes);
    }

    if (!submit || rdpdr->active_job_bytes <= 0)
    {
        if (rdpdr->active_job_path[0] != '\0')
        {
            (void)g_file_delete(rdpdr->active_job_path);
        }
    }
    ohos_rdpdr_print_reset_active_job(rdpdr);
    return status;
}

static enum NTSTATUS
ohos_rdpdr_print_start_job(struct ohos_rdpdr_print *rdpdr,
                           uint32_t device_id, uint32_t *file_id)
{
    uint32_t new_file_id = 0;
    int status;
    int fd;

    if (!ohos_rdpdr_print_is_printer_device(rdpdr, device_id))
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (rdpdr->local_printer_id[0] == '\0' &&
            ohos_rdpdr_print_probe(rdpdr, "printer-create") !=
            XRDP_OHOS_BACKEND_STATUS_OK)
    {
        return STATUS_UNSUCCESSFUL;
    }
    if (rdpdr->active_job_fd >= 0)
    {
        (void)ohos_rdpdr_print_finish_active_job(rdpdr,
                                                 "new-create", 0);
    }

    status = ohos_print_make_spool_file(rdpdr->active_job_file_name,
                                        sizeof(rdpdr->active_job_file_name),
                                        rdpdr->active_job_path,
                                        sizeof(rdpdr->active_job_path));
    if (status != XRDP_OHOS_BACKEND_STATUS_OK)
    {
        return STATUS_UNSUCCESSFUL;
    }

    fd = g_file_open_ex(rdpdr->active_job_path, 0, 1, 1, 1);
    if (fd < 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: open printer spool file failed path=%s",
            rdpdr->active_job_path);
        rdpdr->active_job_file_name[0] = '\0';
        rdpdr->active_job_path[0] = '\0';
        return STATUS_UNSUCCESSFUL;
    }

    g_random((char *)&new_file_id, sizeof(new_file_id));
    if (new_file_id == 0)
    {
        new_file_id = 1;
    }
    rdpdr->active_device_id = device_id;
    rdpdr->active_file_id = new_file_id;
    rdpdr->active_job_fd = fd;
    rdpdr->active_job_bytes = 0;
    if (file_id != 0)
    {
        *file_id = new_file_id;
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: printer job opened device=0x%08x file_id=0x%08x file=%s path=%s",
        (unsigned int)device_id, (unsigned int)new_file_id,
        rdpdr->active_job_file_name, rdpdr->active_job_path);
    return STATUS_SUCCESS;
}

static enum NTSTATUS
ohos_rdpdr_print_write_job(struct ohos_rdpdr_print *rdpdr,
                           uint32_t device_id, uint32_t file_id,
                           uint64_t offset, const char *data,
                           uint32_t length, uint32_t *bytes_written)
{
    uint32_t total_written = 0;

    if (bytes_written != 0)
    {
        *bytes_written = 0;
    }
    if (rdpdr == 0 || rdpdr->active_job_fd < 0 ||
            rdpdr->active_device_id != device_id ||
            rdpdr->active_file_id != file_id)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (data == 0 || length > INT_MAX ||
            offset > (uint64_t)INT_MAX ||
            offset + (uint64_t)length > OHOS_RDPDR_MAX_PRINT_JOB_BYTES)
    {
        return STATUS_UNSUCCESSFUL;
    }
    if (offset != (uint64_t)rdpdr->active_job_bytes)
    {
        if (g_file_seek(rdpdr->active_job_fd, (int)offset) < 0)
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    while (total_written < length)
    {
        int written = g_file_write(rdpdr->active_job_fd,
                                   data + total_written,
                                   (int)(length - total_written));
        if (written <= 0)
        {
            break;
        }
        total_written += (uint32_t)written;
    }

    if (bytes_written != 0)
    {
        *bytes_written = total_written;
    }
    if (total_written != length)
    {
        return STATUS_UNSUCCESSFUL;
    }
    if ((long long)(offset + total_written) > rdpdr->active_job_bytes)
    {
        rdpdr->active_job_bytes = (long long)(offset + total_written);
    }
    return STATUS_SUCCESS;
}

static int
ohos_rdpdr_print_probe(struct ohos_rdpdr_print *rdpdr, const char *reason)
{
    int status = ohos_print_probe(rdpdr->local_printer_id,
                                  sizeof(rdpdr->local_printer_id));

    LOG(status == XRDP_OHOS_BACKEND_STATUS_OK ?
            LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpdr: print probe reason=%s status=%d local_printer=%s",
        reason == 0 ? "" : reason, status, rdpdr->local_printer_id);
    return status;
}

static int
ohos_rdpdr_print_process_client_id_confirm(struct ohos_rdpdr_print *rdpdr,
                                           struct stream *s)
{
    uint16_t major;
    uint16_t minor;
    uint32_t client_id;

    if (!s_check_rem_and_log(s, 8,
                             "OHOS rdpdr client id confirm"))
    {
        rdpdr->errors++;
        return 1;
    }
    in_uint16_le(s, major);
    in_uint16_le(s, minor);
    in_uint32_le(s, client_id);
    rdpdr->client_rdp_version = minor;
    rdpdr->client_id = client_id;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: client id confirm major=%u minor=0x%04x client_id=0x%08x",
        (unsigned int)major, (unsigned int)minor, (unsigned int)client_id);
    return 0;
}

static int
ohos_rdpdr_print_process_client_name(struct ohos_rdpdr_print *rdpdr,
                                     struct stream *s)
{
    uint32_t unicode_flag = 0;
    uint32_t code_page = 0;
    uint32_t name_len = 0;

    if (s_check_rem(s, 12))
    {
        in_uint32_le(s, unicode_flag);
        in_uint32_le(s, code_page);
        in_uint32_le(s, name_len);
        if (name_len > 0 && name_len <= (uint32_t)(s->end - s->p))
        {
            in_uint8s(s, name_len);
        }
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: client name unicode=%u code_page=%u bytes=%u",
        (unsigned int)unicode_flag, (unsigned int)code_page,
        (unsigned int)name_len);
    (void)ohos_rdpdr_print_send_capability(rdpdr);
    return ohos_rdpdr_print_send_client_id_confirm(rdpdr);
}

static int
ohos_rdpdr_print_process_general_cap(struct ohos_rdpdr_print *rdpdr,
                                     struct stream *s, uint16_t length)
{
    uint32_t os_type;
    uint32_t os_version;
    uint16_t major;
    uint16_t minor;
    uint32_t io_code1;
    uint32_t io_code2;
    uint32_t extended_pdu;
    uint32_t extra_flags1;
    uint32_t extra_flags2;
    uint32_t special_type;

    if (length < OHOS_RDPDR_CAP_GENERAL_LENGTH ||
            !s_check_rem_and_log(s, OHOS_RDPDR_CAP_GENERAL_LENGTH - 8,
                                 "OHOS rdpdr general cap"))
    {
        return 1;
    }
    in_uint32_le(s, os_type);
    in_uint32_le(s, os_version);
    in_uint16_le(s, major);
    in_uint16_le(s, minor);
    in_uint32_le(s, io_code1);
    in_uint32_le(s, io_code2);
    in_uint32_le(s, extended_pdu);
    in_uint32_le(s, extra_flags1);
    in_uint32_le(s, extra_flags2);
    in_uint32_le(s, special_type);
    rdpdr->client_extended_pdu = extended_pdu;
    if (rdpdr->client_rdp_version == 0)
    {
        rdpdr->client_rdp_version = minor;
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: client general cap os=(%u,%u) proto=%u.0x%04x io=0x%08x extended=0x%08x extra=0x%08x special=0x%08x",
        (unsigned int)os_type, (unsigned int)os_version,
        (unsigned int)major, (unsigned int)minor,
        (unsigned int)io_code1, (unsigned int)extended_pdu,
        (unsigned int)extra_flags1, (unsigned int)special_type);
    if (length > OHOS_RDPDR_CAP_GENERAL_LENGTH)
    {
        in_uint8s(s, length - OHOS_RDPDR_CAP_GENERAL_LENGTH);
    }
    return 0;
}

static int
ohos_rdpdr_print_process_client_capability(struct ohos_rdpdr_print *rdpdr,
                                           struct stream *s)
{
    uint16_t count;
    uint16_t index;
    uint16_t padding;

    if (!s_check_rem_and_log(s, 4,
                             "OHOS rdpdr client capability header"))
    {
        rdpdr->errors++;
        return 1;
    }
    in_uint16_le(s, count);
    in_uint16_le(s, padding);
    (void)padding;

    for (index = 0; index < count; index++)
    {
        uint16_t type;
        uint16_t length;
        uint32_t version;
        char *cap_end;

        if (!s_check_rem_and_log(s, 8, "OHOS rdpdr capset header"))
        {
            rdpdr->errors++;
            return 1;
        }
        in_uint16_le(s, type);
        in_uint16_le(s, length);
        in_uint32_le(s, version);
        if (length < 8 || !s_check_rem_and_log(s, length - 8,
                                               "OHOS rdpdr capset body"))
        {
            rdpdr->errors++;
            return 1;
        }
        cap_end = s->p + (length - 8);
        if (type == CAP_GENERAL_TYPE)
        {
            (void)ohos_rdpdr_print_process_general_cap(rdpdr, s, length);
        }
        else
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.rdpdr: client cap type=%u version=%u length=%u",
                (unsigned int)type, (unsigned int)version,
                (unsigned int)length);
        }
        s->p = cap_end;
    }

    if ((rdpdr->client_extended_pdu & RDPDR_USER_LOGGEDON_PDU) != 0)
    {
        return ohos_rdpdr_print_send_user_logged_on(rdpdr);
    }
    return ohos_rdpdr_print_send_client_id_confirm(rdpdr);
}

static const char *
ohos_rdpdr_print_device_type_name(uint32_t device_type)
{
    switch (device_type)
    {
        case RDPDR_DTYP_SERIAL:
            return "serial";
        case RDPDR_DTYP_PARALLEL:
            return "parallel";
        case RDPDR_DTYP_PRINT:
            return "printer";
        case RDPDR_DTYP_FILESYSTEM:
            return "filesystem";
        case RDPDR_DTYP_SMARTCARD:
            return "smartcard";
        default:
            return "unknown";
    }
}

static void
ohos_rdpdr_print_trim_dos_name(char *name)
{
    int index;

    if (name == 0)
    {
        return;
    }
    name[8] = '\0';
    for (index = 7; index >= 0; index--)
    {
        if (name[index] == '\0' || name[index] == ' ')
        {
            name[index] = '\0';
        }
        else
        {
            break;
        }
    }
}

static int
ohos_rdpdr_print_process_devlist_announce(struct ohos_rdpdr_print *rdpdr,
                                          struct stream *s)
{
    uint32_t device_count;
    uint32_t index;
    int rv = 0;

    if (!s_check_rem_and_log(s, 4, "OHOS rdpdr devlist announce"))
    {
        rdpdr->errors++;
        return 1;
    }
    in_uint32_le(s, device_count);
    if (device_count > 1024)
    {
        rdpdr->errors++;
        return 1;
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: device announce count=%u",
        (unsigned int)device_count);

    for (index = 0; index < device_count; index++)
    {
        uint32_t device_type;
        uint32_t device_id;
        uint32_t data_len;
        const char *device_data;
        uint32_t printer_flags = 0;
        char printer_name[128];
        char printer_driver[128];
        char preferred_name[9];
        enum NTSTATUS response_status = STATUS_NOT_SUPPORTED;

        if (!s_check_rem_and_log(s, 20, "OHOS rdpdr device announce"))
        {
            rdpdr->errors++;
            return 1;
        }
        in_uint32_le(s, device_type);
        in_uint32_le(s, device_id);
        in_uint8a(s, preferred_name, 8);
        preferred_name[8] = '\0';
        ohos_rdpdr_print_trim_dos_name(preferred_name);
        in_uint32_le(s, data_len);
        if (data_len > (uint32_t)(s->end - s->p))
        {
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.rdpdr: device data length invalid len=%u remaining=%ld",
                (unsigned int)data_len, (long)(s->end - s->p));
            rdpdr->errors++;
            return 1;
        }
        device_data = s->p;
        in_uint8s(s, data_len);

        rdpdr->device_announces++;
        if (device_type == RDPDR_DTYP_PRINT)
        {
            printer_name[0] = '\0';
            printer_driver[0] = '\0';
            if (ohos_rdpdr_print_parse_printer_announce(
                    device_data, data_len, &printer_flags, printer_driver,
                    sizeof(printer_driver), printer_name,
                    sizeof(printer_name)) != 0)
            {
                LOG(LOG_LEVEL_WARNING,
                    "xrdp.ohos.rdpdr: printer announce parse failed id=0x%08x data_len=%u",
                    (unsigned int)device_id, (unsigned int)data_len);
            }
            (void)ohos_rdpdr_print_probe(rdpdr, "printer-device");
            if (ohos_rdpdr_print_register_printer_device(rdpdr,
                    device_id,
                    (printer_flags &
                     OHOS_RDPDR_PRINTER_ANNOUNCE_FLAG_DEFAULTPRINTER) != 0)
                    == 0)
            {
                response_status = STATUS_SUCCESS;
                rdpdr->printer_devices++;
            }
            else
            {
                rdpdr->rejected_devices++;
            }
        }
        else
        {
            rdpdr->rejected_devices++;
        }

        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpdr: device %s name=%s id=0x%08x data_len=%u response=0x%08x",
            ohos_rdpdr_print_device_type_name(device_type),
            preferred_name, (unsigned int)device_id,
            (unsigned int)data_len, (unsigned int)response_status);
        if (device_type == RDPDR_DTYP_PRINT)
        {
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.rdpdr: printer announce id=0x%08x flags=0x%08x default=%d xps=%d printer=%s driver=%s selected=0x%08x",
                (unsigned int)device_id, (unsigned int)printer_flags,
                (printer_flags &
                 OHOS_RDPDR_PRINTER_ANNOUNCE_FLAG_DEFAULTPRINTER) != 0,
                (printer_flags & OHOS_RDPDR_PRINTER_ANNOUNCE_FLAG_XPSFORMAT)
                != 0,
                printer_name, printer_driver,
                rdpdr->printer_device_count > 0 ?
                (unsigned int)rdpdr->printer_device_ids[0] : 0);
        }
        rv |= ohos_rdpdr_print_send_device_reply(rdpdr, device_id,
                                                 response_status);
    }
    rv |= ohos_rdpdr_print_bridge_pump(rdpdr, "device-announce");
    return rv;
}

static int
ohos_rdpdr_print_process_devlist_remove(struct ohos_rdpdr_print *rdpdr,
                                        struct stream *s)
{
    uint32_t device_count;
    uint32_t index;

    if (!s_check_rem_and_log(s, 4, "OHOS rdpdr devlist remove"))
    {
        rdpdr->errors++;
        return 1;
    }
    in_uint32_le(s, device_count);
    if (device_count > 1024)
    {
        rdpdr->errors++;
        return 1;
    }
    if (!s_check_rem_and_log(s, device_count * 4,
                             "OHOS rdpdr devlist remove ids"))
    {
        rdpdr->errors++;
        return 1;
    }
    for (index = 0; index < device_count; index++)
    {
        uint32_t device_id;
        in_uint32_le(s, device_id);
        ohos_rdpdr_print_unregister_printer_device(rdpdr, device_id);
        if (rdpdr->active_job_fd >= 0 &&
                rdpdr->active_device_id == device_id)
        {
            (void)ohos_rdpdr_print_finish_active_job(rdpdr,
                                                     "device-remove", 0);
        }
        if (rdpdr->bridge_active_state !=
                OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE &&
                rdpdr->bridge_active_device_id == device_id)
        {
            ohos_rdpdr_print_bridge_fail_active(rdpdr,
                                                "device-remove");
        }
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpdr: device removed id=0x%08x",
            (unsigned int)device_id);
    }
    (void)ohos_rdpdr_print_bridge_pump(rdpdr, "device-remove");
    return 0;
}

static int
ohos_rdpdr_print_process_device_io_completion(
    struct ohos_rdpdr_print *rdpdr, struct stream *s)
{
    uint32_t device_id;
    uint32_t completion_id;
    uint32_t io_status;

    if (!s_check_rem_and_log(s, 12, "OHOS rdpdr io completion"))
    {
        rdpdr->errors++;
        return 1;
    }
    in_uint32_le(s, device_id);
    in_uint32_le(s, completion_id);
    in_uint32_le(s, io_status);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.rdpdr: io completion device=0x%08x completion=0x%08x status=0x%08x",
        (unsigned int)device_id, (unsigned int)completion_id,
        (unsigned int)io_status);
    if (rdpdr->bridge_active_state !=
            OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE &&
            rdpdr->bridge_active_completion_id == completion_id)
    {
        if (device_id != rdpdr->bridge_active_device_id)
        {
            ohos_rdpdr_print_bridge_fail_active(rdpdr,
                                                "completion-device");
            return ohos_rdpdr_print_bridge_pump(rdpdr,
                                                "completion-device");
        }

        switch (rdpdr->bridge_active_state)
        {
            case OHOS_RDPDR_PRINT_BRIDGE_STATE_CREATE:
            {
                uint32_t file_id = 0;
                if (io_status == STATUS_SUCCESS &&
                        !s_check_rem_and_log(s, 4,
                                             "OHOS rdpdr bridge create completion"))
                {
                    io_status = STATUS_UNSUCCESSFUL;
                }
                if (io_status == STATUS_SUCCESS)
                {
                    in_uint32_le(s, file_id);
                }
                if (io_status == STATUS_SUCCESS)
                {
                    rdpdr->bridge_active_file_id = file_id;
                    LOG(LOG_LEVEL_INFO,
                        "xrdp.ohos.rdpdr: bridge create completed device=0x%08x file_id=0x%08x",
                        (unsigned int)device_id, (unsigned int)file_id);
                    if (ohos_rdpdr_print_bridge_send_next_write(rdpdr) != 0)
                    {
                        ohos_rdpdr_print_bridge_fail_active(
                            rdpdr, "send-write-after-create");
                        return ohos_rdpdr_print_bridge_pump(
                                   rdpdr, "send-write-after-create");
                    }
                    return 0;
                }
                LOG(LOG_LEVEL_WARNING,
                    "xrdp.ohos.rdpdr: bridge create failed device=0x%08x completion=0x%08x status=0x%08x file_id=0x%08x",
                    (unsigned int)device_id, (unsigned int)completion_id,
                    (unsigned int)io_status, (unsigned int)file_id);
                ohos_rdpdr_print_bridge_fail_active(rdpdr,
                                                    "create-completion");
                return ohos_rdpdr_print_bridge_pump(rdpdr,
                                                    "create-completion");
            }

            case OHOS_RDPDR_PRINT_BRIDGE_STATE_WRITE:
            {
                uint32_t bytes_written = 0;
                if (io_status == STATUS_SUCCESS &&
                        !s_check_rem_and_log(s, 4,
                                             "OHOS rdpdr bridge write completion"))
                {
                    io_status = STATUS_UNSUCCESSFUL;
                }
                if (io_status == STATUS_SUCCESS)
                {
                    in_uint32_le(s, bytes_written);
                }
                if (io_status == STATUS_SUCCESS &&
                        bytes_written ==
                        rdpdr->bridge_active_last_write_bytes)
                {
                    rdpdr->bridge_active_offset += bytes_written;
                    rdpdr->bridge_bytes_forwarded += bytes_written;
                    if (ohos_rdpdr_print_bridge_send_next_write(rdpdr) != 0)
                    {
                        ohos_rdpdr_print_bridge_fail_active(
                            rdpdr, "send-next-write");
                        return ohos_rdpdr_print_bridge_pump(
                                   rdpdr, "send-next-write");
                    }
                    return 0;
                }
                LOG(LOG_LEVEL_WARNING,
                    "xrdp.ohos.rdpdr: bridge write failed device=0x%08x completion=0x%08x status=0x%08x written=%u expected=%u",
                    (unsigned int)device_id, (unsigned int)completion_id,
                    (unsigned int)io_status, (unsigned int)bytes_written,
                    (unsigned int)rdpdr->bridge_active_last_write_bytes);
                ohos_rdpdr_print_bridge_fail_active(rdpdr,
                                                    "write-completion");
                return ohos_rdpdr_print_bridge_pump(rdpdr,
                                                    "write-completion");
            }

            case OHOS_RDPDR_PRINT_BRIDGE_STATE_CLOSE:
                if (io_status == STATUS_SUCCESS)
                {
                    ohos_rdpdr_print_bridge_complete_active(rdpdr);
                    return ohos_rdpdr_print_bridge_pump(rdpdr,
                                                        "close-completion");
                }
                LOG(LOG_LEVEL_WARNING,
                    "xrdp.ohos.rdpdr: bridge close failed device=0x%08x completion=0x%08x status=0x%08x",
                    (unsigned int)device_id, (unsigned int)completion_id,
                    (unsigned int)io_status);
                ohos_rdpdr_print_bridge_fail_active(rdpdr,
                                                    "close-completion");
                return ohos_rdpdr_print_bridge_pump(rdpdr,
                                                    "close-completion");

            default:
                break;
        }
    }
    return 0;
}

static int
ohos_rdpdr_print_process_device_io_create(struct ohos_rdpdr_print *rdpdr,
                                          struct stream *s,
                                          uint32_t device_id,
                                          uint32_t completion_id)
{
    uint32_t desired_access;
    uint64_t allocation_size;
    uint32_t file_attributes;
    uint32_t shared_access;
    uint32_t create_disposition;
    uint32_t create_options;
    uint32_t path_len;
    uint32_t file_id = 0;
    enum NTSTATUS status;

    if (!s_check_rem_and_log(s, 32, "OHOS rdpdr printer create request"))
    {
        rdpdr->errors++;
        return ohos_rdpdr_print_send_create_completion(
                   rdpdr, device_id, completion_id, STATUS_UNSUCCESSFUL, 0);
    }
    in_uint32_le(s, desired_access);
    in_uint64_le(s, allocation_size);
    in_uint32_le(s, file_attributes);
    in_uint32_le(s, shared_access);
    in_uint32_le(s, create_disposition);
    in_uint32_le(s, create_options);
    in_uint32_le(s, path_len);
    if (path_len > (uint32_t)(s->end - s->p))
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: printer create path length invalid len=%u remaining=%ld",
            (unsigned int)path_len, (long)(s->end - s->p));
        rdpdr->errors++;
        return ohos_rdpdr_print_send_create_completion(
                   rdpdr, device_id, completion_id, STATUS_UNSUCCESSFUL, 0);
    }
    in_uint8s(s, path_len);

    status = ohos_rdpdr_print_start_job(rdpdr, device_id, &file_id);
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: printer create device=0x%08x completion=0x%08x desired=0x%08x disposition=0x%08x options=0x%08x path_bytes=%u status=0x%08x file_id=0x%08x",
        (unsigned int)device_id, (unsigned int)completion_id,
        (unsigned int)desired_access, (unsigned int)create_disposition,
        (unsigned int)create_options, (unsigned int)path_len,
        (unsigned int)status, (unsigned int)file_id);
    (void)allocation_size;
    (void)file_attributes;
    (void)shared_access;
    return ohos_rdpdr_print_send_create_completion(rdpdr, device_id,
                                                   completion_id, status,
                                                   file_id);
}

static int
ohos_rdpdr_print_process_device_io_write(struct ohos_rdpdr_print *rdpdr,
                                         struct stream *s,
                                         uint32_t device_id,
                                         uint32_t file_id,
                                         uint32_t completion_id)
{
    uint32_t length;
    uint64_t offset;
    uint32_t bytes_written = 0;
    enum NTSTATUS status;

    if (!s_check_rem_and_log(s, 32, "OHOS rdpdr printer write request"))
    {
        rdpdr->errors++;
        return ohos_rdpdr_print_send_write_completion(
                   rdpdr, device_id, completion_id, STATUS_UNSUCCESSFUL, 0);
    }
    in_uint32_le(s, length);
    in_uint64_le(s, offset);
    in_uint8s(s, 20);
    if (length > (uint32_t)(s->end - s->p))
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpdr: printer write length invalid len=%u remaining=%ld",
            (unsigned int)length, (long)(s->end - s->p));
        rdpdr->errors++;
        return ohos_rdpdr_print_send_write_completion(
                   rdpdr, device_id, completion_id, STATUS_UNSUCCESSFUL, 0);
    }

    status = ohos_rdpdr_print_write_job(rdpdr, device_id, file_id, offset,
                                        s->p, length, &bytes_written);
    in_uint8s(s, length);
    if (status == STATUS_SUCCESS)
    {
        rdpdr->write_requests++;
    }
    else
    {
        rdpdr->errors++;
    }
    LOG(status == STATUS_SUCCESS ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpdr: printer write device=0x%08x file_id=0x%08x completion=0x%08x offset=%llu length=%u written=%u total=%lld status=0x%08x",
        (unsigned int)device_id, (unsigned int)file_id,
        (unsigned int)completion_id, (unsigned long long)offset,
        (unsigned int)length, (unsigned int)bytes_written,
        rdpdr->active_job_bytes, (unsigned int)status);
    return ohos_rdpdr_print_send_write_completion(rdpdr, device_id,
                                                  completion_id, status,
                                                  bytes_written);
}

static int
ohos_rdpdr_print_process_device_io_close(struct ohos_rdpdr_print *rdpdr,
                                         struct stream *s,
                                         uint32_t device_id,
                                         uint32_t file_id,
                                         uint32_t completion_id)
{
    enum NTSTATUS status;

    if (s_check_rem(s, 32))
    {
        in_uint8s(s, 32);
    }

    if (rdpdr->active_job_fd >= 0 &&
            rdpdr->active_device_id == device_id &&
            rdpdr->active_file_id == file_id)
    {
        status = ohos_rdpdr_print_finish_active_job(rdpdr,
                                                    "printer-close", 1);
    }
    else
    {
        status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    LOG(status == STATUS_SUCCESS ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpdr: printer close device=0x%08x file_id=0x%08x completion=0x%08x status=0x%08x",
        (unsigned int)device_id, (unsigned int)file_id,
        (unsigned int)completion_id, (unsigned int)status);
    return ohos_rdpdr_print_send_io_completion(rdpdr, device_id,
                                               completion_id, status,
                                               "printer close completion");
}

static int
ohos_rdpdr_print_process_device_io_request(struct ohos_rdpdr_print *rdpdr,
                                           struct stream *s)
{
    uint32_t device_id;
    uint32_t file_id;
    uint32_t completion_id;
    uint32_t major_function;
    uint32_t minor_function;

    if (!s_check_rem_and_log(s, 20, "OHOS rdpdr io request"))
    {
        rdpdr->errors++;
        return 1;
    }
    in_uint32_le(s, device_id);
    in_uint32_le(s, file_id);
    in_uint32_le(s, completion_id);
    in_uint32_le(s, major_function);
    in_uint32_le(s, minor_function);
    rdpdr->io_requests++;

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpdr: device io request device=0x%08x file_id=0x%08x completion=0x%08x major=0x%08x minor=0x%08x",
        (unsigned int)device_id, (unsigned int)file_id,
        (unsigned int)completion_id, (unsigned int)major_function,
        (unsigned int)minor_function);

    switch (major_function)
    {
        case IRP_MJ_CREATE:
            return ohos_rdpdr_print_process_device_io_create(
                       rdpdr, s, device_id, completion_id);

        case IRP_MJ_WRITE:
            return ohos_rdpdr_print_process_device_io_write(
                       rdpdr, s, device_id, file_id, completion_id);

        case IRP_MJ_CLOSE:
            return ohos_rdpdr_print_process_device_io_close(
                       rdpdr, s, device_id, file_id, completion_id);

        default:
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.rdpdr: unsupported device io request major=0x%08x device=0x%08x",
                (unsigned int)major_function, (unsigned int)device_id);
            return ohos_rdpdr_print_send_io_completion(
                       rdpdr, device_id, completion_id, STATUS_NOT_SUPPORTED,
                       "unsupported printer io completion");
    }
}

static int
ohos_rdpdr_print_process_core_pdu(struct ohos_rdpdr_print *rdpdr,
                                  uint16_t packet_id, struct stream *s)
{
    switch (packet_id)
    {
        case PAKID_CORE_CLIENTID_CONFIRM:
            return ohos_rdpdr_print_process_client_id_confirm(rdpdr, s);

        case PAKID_CORE_CLIENT_NAME:
            return ohos_rdpdr_print_process_client_name(rdpdr, s);

        case PAKID_CORE_CLIENT_CAPABILITY:
            return ohos_rdpdr_print_process_client_capability(rdpdr, s);

        case PAKID_CORE_DEVICELIST_ANNOUNCE:
            return ohos_rdpdr_print_process_devlist_announce(rdpdr, s);

        case PAKID_CORE_DEVICELIST_REMOVE:
            return ohos_rdpdr_print_process_devlist_remove(rdpdr, s);

        case PAKID_CORE_DEVICE_IOREQUEST:
            return ohos_rdpdr_print_process_device_io_request(rdpdr, s);

        case PAKID_CORE_DEVICE_IOCOMPLETION:
            return ohos_rdpdr_print_process_device_io_completion(rdpdr, s);

        default:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.rdpdr: ignored core packet=0x%04x",
                (unsigned int)packet_id);
            return 0;
    }
}

static int
ohos_rdpdr_print_process_pn_pdu(struct ohos_rdpdr_print *rdpdr,
                                 uint16_t packet_id, struct stream *s)
{
    rdpdr->pn_packets++;
    switch (packet_id)
    {
        case PAKID_PRN_CACHE_DATA:
        {
            uint32_t event_id = 0;
            if (s_check_rem(s, 4))
            {
                in_uint32_le(s, event_id);
            }
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.rdpdr: printer cache data event=0x%08x",
                (unsigned int)event_id);
            break;
        }

        case PAKID_PRN_USING_XPS:
        {
            uint32_t printer_id = 0;
            uint32_t flags = 0;
            if (s_check_rem(s, 8))
            {
                in_uint32_le(s, printer_id);
                in_uint32_le(s, flags);
            }
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.rdpdr: printer xps mode printer=0x%08x flags=0x%08x",
                (unsigned int)printer_id, (unsigned int)flags);
            break;
        }

        default:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.rdpdr: ignored printer packet=0x%04x",
                (unsigned int)packet_id);
            break;
    }
    return 0;
}

static int
ohos_rdpdr_print_process_pdu(struct ohos_rdpdr_print *rdpdr,
                             struct stream *s)
{
    uint16_t component;
    uint16_t packet_id;

    if (!s_check_rem_and_log(s, 4, "OHOS rdpdr header"))
    {
        rdpdr->errors++;
        return 1;
    }
    in_uint16_le(s, component);
    in_uint16_le(s, packet_id);

    if (component == RDPDR_CTYP_CORE)
    {
        return ohos_rdpdr_print_process_core_pdu(rdpdr, packet_id, s);
    }
    if (component == RDPDR_CTYP_PRN)
    {
        return ohos_rdpdr_print_process_pn_pdu(rdpdr, packet_id, s);
    }

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.rdpdr: ignored component=0x%04x packet=0x%04x",
        (unsigned int)component, (unsigned int)packet_id);
    return 0;
}

void
ohos_rdpdr_print_init(struct ohos_rdpdr_print *rdpdr, struct mod *mod)
{
    if (rdpdr == 0)
    {
        return;
    }
    g_memset(rdpdr, 0, sizeof(struct ohos_rdpdr_print));
    rdpdr->mod = mod;
    rdpdr->channel_id = -1;
    rdpdr->active_job_fd = -1;
    rdpdr->bridge_listen_sck = -1;
    rdpdr->bridge_client_sck = -1;
    rdpdr->bridge_recv_fd = -1;
    rdpdr->bridge_active_fd = -1;
    rdpdr->bridge_active_state = OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE;
}

void
ohos_rdpdr_print_deinit(struct ohos_rdpdr_print *rdpdr)
{
    if (rdpdr == 0)
    {
        return;
    }
    ohos_rdpdr_print_disconnect(rdpdr);
    ohos_rdpdr_print_channel_reset(rdpdr);
    g_memset(rdpdr, 0, sizeof(struct ohos_rdpdr_print));
    rdpdr->channel_id = -1;
    rdpdr->active_job_fd = -1;
    rdpdr->bridge_listen_sck = -1;
    rdpdr->bridge_client_sck = -1;
    rdpdr->bridge_recv_fd = -1;
    rdpdr->bridge_active_fd = -1;
    rdpdr->bridge_active_state = OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE;
}

int
ohos_rdpdr_print_connect(struct ohos_rdpdr_print *rdpdr)
{
    int rv;

    if (rdpdr == 0 || rdpdr->mod == 0)
    {
        return 1;
    }
    rdpdr->connected = 1;
    rdpdr->channel_ready = 0;
    rdpdr->client_rdp_version = OHOS_RDPDR_SERVER_MINOR_VERSION;
    rdpdr->client_id = 0;
    ohos_rdpdr_print_reset_stats(rdpdr);
    g_random((char *)&rdpdr->server_client_id,
             sizeof(rdpdr->server_client_id));
    if (rdpdr->server_client_id == 0)
    {
        rdpdr->server_client_id = 0x4f485250U;
    }
    g_random((char *)&rdpdr->bridge_next_completion_id,
             sizeof(rdpdr->bridge_next_completion_id));
    if (rdpdr->bridge_next_completion_id == 0)
    {
        rdpdr->bridge_next_completion_id = 1;
    }

    if (rdpdr->mod->server_chansrv_in_use != 0 &&
            rdpdr->mod->server_chansrv_in_use(rdpdr->mod))
    {
        LOG(LOG_LEVEL_INFO, "xrdp.ohos.rdpdr: chansrv owns rdpdr channel");
        return 0;
    }
    if (rdpdr->mod->server_get_channel_id == 0 ||
            rdpdr->mod->server_send_to_channel == 0)
    {
        LOG(LOG_LEVEL_INFO, "xrdp.ohos.rdpdr: channel callbacks unavailable");
        return 0;
    }

    rdpdr->channel_id =
        rdpdr->mod->server_get_channel_id(rdpdr->mod,
                                          RDPDR_SVC_CHANNEL_NAME);
    if (rdpdr->channel_id < 0)
    {
        LOG(LOG_LEVEL_INFO, "xrdp.ohos.rdpdr: rdpdr channel unavailable");
        return 0;
    }

    (void)ohos_rdpdr_print_probe(rdpdr, "session-connect");
    rv = ohos_rdpdr_print_send_server_announce(rdpdr);
    if (rv == 0)
    {
        rdpdr->channel_ready = 1;
        if (ohos_rdpdr_print_bridge_start_listener(rdpdr) != 0)
        {
            rdpdr->errors++;
        }
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpdr: channel ready id=%d local_printer=%s bridge=%s:%s listen=%d",
            rdpdr->channel_id, rdpdr->local_printer_id,
            OHOS_RDPDR_PRINT_BRIDGE_HOST,
            OHOS_RDPDR_PRINT_BRIDGE_PORT,
            rdpdr->bridge_listen_sck > 0);
    }
    else
    {
        rdpdr->errors++;
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.rdpdr: channel init failed id=%d rv=%d",
            rdpdr->channel_id, rv);
    }
    return rv;
}

void
ohos_rdpdr_print_disconnect(struct ohos_rdpdr_print *rdpdr)
{
    int should_log;

    if (rdpdr == 0)
    {
        return;
    }
    should_log = rdpdr->connected || rdpdr->channel_ready;
    ohos_rdpdr_print_bridge_stop_listener(rdpdr);
    if (rdpdr->active_job_fd >= 0)
    {
        (void)ohos_rdpdr_print_finish_active_job(rdpdr,
                                                 "disconnect", 0);
    }
    if (rdpdr->bridge_active_state != OHOS_RDPDR_PRINT_BRIDGE_STATE_IDLE)
    {
        ohos_rdpdr_print_bridge_fail_active(rdpdr, "disconnect");
    }
    ohos_rdpdr_print_bridge_clear_queue(rdpdr);
    if (should_log)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpdr: disconnect devices=%u printers=%u rejected=%u io_requests=%u writes=%u jobs=%u failed_jobs=%u bridge_received=%u bridge_forwarded=%u bridge_failed=%u bridge_dropped=%u bridge_bytes=%u bridge_queued=%u active_bytes=%lld pn_packets=%u errors=%u",
            rdpdr->device_announces, rdpdr->printer_devices,
            rdpdr->rejected_devices,
            rdpdr->io_requests, rdpdr->write_requests,
            rdpdr->print_jobs, rdpdr->failed_print_jobs,
            rdpdr->bridge_jobs_received,
            rdpdr->bridge_jobs_forwarded,
            rdpdr->bridge_jobs_failed,
            rdpdr->bridge_jobs_dropped,
            rdpdr->bridge_bytes_forwarded,
            rdpdr->bridge_queue_count,
            rdpdr->active_job_bytes,
            rdpdr->pn_packets, rdpdr->errors);
    }
    rdpdr->connected = 0;
    rdpdr->channel_ready = 0;
    rdpdr->channel_id = -1;
    rdpdr->client_id = 0;
    rdpdr->client_rdp_version = OHOS_RDPDR_SERVER_MINOR_VERSION;
    ohos_rdpdr_print_channel_reset(rdpdr);
}

int
ohos_rdpdr_print_get_wait_objs(struct ohos_rdpdr_print *rdpdr,
                               tbus *read_objs, int *rcount)
{
    if (rdpdr == 0 || read_objs == 0 || rcount == 0)
    {
        return 0;
    }
    if (rdpdr->bridge_listen_wait_obj != 0)
    {
        read_objs[*rcount] = rdpdr->bridge_listen_wait_obj;
        (*rcount)++;
    }
    if (rdpdr->bridge_client_wait_obj != 0)
    {
        read_objs[*rcount] = rdpdr->bridge_client_wait_obj;
        (*rcount)++;
    }
    return 0;
}

int
ohos_rdpdr_print_check_wait_objs(struct ohos_rdpdr_print *rdpdr)
{
    if (rdpdr == 0)
    {
        return 0;
    }
    if (rdpdr->bridge_listen_wait_obj != 0 &&
            g_is_wait_obj_set(rdpdr->bridge_listen_wait_obj))
    {
        (void)ohos_rdpdr_print_bridge_accept_client(rdpdr);
    }
    if (rdpdr->bridge_client_wait_obj != 0 &&
            g_is_wait_obj_set(rdpdr->bridge_client_wait_obj))
    {
        (void)ohos_rdpdr_print_bridge_read_client(rdpdr);
    }
    (void)ohos_rdpdr_print_bridge_pump(rdpdr, "check-wait-objs");
    return 0;
}

int
ohos_rdpdr_print_process_channel_data(struct ohos_rdpdr_print *rdpdr,
                                      tbus param1, tbus param2,
                                      tbus param3, tbus param4)
{
    int chan_id;
    int flags;
    int size;
    int total_size;
    char *data;
    int first;
    int last;
    int rv = 0;

    if (rdpdr == 0 || !rdpdr->channel_ready)
    {
        return 0;
    }

    chan_id = (int)(param1 & 0xffff);
    flags = (int)((param1 >> 16) & 0xffff);
    size = (int)param2;
    data = (char *)param3;
    total_size = (int)param4;
    if (chan_id != rdpdr->channel_id)
    {
        return 0;
    }
    if (data == 0 || size < 0 || total_size < size ||
            total_size > OHOS_RDPDR_MAX_PDU_BYTES)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.rdpdr: invalid channel chunk size=%d total=%d",
            size, total_size);
        rdpdr->errors++;
        return 1;
    }

    first = (flags & XR_CHANNEL_FLAG_FIRST) != 0;
    last = (flags & XR_CHANNEL_FLAG_LAST) != 0;
    if (first && rdpdr->dechunker_s != 0)
    {
        ohos_rdpdr_print_channel_reset(rdpdr);
    }
    if (first && last)
    {
        struct stream packet_s = { 0 };
        packet_s.data = data;
        packet_s.p = data;
        packet_s.end = data + size;
        packet_s.size = size;
        return ohos_rdpdr_print_process_pdu(rdpdr, &packet_s);
    }
    if (first)
    {
        make_stream(rdpdr->dechunker_s);
        if (rdpdr->dechunker_s == 0)
        {
            return 1;
        }
        init_stream(rdpdr->dechunker_s, total_size);
        if (rdpdr->dechunker_s->data == 0)
        {
            ohos_rdpdr_print_channel_reset(rdpdr);
            return 1;
        }
        out_uint8a(rdpdr->dechunker_s, data, size);
        return 0;
    }
    if (rdpdr->dechunker_s == 0)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.rdpdr: channel chunk without first chunk");
        rdpdr->errors++;
        return 1;
    }
    if (!s_check_rem_out_and_log(rdpdr->dechunker_s, size,
                                 "OHOS rdpdr dechunk"))
    {
        ohos_rdpdr_print_channel_reset(rdpdr);
        return 1;
    }
    out_uint8a(rdpdr->dechunker_s, data, size);
    if (last)
    {
        s_mark_end(rdpdr->dechunker_s);
        rdpdr->dechunker_s->p = rdpdr->dechunker_s->data;
        rv = ohos_rdpdr_print_process_pdu(rdpdr, rdpdr->dechunker_s);
        ohos_rdpdr_print_channel_reset(rdpdr);
    }
    return rv;
}
