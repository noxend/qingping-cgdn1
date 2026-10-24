# CGDN1 custom firmware (ESP-IDF)

Custom firmware for the Qingping Air Monitor Lite (CGDN1), ESP32-WROVER-B: screen plugins on a
touch-strip carousel, a device menu, a night profile and burn-in protection, a web interface, and
signed OTA updates. HomeKit is not in it (parked). See `../docs/hardware.md` for the
reverse-engineered pinout and `../README.md` for the user's side.

The update path came first and stays the most guarded part: the setup access point, HTTP OTA, the
rollback gate and the GPIO23 power latch were proven before anything else, so the case can be
sealed and every later feature ships over WiFi (see the pre-seal checklist below).

## WiFi setup (no credentials hardcoded)

At boot the device runs an access point `CGDN1-setup-XXXX` (WPA2/WPA3) on `10.47.0.1`. Its
password is 12 characters the device generated on its first start and shows on the menu's Wi-Fi
setup page. Join it, open `http://10.47.0.1/`, pick the home WiFi, enter its password and choose
the config password; they are saved and the device restarts to join that
network. The AP goes off
5 minutes after the station gets an address and comes back whenever the station loses it, so a
changed home network is fixed through the same page without opening the case. While the device is
on the LAN the same form (System page) changes the network too, for a signed-in browser.

## Device credentials (none in the image)

`components/system/creds.c` keeps both in `nvs_cfg`, so a factory reset clears them:

- **Setup AP password**: 12 characters from `abcdefghjkmnpqrstuvwxyz23456789`, made on the first boot
  from entropy taken under `bootloader_random_enable` before touch and Wi-Fi start (without RF the
  RNG is only pseudo-random, per the IDF guide on random numbers).
- **Config password**: none until the user sets it. While none is set, `/scan` and `/save` answer
  only clients of the setup AP whose request carries no `Origin` or the device's own, and the
  `token` sent to `/save` becomes the password (8 to 63 printable ASCII characters, no space). Stored as PBKDF2-HMAC-SHA256 (16 byte salt, 4096
  iterations). A wrong one costs one PBKDF2 and blocks that client's next try for a second
  (429; 16 clients are tracked, the one whose second ends first gives way). Past four PBKDF2
  runs a second, all clients together, a check waits its turn instead of being refused, so
  other clients' wrong tries cannot keep the owner out. PBKDF2 (0.48 s each) runs on a task of its
  own, not the server task (`components/http/pass_queue.c`): a request whose password needs it waits
  in a queue of four, one per address, and the server keeps answering session cookies and
  `/status` meanwhile. A request that finds the queue full, or its address already in it, gets 503
  with `Retry-After: 1`. A refused address keeps its place in line while it asks again within 5 s, and
  a place that comes free goes to the address refused first, so during a flood the owner's sign-in
  waits for its turn, then about half a second for each check ahead of it. The line holds 16
  addresses; past that a newcomer is refused without a place.
  The last good password is kept in RAM so a polling client does not pay for PBKDF2.
  `GET /status` says whether one is set (`"cfg"`); signed in it also gives the last PBKDF2 time
  (`"cfg_ms"`).

## Sign-in

`components/http/auth.c` and `session.c`. Every endpoint registered with `http_register` needs one
of two things, and `http_register_open` is only for `/`, `/app`, `/shell.js`, `/fallback`,
`/status`, `/login`, `/logout`, `/scan` and `/save` (the last two check for themselves: signed in,
the password in the form's `token` from `/fallback`, or the first setup) and the diagnostics
(`DEV_OPEN_DIAGNOSTICS`):

- **A session**: `POST /login {"password"}` answers with a cookie `cgdn1_session` (32 random
  bytes as hex; HttpOnly, SameSite=Strict, Path=/, Max-Age 30 days). The device keeps 8 sessions
  in `nvs_cfg` as the SHA-256 of the token and an expiry, so a reboot or an update signs nobody
  out; a ninth sign-in drops the one that expires first. Each use moves the expiry to 30 days
  ahead in steps of a day (one flash write a day per session in use), and the cookie is sent again.
  Expiry follows the clock SNTP sets; until it is set after a boot, the latest time the device
  saved plus the uptime stands in (never later than the real time), and a session made in a boot
  that never had a clock ends with that boot. `POST /logout` forgets the session and clears the cookie.
- **The password with the request**, for tools: `Authorization: Bearer <password>`. The same
  wrong-password wait applies.

A request with an `Origin` other than `http://<its Host>` is refused (403) before either is
looked at. `GET /status` without a session gives only `api`, `fw`, `cfg`, `"signed_in":false`
and, for the page after the first setup, `ip`, `host` and `wifi_fail`.
- **Online with no config password** (an older build updated straight to this one, or nvs_cfg lost):
  nothing on the LAN can change Wi-Fi or install, and the AP stays off while the station holds an
  address. The way back is the device menu's Factory reset, then the AP setup.

A device that ran a build from before 1.0.0-rc2 has neither. The migration build, made once with
the old `CONFIG_PASS` still in `secrets.h` (at most 63 characters, no space), stores that
password's hash when home Wi-Fi is saved and reports `"cfg_migrated":"ok"` in `GET /status`
("skipped" without saved Wi-Fi); it also takes `CONFIG_PASS` itself as the password, as a fallback. Then a
release build goes on top. The migration build is a DEV build, so `publish_fw.mjs` refuses it:

```bash
idf.py -B build-migrate -DDEV_BUILD=1 -DCGDN1_MIGRATE=1 build
```

On the home network the device advertises itself over mDNS, so the page is at
`http://cgdn1.local/` regardless of the IP the router hands out, with no reservation needed.
`.local` resolves out of the box on macOS, iOS and Windows; some Android browsers and
multicast-blocking networks still need the raw IP.

## Build

ESP-IDF v5.5.5 and wasi-sdk 34: the build compiles the standard plugins from `../plugins/standard/`
and embeds them. Install wasi-sdk once, activate ESP-IDF, then build:

```bash
plugins/get-wasi-sdk.sh # wasi-sdk into ~/.local, once
. ~/esp/esp-idf/export.sh # ESP-IDF v5.5.5, where Espressif's guide installs it
cd firmware
idf.py build
```

The build signs the image and stops when the signing key is missing. In a fresh checkout make one
with `idf.py secure-generate-signing-key secure_boot_signing_key.pem` (the path
`sdkconfig.defaults` names, ignored by git), or point `CONFIG_SECURE_BOOT_SIGNING_KEY` in the local
`sdkconfig` at a key kept elsewhere; see "Signed OTA images" below.

A release build needs no `components/secrets/secrets.h`. Copy `secrets.h.example` there only for
the development switches: the `DEV_*` switches in `secrets.h` (open diagnostics, early confirm, a test stand for the release
channel) compile only with `idf.py -DDEV_BUILD=1 build`, and such a build reports `"dev":true` in
`GET /status`. The CMake cache keeps the flag: pass `-DDEV_BUILD=0` or run `fullclean` to go back
to a release build.

### Test builds

The endpoints that drive the device from outside for soak and interaction tests (`/touch` with
injected gestures, `/oled` with raw panel commands and the crash check, `/gpio`, `/i2c`) are not in
this repository. They live in the private `noxend/cgdn1-dev` repository as the ESP-IDF component
`test_hooks`, which defines `api_test_hooks_register` (`components/api/test_hooks.h`) over the
weak default that registers nothing. Build such an image in its own directory, so the release
build's cache never holds it:

```bash
idf.py -B build-soak -DEXTRA_COMPONENT_DIRS=$HOME/Desktop/repos/cgdn1-dev/components build
```

It reports `"dev":true` and `"test_hooks":true` in `GET /status`, and `tools/publish_fw.mjs`
refuses it: the publisher builds with an empty `EXTRA_COMPONENT_DIRS`, checks the build's
component list, and refuses an ELF where `nm` shows `api_test_hooks_register` as anything but `W`,
the weak default, whatever the test component is named. The web interface shows its Developer page whenever `dev` is true, so a `DEV_BUILD`
image without `test_hooks` shows it too, with the diagnostics only; the controls that need the test
endpoints appear when `test_hooks` is true.

## First flash (UART, case open)

The whole procedure for a stock device, with photos, is [docs/install.md](../docs/install.md).
Device powered by its own USB-C (from battery the chip cannot enter download mode). Enter download
mode by hand (hold IO0 to GND, tap EN to GND, release IO0), then:

```bash
idf.py -p /dev/cu.usbserial-0001 -b 115200 flash monitor
```

This writes the bootloader too. The bootloader is never updated over OTA, and its rollback and
watchdog settings are part of the lock-out protection, so this UART flash must happen before the
case is sealed.

## OTA (WiFi, how every later update ships)

```bash
read -rs CGDN1_TOKEN && export CGDN1_TOKEN   # the device password, not echoed
# -H @file keeps the password out of curl's arguments, which other users can list
auth() { printf 'Authorization: Bearer %s\n' "$CGDN1_TOKEN"; }
curl -H @<(auth) http://<device-ip>/status
curl -X POST --data-binary @build/cgdn1.bin -H @<(auth) \
     http://<device-ip>/ota         # upload; device verifies, switches slot, reboots
```

Only images built from this project are accepted. After the reboot the new image confirms itself
after 60 s of being healthy: HTTP server up, and joined to home WiFi or serving a request on the
setup AP. If you updated over the setup AP without home WiFi, open `http://10.47.0.1/` within 3
minutes, otherwise the update rolls back.

The System page of the web interface does the same upload from the browser, and installs releases too: the device
reads `https://cgdn1.swithy.com/firmware.json` hourly, shows "New version" in the header when it
names a version above its own, and on "Update" downloads the image itself (`POST /update`). Optional auto update at 04:00 (`POST /update/auto`, off by default). A release is
published with

```bash
node tools/publish_fw.mjs --notes "What changed"   # --dry-run builds and signs only
```

which builds from a clean tree with `DEV_BUILD` off and refuses a version not above the live one.
The manifest is signed with the web manifest's key; the device still holds the image to the
manifest's size and sha256 and to its own version, and `esp_ota_end` checks the image signature.

## Lock-out protection

- **Power latch**: GPIO23 is raised from a constructor before `app_main`, then pad hold keeps it
  high through software, panic and watchdog resets.
- **Dual OTA slots**: an update writes the other slot; the running good app is never erased.
- **Rollback gate**: an unconfirmed image that is not healthy within 3 minutes reboots and the
  bootloader rolls back. A crash before confirmation rolls back too.
- **Bootloader RTC watchdog** stays armed while an image is unconfirmed, so a hang before
  confirmation also ends in a rollback.
- **Boot-loop counter**: three crash resets in a row roll back even a confirmed image, provided the
  other slot holds a working app. Brownouts are not counted.
- **WiFi**: reconnects forever with backoff (5 s doubling to 60 s). The setup AP (WPA2/WPA3, PMF
  required) runs while there is no home network, for 5 minutes after the station gets an address,
  and whenever the station is lost. "Wi-Fi setup" in the device menu, chosen while the station has no
  address, keeps the AP up for 10 minutes instead of 5 after the station next joins; chosen while
  it has one, the menu asks "Start hotspot?" and Yes brings the AP up for 10 minutes. With no network
  saved the screen shows the Wi-Fi setup page at boot and after 30 s of no touch in the menu.
  After a save from the setup page, the first join of the next boot shows "Connected" with
  `cgdn1.local` and the IP for 5 minutes (30 s after a touch) or until a tap or a power button click (`net_saved_before_boot`, a `.noinit` flag).
- **Captive portal**: the AP's DHCP offer names 10.47.0.1 as the DNS server and a raw lwIP UDP pcb
  (no socket, no task) answers every A question with that address (`net/dns_answer.c`). A setup AP
  client asking for another host, whatever the path, gets a 302 to
  `http://10.47.0.1/` (`auth_captive_redirect`), so a phone's connectivity check opens the setup
  page. The Host check still answers 421 to everyone else. DHCP option 114 is not offered: RFC
  8910 wants an HTTPS URI there.
- **Restart, factory reset and a check now over HTTP**: `POST /reboot`, `POST /reset` and
  `POST /update/check` (at most every 30 s; 409 while offline) need a signed-in caller. `/status`
  carries `ip`, `host` and `wifi_fail` (`password` or `not_found`) for anyone, and `checking`,
  `check_failed` and, after a rollback, `rolled_back` when signed in.
- **Factory reset without UART**: the device menu's "Factory reset" erases `nvs_cfg` (settings,
  the config password, the AP password), the Wi-Fi credentials (both the `wifi` namespace and the
  driver's own copy) and restarts into the setup AP with a new AP password.
  Both OTA slots and the confirmed image stay.
- **Signed OTA images**: every image is signed at build time (ECDSA, the secure boot v1 scheme,
  verified in software by `esp_ota_end`; the bootloader and eFuses are untouched, so this is not
  hardware secure boot). The key lives outside the checkout, at `~/.config/cgdn1/ota_signing_key.pem`:
  the local `sdkconfig` names that path in `CONFIG_SECURE_BOOT_SIGNING_KEY`, while
  `sdkconfig.defaults` keeps the in-tree name, so a fresh checkout fails to build until the key is
  in place. Keep a copy off this machine: without it no further OTA image is accepted and the device
  has to be opened for a serial flash.
- **Signed web manifest**: the hosted interface is published with `tools/publish_web.mjs`, which signs
  with `~/.config/cgdn1/web_signing_key.pem`; its public half is `components/net/web_manifest_pub.pem`. Keep a
  copy of this key too: without it the interface cannot be updated until a firmware with a new
  public key ships. Both keys are unencrypted, mode 0600, by decision (2026-10-19): a passphrase would
  mean every build and every publish waits for the owner to type it.
- **Host check**: every request must name the device in `Host` (`cgdn1.local`, or `cgdn1-2.local`
  after an mDNS name clash, `10.47.0.1` or the station's IP), so a web page whose DNS record is
  pointed at the device gets 421. A name the router made up for the device (`espressif.lan` and
  the like) is refused too: use the IP or the mDNS name.
- **Setup AP clients need PMF** (802.11w): any phone or laptop from the last ten years has it;
  without it the client cannot join the setup network.

The password and the session cookie travel over plaintext HTTP, so anyone who can read the
local network's traffic can take a session; the signature is what keeps a foreign image out.

## Pre-seal checklist (all on hardware, case still open)

1. Setup page over the AP, join home WiFi, status page reachable from the LAN.
2. OTA over the LAN; new image confirms after 60 s.
3. OTA over the AP with the router off; open the page within 3 min; image confirms.
4. Deliberately bad images roll back: one that never gets healthy, one that crashes 10 s after
   start, one that hangs before confirmation.
5. OTA and the forced rollback on battery only, USB-C unplugged: the device stays powered.
6. Router off for 5 minutes, then on: the device rejoins by itself.
7. Keep the UART pads reachable (a small hole or a JST pigtail) as the last resort. Recovery of last
   resort is always a full reflash of the 4 MB backup over UART.

## On the device: menu, power button, night profile, temperature

- **Touch strip**: a slide moves the carousel to the next screen. In the menu a slide to the right
  moves down the list, to the left up, and a tap selects. A hold on the strip does nothing; it is
  kept for later.
- **Menu** (`components/ui/menu.c`, host test in `test/plugins/run.sh`): a 1 s hold on the power
  button opens it, a click goes back one page and closes it from the list. Rows: Power off,
  Restart, Wi-Fi setup (a Wi-Fi QR code of the AP with its name; a slide shows name, password,
  address as text), Factory reset (asks No / Yes, reset), Info (version, IP, SSID, uptime, Back),
  Back. The menu closes after 30 s without a touch. Its strings are English.
- **Power button** (GPIO36, pulled low by the ATtiny814 while held): `button.c` polls the line,
  `button_press.c` decides (host test `test/button/run.sh`). A click blanks or wakes the panel and
  the LED, or goes back in the menu; a 1 s hold opens the menu; a 5 s hold shows "Powering off" and
  powers the board off whatever the menu does (on USB it stays dark until the next hold restarts
  it). The menu's Power off takes the same path on the main task. While an update is unconfirmed
  power off is ignored, since a power cut would roll it back. The panel shows "Starting" from
  `oled_init` until the first screen. `GET /status` reports `btn` (armed, level, clicks, holds,
  longs) and `off`, the reason the last power off noted.
- **Night profile**: one record in the core settings, read and written as `night` in `GET /config`
  and `POST /config` (off by default, 22:00 to 08:00, screen 30 %, indicator 20 %). Inside the
  window the panel's level table is scaled down or the panel goes dark, and the LED is scaled.
  The Display page of the web interface edits it.
- **Temperature correction**: an offset of -3.0 to +3.0 °C in steps of 0.1 (`temp_offset.c`, host
  test `test/temp_offset/run.sh`), one record in the core settings, read and written as
  `temp_offset` in `GET /config` and `POST /config` (0 by default). `sensor_get` adds it to every
  temperature, so the panel, the plugins and `GET /sensors` all show the corrected value. It also
  recomputes every humidity for the corrected temperature at the same water vapour content
  (`temp_offset_humidity`, Sensirion's Physics of Humidity application note, equation 10), capped
  at 100 %RH. The System page of the web interface edits it.
- **Burn-in protection**: a 2 px pixel orbit applied when the frame is flushed, one step every
  60 s, always on.
- **Time**: SNTP from `pool.ntp.org`, the zone fixed to Kyiv (`net.c`).

## Partitions

`partitions.csv`: nvs, otadata, phy_init, ota_0 (0x10000), ota_1 (0x1F0000), nvs_cfg. ota_0/ota_1
offsets match the stock image, a known-good layout on this flash.

## Screen plugins

Every screen is a plugin, and every plugin is a WebAssembly module: how to write one is
[plugins/README.md](../plugins/README.md). The device runs them with WAMR's classic
interpreter, everything it allocates in PSRAM; without PSRAM no screen runs.

- **The standard plugins** (alerts, clock, humidity, temperature, co2) are built from
  `plugins/standard/` by `plugins/build.sh` during the firmware build
  (`components/wasm_host/CMakeLists.txt`), and the image embeds the result. At the first boot, after a
  factory reset, and on the first boot of 1.0, the device installs the ones missing, once (a record
  in the core namespace of `nvs_cfg` says it did). From then on they are deleted and installed like
  an upload; the store offers a deleted one again from the image (`POST /plugins/install`).
- **Storage** (`wasm_store.c`): each module in one of two banks of `nvs_cfg` under its id, and a
  record naming the bank with the module's size and SHA-256; an update writes the other bank, then
  the record. The module carries its `plugin.json` in the custom section `cgdn1.plugin`, read again
  at every load (`manifest.c`, on the in-place JSON reader `json_scan.c`).
- **The plugin table** (`wasm_plugins.c`): one entry per installed plugin, grown as plugins are
  installed, its index stable until the plugin is deleted. The core keeps its own state per index
  (faults, two timers, a generation that drops answers and ticks meant for a plugin since
  replaced). Nothing has a fixed count: storage, PSRAM and the internal heap are the limits, and a
  refused install says which (`space`, `memory`, `heap`).
- **Settings** are stored per plugin id as before; a select stores its option's key. A label an
  older firmware stored ("24 години") is rewritten to the key when the plugin loads.
- **Budget**: every call into a plugin runs under 50,000 instructions and 200 ms, or the plugin is
  stopped. The core also times every callback: one over 250 ms is a fault, three disable the
  plugin until the next boot, one is forgiven every ten minutes; web previews are not counted.

Tests on the Mac: `test/json` (the JSON reader against Python's), `test/store` (storage, manifest
rules, the record of 1.0.0-rc7), `test/wasm` (the loader, the limits, the SDK calls, instruction
counts of the standard plugins), `test/plugins` (every standard plugin's frames, LED and stats
against what the native screens of rc7 drew, and the device menu), `test/upload` (first boot,
installs, deletions, an older firmware's settings, installing until the storage is full).

