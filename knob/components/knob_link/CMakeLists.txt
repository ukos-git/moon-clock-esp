# Wi-Fi STA join only (K2). The companion TX sender + mDNS discovery were
# retired with the companion approach 2026-11-08 — the knob is a
# standalone moon display; this join carries K7d's SNTP.
idf_component_register(SRCS "knob_link_wifi.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event spiffs nvs_flash lwip)
