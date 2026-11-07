/**
 * @file knob_link_tx.c
 * @brief Companion frame sender + clock discovery (K4).
 *
 * Sends the fixed 10-byte frames (companion_proto.h, shared with the
 * clock) to the clock's listener. Discovery = mDNS PTR on
 * _moonclock._udp.local -> A on the hostname, retried by a small
 * background task so the LVGL task never blocks on the 1.5 s query
 * timeout. Until discovered, frames go to the subnet broadcast (the
 * clock binds INADDR_ANY), which also makes the system work with mDNS
 * disabled on the AP.
 */
#include "knob_link.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#include "companion_proto.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

static const char *TAG = "knob_tx";

static void discover_task(void *arg);

static uint16_t companion_port(void) {
#ifndef CONFIG_COMPANION_PORT
    return 50188; /* the clock's companion default */
#else
    return (uint16_t)CONFIG_COMPANION_PORT;
#endif
}

static uint8_t companion_token(void) {
#ifndef CONFIG_COMPANION_TOKEN
    return 0;
#else
    return (uint8_t)CONFIG_COMPANION_TOKEN;
#endif
}

static int s_sock = -1;
static uint16_t s_seq;
static struct sockaddr_in s_clock;
static volatile bool s_ready;
static char s_ip_str[16] = "broadcast";

esp_err_t knob_link_start(void) {
    static bool s_started;
    if (s_started) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mdns init");
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket: errno %d", errno);
        return ESP_FAIL;
    }
    int opt = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    s_started = true;
    xTaskCreate(discover_task, "knob_disc", 6144, NULL, 4, NULL);
    return ESP_OK;
}

/* One PTR + A round. On success caches the clock address (single clock
 * on the LAN: first result wins). */
static esp_err_t discover_once(void) {
    mdns_result_t *r = NULL;
    esp_err_t e = mdns_query_ptr("_moonclock", "_udp", 1500, 4, &r);
    if (e != ESP_OK || !r) {
        if (r) {
            mdns_query_results_free(r);
        }
        return e == ESP_OK ? ESP_ERR_NOT_FOUND : e;
    }
    const char *hostname = NULL;
    for (mdns_result_t *it = r; it != NULL; it = it->next) {
        if (it->hostname && it->hostname[0]) {
            hostname = it->hostname;
            break;
        }
    }
    esp_ip4_addr_t a;
    memset(&a, 0, sizeof(a));
    if (hostname) {
        e = mdns_query_a(hostname, 1500, &a);
    } else {
        e = ESP_ERR_NOT_FOUND;
    }
    mdns_query_results_free(r);
    if (e != ESP_OK || a.addr == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    memset(&s_clock, 0, sizeof(s_clock));
    s_clock.sin_family = AF_INET;
    s_clock.sin_port = htons(companion_port());
    s_clock.sin_addr.s_addr = a.addr;
    snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR((ip4_addr_t *)&a));
    s_ready = true;
    ESP_LOGI(TAG, "clock found at %s (:%u)", s_ip_str, (unsigned)companion_port());
    return ESP_OK;
}

static void discover_task(void *arg) {
    (void)arg;
    /* Give the association a head start, then poll. Cheap: one PTR+A per
     * two seconds until found, then the task retires. */
    vTaskDelay(pdMS_TO_TICKS(3000));
    while (!s_ready) {
        if (knob_link_wifi_is_connected() && discover_once() == ESP_OK) {
            knob_link_hello();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    vTaskDelete(NULL);
}

bool knob_link_clock_ready(void) { return s_ready; }

const char *knob_link_clock_ip_str(void) { return s_ip_str; }

esp_err_t knob_link_send(uint8_t type, uint8_t field, int8_t delta) {
    if (s_sock < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    companion_frame_t f;
    memset(&f, 0, sizeof(f));
    f.magic = COMPANION_MAGIC;
    f.version = COMPANION_VERSION;
    f.type = type;
    f.seq = s_seq++;
    f.token = companion_token();
    f.field = field;
    f.delta = delta;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    if (s_ready) {
        dst = s_clock;
    } else {
        dst.sin_family = AF_INET;
        dst.sin_port = htons(companion_port());
        dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }
    const ssize_t n = sendto(s_sock, &f, sizeof(f), 0, (struct sockaddr *)&dst, sizeof(dst));
    if (n != (ssize_t)sizeof(f)) {
        ESP_LOGW(TAG, "sendto type %u: errno %d", type, errno);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "tx type %u field %u delta %d seq %u", type, field, delta, (unsigned)(f.seq - 1));
    return ESP_OK;
}

esp_err_t knob_link_hello(void) { return knob_link_send(COMPANION_FRAME_HELLO, COMPANION_FIELD_NONE, 0); }
