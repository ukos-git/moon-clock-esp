# The companion wire contract is SHARED verbatim with the clock build
# (../../../components/companion/include) — the same header both sides
# compile against is the whole point of companion_proto.h.
idf_component_register(SRCS "knob_link_wifi.c" "knob_link_tx.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event spiffs nvs_flash lwip mdns)
target_include_directories(${COMPONENT_LIB} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/companion/include)
