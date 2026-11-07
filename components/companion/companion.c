/**
 * @file companion.c
 * @brief mDNS + UDP listener for the companion knob (task #16).
 *
 * The task does transport work only: recvfrom -> companion_frame_ok ->
 * queue. Screen gating and every LVGL touch happen in
 * screens_companion_handle(), reached from the UI tick (the drain), never
 * from here. Kconfig values come through #ifndef-seeded getters (Kconfig
 * `n` leaves the macro UNDEFINED — the project invariant).
 */
#include "companion.h"

#include <errno.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mdns.h"

static const char *TAG = "companion";

#define COMPANION_QUEUE_DEPTH 16

static QueueHandle_t s_queue;
static uint16_t s_last_seq;
static bool s_have_last;
static TaskHandle_t s_task;

static uint8_t companion_expected_token(void) {
#ifndef CONFIG_COMPANION_TOKEN
    return 0; /* 0 = accept any (LAN-trusted default) */
#else
    return (uint8_t)CONFIG_COMPANION_TOKEN;
#endif
}

static uint16_t companion_port(void) {
#ifndef CONFIG_COMPANION_PORT
    return 50188;
#else
    return (uint16_t)CONFIG_COMPANION_PORT;
#endif
}

bool companion_wifi_keep_connected(void) {
    /* #ifndef-seeded: COMPANION_KEEP_WIFI=n (the Kconfig default, and the
     * right call for the battery S3) leaves the macro undefined. */
#ifdef CONFIG_COMPANION_KEEP_WIFI
    return true;
#else
    return false;
#endif
}

#ifdef CONFIG_COMPANION_ENABLE

static void companion_task(void *arg) {
    (void)arg;
    const int sock = (int)(intptr_t)arg;
    char rx[COMPANION_FRAME_LEN + 16]; /* slack: reject long junk cheaply */
    while (1) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        const int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&from, &from_len);
        if (n <= 0) {
            continue; /* SO_RCVTIMEO expiry — loop, feed the WDT */
        }
        if (!companion_frame_ok(rx, (size_t)n, companion_expected_token(), s_have_last, s_last_seq)) {
            ESP_LOGW(TAG, "dropped %d B from " IPSTR " (bad magic/version/token/dup)", n,
                     IP2STR((const ip4_addr_t *)&from.sin_addr));
            continue;
        }
        companion_frame_t f;
        memcpy(&f, rx, sizeof(f));
        s_last_seq = f.seq;
        s_have_last = true;
        if (f.type == COMPANION_FRAME_HELLO) {
            /* sockaddr_in::sin_addr is the POSIX struct in_addr (s_addr);
             * IP2STR wants the lwIP ip4_addr_t (both are one network-order
             * u32, so the cast is the IDF-example idiom). */
            ESP_LOGI(TAG, "HELLO from " IPSTR " (seq %u)", IP2STR((const ip4_addr_t *)&from.sin_addr), f.seq);
            continue;
        }
        if (xQueueSend(s_queue, &f, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue full, dropping type %u seq %u", f.type, f.seq);
        }
    }
}

esp_err_t companion_start(void) {
    if (s_task) {
        return ESP_OK;
    }
    const uint16_t port = companion_port();

    /* mDNS: dial _moonclock._udp.local to find us on the LAN. The
     * registry mdns rejects service_add while the server hostname is
     * unset (mdns_service_add_for_host_base's !s_server->hostname arm
     * returns ESP_ERR_INVALID_ARG — found on the P4 bench), so set it. */
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mdns init");
    ESP_RETURN_ON_ERROR(mdns_hostname_set("moonclock"), TAG, "mdns hostname");
    esp_err_t merr = mdns_service_add(NULL, "_moonclock", "_udp", port, NULL, 0);
    if (merr != ESP_OK) {
        ESP_LOGE(TAG, "mdns_service_add: %s", esp_err_to_name(merr));
        return merr;
    }

    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket: errno %d", errno);
        return ESP_FAIL;
    }
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct timeval tv = {.tv_sec = 0, .tv_usec = 250 * 1000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind :%u: errno %d", port, errno);
        close(sock);
        return ESP_FAIL;
    }

    s_queue = xQueueCreate(COMPANION_QUEUE_DEPTH, sizeof(companion_frame_t));
    if (!s_queue) {
        close(sock);
        return ESP_ERR_NO_MEM;
    }
    /* Core 0 (the house rule for network listeners): the LVGL/UI work runs
     * on core 1, the display pipeline never waits on this task. */
    xTaskCreatePinnedToCore(companion_task, "companion", 4096, (void *)(intptr_t)sock, 5, &s_task, 0);
    if (!s_task) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "listening on :%u, mDNS _moonclock._udp.local (token %s)", port,
             companion_expected_token() ? "required" : "off");
    return ESP_OK;
}

#else /* !CONFIG_COMPANION_ENABLE */

esp_err_t companion_start(void) {
    ESP_LOGI(TAG, "companion input disabled (CONFIG_COMPANION_ENABLE=n)");
    return ESP_OK;
}

#endif /* CONFIG_COMPANION_ENABLE */

bool companion_poll(companion_frame_t *out) {
    if (!s_queue || !out) {
        return false;
    }
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}
