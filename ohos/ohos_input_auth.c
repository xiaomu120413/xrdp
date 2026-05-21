#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_input.h"

#include "log.h"

#include <stdatomic.h>
#include <stdbool.h>
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
}

static void
ohos_input_authorize_callback(Input_InjectionStatus status)
{
    atomic_store(&g_authorized_status, status);
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.input: injection authorization callback status=%d",
        (int)status);
}

static int
ohos_input_should_log_auth(uint32_t count)
{
    return count <= 3U || (count % 500U) == 0U;
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
    Input_Result request_rc;

    if (atomic_load(&g_authorized_status) == AUTHORIZED)
    {
        return 1;
    }

    query_rc = OH_Input_QueryAuthorizedStatus(&status);
    if (query_rc == INPUT_SUCCESS)
    {
        atomic_store(&g_authorized_status, status);
        if (status == AUTHORIZED)
        {
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
                "xrdp.ohos.input: injection authorization pending reason=%s query_rc=%d status=%d",
                reason == 0 ? "" : reason, (int)query_rc, (int)status);
        }
        return 0;
    }

    atomic_store(&g_authorization_requested, 1);
    g_last_authorization_request_ms = now_ms;
    request_rc = OH_Input_RequestInjection(ohos_input_authorize_callback);
    if (request_rc == INPUT_INJECTION_AUTHORIZED)
    {
        atomic_store(&g_authorized_status, AUTHORIZED);
        return 1;
    }

    {
        uint32_t count = ++g_authorization_log_count;
        if (ohos_input_should_log_auth(count))
        {
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.input: injection authorization requested reason=%s request_rc=%d query_rc=%d status=%d",
                reason == 0 ? "" : reason, (int)request_rc,
                (int)query_rc, (int)status);
        }
    }
    return 0;
}

void
ohos_input_prime_authorization(const char *reason)
{
    (void)ohos_input_ensure_authorized(reason);
}
