#include "sensors_http.h"

#include <stdio.h>
#include "sensors.h"
#include "util.h"

// The JSON names of the readings.
static const char *const NAMES[SENSOR_COUNT] = {
    [SENSOR_TEMP] = "temp",
    [SENSOR_HUM] = "hum",
    [SENSOR_CO2] = "co2",
};

// One JSON member: separator, name, valid flag, value, age.
#define ENTRY_FORMAT "%s\"%s\":{\"valid\":%s,\"value\":%.1f,\"age_ms\":%u}"

static esp_err_t sensors_get(httpd_req_t *req) {
  char buf[320];  // three entries of at most ~80 characters
  int n = snprintf(buf, sizeof(buf), "{");
  for (int i = 0; i < SENSOR_COUNT; i++) {
    sensor_reading_t r;
    sensor_get((sensor_id_t) i, &r);
    const char *valid = r.valid ? "true" : "false";
    int w = snprintf(buf + n, sizeof(buf) - n, ENTRY_FORMAT, i ? "," : "", NAMES[i], valid, r.value,
                     (unsigned) r.age_ms);
    if (w < 0 || w >= (int) sizeof(buf) - n) {
      break;
    }
    n += w;
  }
  if (n < (int) sizeof(buf) - 1) {
    buf[n++] = '}';
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, buf, n);
  return ESP_OK;
}

esp_err_t sensors_http_register(httpd_handle_t srv) {
  const httpd_uri_t uri = {.uri = "/sensors", .method = HTTP_GET, .handler = sensors_get};
  return http_register(srv, &uri);
}
