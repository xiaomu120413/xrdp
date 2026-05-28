#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_input.h"

#include "log.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <multimodalinput/oh_input_manager.h>

#define OHOS_INPUT_AUTH_RETRY_MS 5000ULL

static atomic_int g_authorized_status = UNAUTHORIZED;
static atomic_int g_authorization_requested = 0;
static uint64_t g_last_authorization_request_ms = 0;
static uint32_t g_authorization_log_count = 0;
static int64_t g_last_mouse_action_time_ms = 0;

uint64_t
ohos_input_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000ULL) +
           ((uint64_t)ts.tv_nsec / 1000000ULL);
}

int64_t
ohos_input_next_mouse_action_time(void)
{
    int64_t now = (int64_t)ohos_input_now_ms();
    int64_t next = now > g_last_mouse_action_time_ms ?
        now : g_last_mouse_action_time_ms + 1;
    g_last_mouse_action_time_ms = next;
    return next;
}

void
ohos_input_mark_unauthorized(void)
{
    atomic_store(&g_authorized_status, UNAUTHORIZED);
    atomic_store(&g_authorization_requested, 0);
    LOG(LOG_LEVEL_WARNING,
        "xrdp.ohos.input: stage=auth_mark result=unauthorized reason=inject_permission_denied");
}

static void
ohos_input_authorize_callback(Input_InjectionStatus status)
{
    atomic_store(&g_authorized_status, status);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.input: injection authorization callback status=%d",
        (int)status);
}

static int
ohos_input_should_log_auth(uint32_t count)
{
    return count <= 3U || (count % 500U) == 0U;
}

static int
ohos_input_auth_reason_is_hot_path(const char *reason)
{
    return reason != 0 &&
           (strcmp(reason, "mouse event") == 0 ||
            strcmp(reason, "key event") == 0);
}

int
ohos_input_refresh_authorized_status(void)
{
    Input_InjectionStatus status = UNAUTHORIZED;
    Input_Result query_rc;

    if (atomic_load(&g_authorized_status) == AUTHORIZED)
    {
        return 1;
    }

    query_rc = OH_Input_QueryAuthorizedStatus(&status);
    if (query_rc == INPUT_SUCCESS)
    {
        atomic_store(&g_authorized_status, status);
        return status == AUTHORIZED;
    }
    return 0;
}

int
ohos_input_ensure_authorized(const char *reason)
{
    Input_InjectionStatus status = UNAUTHORIZED;
    Input_Result query_rc;
    uint64_t now_ms;
    int should_request;
    int is_hot_path = ohos_input_auth_reason_is_hot_path(reason);
    Input_Result request_rc;

    if (atomic_load(&g_authorized_status) == AUTHORIZED)
    {
        if (!is_hot_path)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=auth_check result=authorized source=atomic reason=%s status=%d requested=%d last_request_ms=%llu",
                reason == 0 ? "" : reason, AUTHORIZED,
                atomic_load(&g_authorization_requested),
                (unsigned long long)g_last_authorization_request_ms);
        }
        return 1;
    }

    query_rc = OH_Input_QueryAuthorizedStatus(&status);
    if (query_rc == INPUT_SUCCESS)
    {
        atomic_store(&g_authorized_status, status);
        if (status == AUTHORIZED)
        {
            if (!is_hot_path)
            {
                LOG(LOG_LEVEL_DEBUG,
                    "xrdp.ohos.input: stage=auth_check result=authorized source=query reason=%s query_rc=%d status=%d requested=%d last_request_ms=%llu",
                    reason == 0 ? "" : reason, (int)query_rc, (int)status,
                    atomic_load(&g_authorization_requested),
                    (unsigned long long)g_last_authorization_request_ms);
            }
            return 1;
        }
    }

    now_ms = ohos_input_now_ms();
    should_request = !atomic_load(&g_authorization_requested) ||
        now_ms < g_last_authorization_request_ms ||
        now_ms - g_last_authorization_request_ms > OHOS_INPUT_AUTH_RETRY_MS;
    if (!should_request)
    {
        uint32_t count = ++g_authorization_log_count;
        if (ohos_input_should_log_auth(count))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=auth_check result=pending reason=%s query_rc=%d status=%d requested=%d last_request_ms=%llu now_ms=%llu",
                reason == 0 ? "" : reason, (int)query_rc, (int)status,
                atomic_load(&g_authorization_requested),
                (unsigned long long)g_last_authorization_request_ms,
                (unsigned long long)now_ms);
        }
        return 0;
    }

    atomic_store(&g_authorization_requested, 1);
    g_last_authorization_request_ms = now_ms;
    request_rc = OH_Input_RequestInjection(ohos_input_authorize_callback);
    if (request_rc == INPUT_INJECTION_AUTHORIZED)
    {
        atomic_store(&g_authorized_status, AUTHORIZED);
        if (!is_hot_path)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=auth_request result=authorized reason=%s request_rc=%d query_rc=%d status=%d requested=%d last_request_ms=%llu",
                reason == 0 ? "" : reason, (int)request_rc,
                (int)query_rc, (int)status,
                atomic_load(&g_authorization_requested),
                (unsigned long long)g_last_authorization_request_ms);
        }
        return 1;
    }

    {
        uint32_t count = ++g_authorization_log_count;
        if (ohos_input_should_log_auth(count))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=auth_request result=pending reason=%s request_rc=%d query_rc=%d status=%d requested=%d last_request_ms=%llu now_ms=%llu",
                reason == 0 ? "" : reason, (int)request_rc,
                (int)query_rc, (int)status,
                atomic_load(&g_authorization_requested),
                (unsigned long long)g_last_authorization_request_ms,
                (unsigned long long)now_ms);
        }
    }
    return 0;
}

void
ohos_input_prime_authorization(const char *reason)
{
    int before_status = atomic_load(&g_authorized_status);
    int before_requested = atomic_load(&g_authorization_requested);
    int ready = ohos_input_ensure_authorized(reason);

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.input: stage=auth_prime reason=%s ready=%d status=%d->%d requested=%d->%d last_request_ms=%llu",
        reason == 0 ? "" : reason, ready,
        before_status, atomic_load(&g_authorized_status),
        before_requested, atomic_load(&g_authorization_requested),
        (unsigned long long)g_last_authorization_request_ms);
}
