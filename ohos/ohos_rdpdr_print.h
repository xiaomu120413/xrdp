#ifndef XRDP_OHOS_RDPDR_PRINT_H
#define XRDP_OHOS_RDPDR_PRINT_H

#include "arch.h"

#include <stdint.h>

struct mod;
struct stream;
struct ohos_rdpdr_print_bridge_job;

#define OHOS_RDPDR_MAX_PRINTER_DEVICES 8
#define OHOS_RDPDR_MAX_PRINT_PATH 4096

struct ohos_rdpdr_print
{
    struct mod *mod;
    int channel_id;
    int connected;
    int channel_ready;
    uint32_t server_client_id;
    uint32_t client_id;
    uint16_t client_rdp_version;
    uint32_t client_extended_pdu;
    struct stream *dechunker_s;
    char local_printer_id[256];
    uint32_t printer_device_ids[OHOS_RDPDR_MAX_PRINTER_DEVICES];
    unsigned int printer_device_count;
    uint32_t active_device_id;
    uint32_t active_file_id;
    int active_job_fd;
    char active_job_file_name[128];
    char active_job_path[OHOS_RDPDR_MAX_PRINT_PATH];
    long long active_job_bytes;
    int bridge_listen_sck;
    tintptr bridge_listen_wait_obj;
    int bridge_client_sck;
    tintptr bridge_client_wait_obj;
    int bridge_recv_fd;
    char bridge_recv_file_name[128];
    char bridge_recv_path[OHOS_RDPDR_MAX_PRINT_PATH];
    long long bridge_recv_bytes;
    struct ohos_rdpdr_print_bridge_job *bridge_queue_head;
    struct ohos_rdpdr_print_bridge_job *bridge_queue_tail;
    unsigned int bridge_queue_count;
    struct ohos_rdpdr_print_bridge_job *bridge_active_job;
    int bridge_active_fd;
    uint32_t bridge_active_device_id;
    uint32_t bridge_active_file_id;
    uint32_t bridge_active_completion_id;
    uint32_t bridge_active_last_write_bytes;
    uint32_t bridge_next_completion_id;
    uint64_t bridge_active_offset;
    int bridge_active_state;
    unsigned int device_announces;
    unsigned int printer_devices;
    unsigned int rejected_devices;
    unsigned int io_requests;
    unsigned int write_requests;
    unsigned int print_jobs;
    unsigned int failed_print_jobs;
    unsigned int bridge_jobs_received;
    unsigned int bridge_jobs_forwarded;
    unsigned int bridge_jobs_failed;
    unsigned int bridge_jobs_dropped;
    unsigned int bridge_bytes_forwarded;
    unsigned int pn_packets;
    unsigned int errors;
};

void
ohos_rdpdr_print_init(struct ohos_rdpdr_print *rdpdr, struct mod *mod);

void
ohos_rdpdr_print_deinit(struct ohos_rdpdr_print *rdpdr);

int
ohos_rdpdr_print_connect(struct ohos_rdpdr_print *rdpdr);

void
ohos_rdpdr_print_disconnect(struct ohos_rdpdr_print *rdpdr);

int
ohos_rdpdr_print_process_channel_data(struct ohos_rdpdr_print *rdpdr,
                                      tbus param1, tbus param2,
                                      tbus param3, tbus param4);

int
ohos_rdpdr_print_get_wait_objs(struct ohos_rdpdr_print *rdpdr,
                               tbus *read_objs, int *rcount);

int
ohos_rdpdr_print_check_wait_objs(struct ohos_rdpdr_print *rdpdr);

#endif
