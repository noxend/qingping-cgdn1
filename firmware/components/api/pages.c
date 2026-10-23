#include "handlers.h"

#include <stdio.h>
#include "esp_ota_ops.h"
#include "creds.h"
#include "net.h"
#include "util.h"
#include "version.h"
#include "web_manifest.h"

static const char *PAGE_HEAD =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>CGDN1</title>"
    "<style>body{font-family:system-ui;margin:2rem;max-width:30rem}"
    "input{width:100%;padding:.5rem;margin:.3rem 0;box-sizing:border-box}"
    "button{padding:.6rem 1rem}</style>";

static const char *CSP = "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'";

// Minimal unauthenticated status + a no-script fallback form. No scan here, so
// it discloses nothing and cannot be used to DoS the radio. (served at / )
// the network dropdown is the React app (served at /) talking to /scan.
// Embedded React single-page app (gzipped), served at "/".
extern const uint8_t app_html_gz_start[] asm("_binary_app_html_gz_start");
extern const uint8_t app_html_gz_end[] asm("_binary_app_html_gz_end");

void api_mark_ap_hit(httpd_req_t *req) {
  net_note_request(httpd_req_to_sockfd(req));
}

// GET /app: the interface built into the firmware, always. GET / falls back to it
// when there is no verified manifest, and the shell goes here when the hosted
// files do not load (the Air or the internet is down).
esp_err_t api_embedded_app_get(httpd_req_t *req) {
  api_mark_ap_hit(req);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Content-Security-Policy",
                     "default-src 'self'; script-src 'self' 'unsafe-inline' 'unsafe-eval'; "
                     "style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                     "font-src 'self' data:; connect-src 'self'");
  httpd_resp_send(req, (const char *) app_html_gz_start, app_html_gz_end - app_html_gz_start);
  return ESP_OK;
}

extern const char shell_js_start[] asm("_binary_shell_js_start");
extern const char shell_js_end[] asm("_binary_shell_js_end");

// GET /shell.js: loads the hosted build. From the device, so the page's CSP can
// allow scripts from 'self' and the one origin and nothing inline.
esp_err_t api_shell_js_get(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/javascript");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  // EMBED_TXTFILES adds a NUL the browser must not see.
  httpd_resp_send(req, shell_js_start, shell_js_end - shell_js_start - 1);
  return ESP_OK;
}

// GET /: the shell page for the hosted interface when a verified manifest is
// known, else the built-in interface. The JSON block is data, not script; every
// value in it passed web_manifest's shape checks, so it holds no quote or tag.
esp_err_t api_app_get(httpd_req_t *req) {
  web_manifest_t m;
  if (!web_manifest_get(&m)) {
    return api_embedded_app_get(req);
  }
  api_mark_ap_hit(req);
  char page[1400];
  int n = 0;
  bufappend(page, &n, sizeof(page),
            "<!doctype html><html lang=\"uk\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<meta name=\"color-scheme\" content=\"light dark\">"
            "<meta name=\"referrer\" content=\"no-referrer\"><title>CGDN1</title>"
            "<script type=\"application/json\" id=\"cgdn1-web\">"
            "{\"version\":\"%s\",\"base\":\"%s\",\"js\":\"%s\",\"css\":[",
            m.version, m.base, m.js);
  for (int i = 0; i < m.css_n; i++) {
    bufappend(page, &n, sizeof(page), "%s\"%s\"", i ? "," : "", m.css[i]);
  }
  bufappend(page, &n, sizeof(page), "],\"integrity\":{\"%s\":\"%s\"", m.js, m.js_sri);
  for (int i = 0; i < m.css_n; i++) {
    bufappend(page, &n, sizeof(page), ",\"%s\":\"%s\"", m.css[i], m.css_sri[i]);
  }
  bufappend(page, &n, sizeof(page),
            "}}</script><script src=\"/shell.js\"></script></head>"
            "<body><div id=\"root\"></div></body></html>");
  if (n >= (int) sizeof(page) - 1) {
    return api_embedded_app_get(req);  // cannot happen with checked lengths; never send half a page
  }
  // The CSP names the exact files of this build, not the whole origin, so a file the
  // server adds later is refused even without the hashes.
  char csp[700];
  int c = 0;
  bufappend(csp, &c, sizeof(csp), "default-src 'self'; script-src 'self' %s%s; ", m.base, m.js);
  bufappend(csp, &c, sizeof(csp), "style-src 'self' 'unsafe-inline'");
  for (int i = 0; i < m.css_n; i++) {
    bufappend(csp, &c, sizeof(csp), " %s%s", m.base, m.css[i]);
  }
  bufappend(csp, &c, sizeof(csp),
            "; img-src 'self' data: blob:; font-src 'self' data:; connect-src 'self'; "
            "worker-src 'none'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; "
            "form-action 'self'");
  if (c >= (int) sizeof(csp) - 1) {
    return api_embedded_app_get(req);
  }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Content-Security-Policy", csp);
  httpd_resp_send(req, page, n);
  return ESP_OK;
}

esp_err_t api_fallback_get(httpd_req_t *req) {
  api_mark_ap_hit(req);
  const esp_partition_t *run = esp_ota_get_running_partition();
  bool joined = net_online();
  bool first = !creds_config_is_set();
  char buf[960];
  int n = snprintf(buf, sizeof(buf),
                   "%s<h2>CGDN1 air monitor</h2><p>firmware %s<br>slot %s<br>WiFi: %s</p>"
                   "<h3>Set WiFi</h3><form method=POST action=/save>"
                   "<label>%s</label>"
                   "<input name=token type=%s maxlength=63 autocomplete=off>"
                   "<label>Network name</label><input name=ssid maxlength=32 autocomplete=off>"
                   "<label>WiFi password</label>"
                   "<input name=pass type=password maxlength=64 autocomplete=off>"
                   "<button>Save &amp; connect</button></form>",
                   PAGE_HEAD, FW_VERSION, run->label, joined ? "connected" : "not set",
                   first ? "New device password (8-63, no spaces)" : "Device password",
                   first ? "text" : "password");
  if (n < 0 || n >= (int) sizeof(buf)) {
    n = (int) sizeof(buf) - 1;  // a truncated page still renders; never send past buf
  }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Security-Policy", CSP);
  httpd_resp_send(req, buf, n);
  return ESP_OK;
}
