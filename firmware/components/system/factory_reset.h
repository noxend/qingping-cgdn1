// Factory reset: forgets the home Wi-Fi and every setting, keeps the firmware and
// the confirmed image, and reboots; the device then comes up on its setup AP.
#pragma once

// The NVS namespace on the default partition that holds the home network's
// credentials (net.c writes it, the reset erases it).
#define FACTORY_WIFI_NS "wifi"

// Erases nvs_cfg (plugin settings and the core's records), calls forget_wifi (the
// network component's own erase: the Wi-Fi driver keeps a second copy of the
// credentials that only it can clear), erases FACTORY_WIFI_NS, then restarts.
// Does not return. Safe from any task.
void factory_reset(void (*forget_wifi)(void)) __attribute__((noreturn));
