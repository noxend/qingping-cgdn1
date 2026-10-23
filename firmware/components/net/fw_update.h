// Installs the offered firmware (fw_manifest.h) from its URL, as a job on the
// network worker: esp_https_ota with the certificate bundle, then, before the boot
// slot changes, the image is held to the size and sha256 the signed manifest names
// and to its project and version, and esp_ota_end checks the image signature as it
// does for an upload. Then a reboot, and the rollback gate decides whether the new
// image stays.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
  FW_UPD_IDLE,
  FW_UPD_QUEUED,
  FW_UPD_DOWNLOADING,
  FW_UPD_VERIFYING,
  FW_UPD_REBOOTING,
  FW_UPD_FAILED,
} fw_update_state_t;

typedef struct {
  fw_update_state_t state;
  uint32_t done;  // bytes written so far
  uint32_t total;
  char version[24];
  char err[48];  // why the last attempt failed, for FW_UPD_FAILED
} fw_update_status_t;

// Loads the auto update option. Call after settings_init.
void fw_update_init(void);

// Queues the install of the offered update. ESP_ERR_NOT_FOUND when nothing is
// offered, ESP_ERR_INVALID_STATE while one is already queued or running or a file
// upload holds the slot, ESP_FAIL when the network worker cannot take it.
esp_err_t fw_update_start(void);

// POST /ota writes the same spare slot: it holds the slot for the length of an
// upload, and is refused (false) while an install from the URL holds it.
bool fw_update_hold_slot(void);
void fw_update_release_slot(void);

void fw_update_status(fw_update_status_t *out);
// "idle", "queued", "downloading", "verifying", "rebooting", "failed".
const char *fw_update_state_name(fw_update_state_t state);

// Off by default. When on, an offered update is installed once a day at 04:00 local
// time, unless that same version already failed its rollback gate here.
bool fw_update_auto(void);
esp_err_t fw_update_set_auto(bool on);

// UI loop, once a second: the 04:00 auto update.
void fw_update_tick(void);

// The version of a newer image that was installed here and rolled back (it did not
// pass the rollback gate), for the owner to see; false when there is none.
bool fw_update_rolled_back(char *out, size_t cap);
