# The network: station, setup AP, mDNS, SNTP, the one network worker, and the two
# signed release channels (the web manifest, the firmware manifest and its updater).
# Results reach the screens through the callback they pass.
idf_component_register(SRCS "net.c" "core_net.c" "signed_envelope.c" "web_manifest.c"
                            "fw_manifest.c" "fw_update.c" "dns_answer.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "web_manifest_pub.pem"
                       REQUIRES plugin_sdk
                       PRIV_REQUIRES system secrets esp_wifi esp_netif esp_event lwip nvs_flash
                                     esp_http_client esp_https_ota app_update mbedtls json
                                     esp_timer mdns)
