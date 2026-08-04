#ifndef XRDP_OHOS_PRINT_H
#define XRDP_OHOS_PRINT_H

#include "arch.h"
#include "xrdp_ohos.h"

int
ohos_print_ensure_initialized(void);

void
ohos_print_release(void);

int
ohos_print_probe(char *printer_id, int printer_id_bytes);

int
ohos_print_make_spool_file(char *file_name, int file_name_bytes,
                           char *resolved_file, int resolved_file_bytes);

int
ohos_print_spool_file(const char *file_name, const char *printer_id);

#endif
