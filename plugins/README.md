# Writing a plugin

A plugin is a screen on the CGDN1: a small C program compiled to WebAssembly, uploaded from the
web interface, and run by the device in a sandbox. It draws a 160 x 128 grey frame, may light the
LED, read the sensors, the time and its own settings, and fetch JSON over HTTPS from the hosts it
names. Every screen on the device is one, the clock and the alert map included: their sources in
`standard/` are complete examples. This guide is everything needed to write one, and it is written
so it can be given to an AI assistant as the whole context.

## Quick start

```bash
cd plugins
./get-wasi-sdk.sh
cp -r template my_plugin
./preview.sh my_plugin
```

1. `get-wasi-sdk.sh` downloads the compiler (wasi-sdk 34, a 180 MB download, about 600 MB
   unpacked) into `~/.local`, once.
2. Copy `template/` to a folder of your own: it needs `plugin.c` and `plugin.json`, and may hold
   headers and data files they include. Change `"id"` and `"name"` in `plugin.json` first.
3. `preview.sh my_plugin` builds `my_plugin/my_plugin.wasm` and runs it on your computer as the
   device would, writing what it draws to `my_plugin/preview/*.png`.
4. In the web interface open Store, press "Add plugin", Choose file, pick `my_plugin.wasm`,
   check what it says about the plugin, and install it. The device starts
   showing it at once; its page has its settings, "Stop showing" and "Delete from device".

`build.sh my_plugin` builds without the preview. Tools: macOS or Linux, Python 3, and for the
preview also `cmake`, a C compiler and `curl` (on macOS `xcode-select --install` and
`brew install cmake`; on Debian or Ubuntu `sudo apt install cmake build-essential python3 curl`).
The first preview downloads and builds the runtime once.

## How a plugin works

The device calls functions the plugin exports, one at a time, on its UI task. Each call must
return quickly: there is no main loop, no sleeping and no waiting. Work that takes time is asked
for (a timer, a network request) and its result comes back as a later call.

| Export       | When the device calls it                                                              | Signature                               |
| ------------ | ------------------------------------------------------------------------------------- | --------------------------------------- |
| `draw`       | to paint the frame: when the screen is shown, after `core_redraw()`, for web previews | `void draw(void)`                       |
| `init`       | once, before anything else                                                            | `void init(void)`                       |
| `on_config`  | after `init`, and each time the user saves a setting                                  | `void on_config(void)`                  |
| `on_event`   | the screen was shown or hidden, or a timer fired                                      | `void on_event(int type, int timer_id)` |
| `net_buffer` | a network answer of `len` bytes arrived: return where to put it                       | `char *net_buffer(int len)`             |
| `on_net`     | after the body was copied into that buffer                                            | `void on_net(int status, int len)`      |
| `led`        | to set the LED while the screen is shown: `0xRRGGBB`, or -1 for off                   | `int led(void)`                         |

Only `draw` is required. Export each with the macro, for example
`WASM_EXPORT("draw") void my_draw(void) { ... }`; the C name does not matter, the string does.

`draw` must paint from state the other callbacks prepared and must not start work: the device also
calls it for previews in the web page, where requests and timers are refused.

## The SDK

Include `plugin_wasm.h` (in `sdk/`, which build.sh puts on the include path) and nothing from the
firmware. These are all the functions a plugin can call.

### Drawing

The frame is `GFX_W` x `GFX_H` = 160 x 128 pixels, 16 grey levels: 0 is off, 15 full. The device
puts the frame on the panel when `draw` returns.

| Call                                              | What it does                                          |
| ------------------------------------------------- | ----------------------------------------------------- |
| `void gfx_clear(void)`                            | sets every pixel to 0                                 |
| `void gfx_pixel(int x, int y, int level)`         | one pixel; outside the frame it is ignored            |
| `void gfx_text_begin(void)`                       | starts a batch of text                                |
| `void gfx_font(int font)`                         | `GFX_FONT_TEXT` or `GFX_FONT_DIGITS` for what follows |
| `void gfx_text(int x, int y, const char *s)`      | text with its top-left corner at x, y (UTF-8)         |
| `void gfx_text_centered(int y, const char *s)`    | text centred across the width, top at y               |
| `int gfx_text_width(const char *s)`               | its width in pixels in the current font               |
| `void gfx_text_blit(int level)`                   | paints the batch at one grey level                    |
| `void gfx_blit(x, y, w, h, pixels, bpp, palette)` | a picture, see below                                  |

Text goes in batches: `gfx_text_begin`, `gfx_font`, any number of `gfx_text` calls, then
`gfx_text_blit(level)`. A second grey level is a second batch.

- `GFX_FONT_TEXT` is 9 x 15 pixels per character, Latin and Cyrillic: about 17 characters fit
  across. It has no degree sign: `ui_caption` and `ui_label` draw one, or `ui_ring` alone.
- `GFX_FONT_DIGITS` is 42 pixels tall, for numbers: only `0-9 : . - + , / *` and space. A
  character a font lacks draws nothing.
- One call draws at most 127 bytes of text.

`gfx_blit(int x, int y, int w, int h, const void *pixels, int bpp, const uint8_t *palette)` draws
a picture of `w` x `h` pixels (at most the frame's size) at x, y in one call. Each row holds `bpp`
bits per pixel (1, 2, 4 or 8), the first pixel in the highest bits of a byte, and starts on a new
byte. A pixel's value picks an entry of `palette` (`1 << bpp` entries): a grey level 0 to 15, or
`GFX_KEEP` to leave that pixel as it is. Draw anything with more than a few hundred pixels this way:
a loop of `gfx_pixel` calls runs out of instructions. A picture made by a script ahead of time can
be a C array in a header beside `plugin.c`, or a file the code embeds with C23's `#embed`.
`standard/alerts` draws its map from an 8-bit picture whose palette it changes with the alerts.

### The standard screen

Every screen the device comes with uses one layout, and a plugin that uses it looks like part of
the device. Each part is centred across the panel:

| Part    | Call                         | Font              | Top at              | Grey level            |
| ------- | ---------------------------- | ----------------- | ------------------- | --------------------- |
| caption | `ui_caption(const char *s)`  | `GFX_FONT_TEXT`   | `UI_CAPTION_Y` (8)  | `UI_LEVEL_TEXT` (11)  |
| value   | `ui_value(const char *s)`    | `GFX_FONT_DIGITS` | `UI_VALUE_Y` (32)   | `UI_LEVEL_VALUE` (15) |
| label   | `ui_label(const char *s)`    | `GFX_FONT_TEXT`   | `UI_LABEL_Y` (90)   | `UI_LEVEL_TEXT` (11)  |

- The caption is optional: what the value is when the label does not say it (`USD / UAH`, `PM`).
- The value is the one number the screen is for. Keep it to what fits: about five digits.
- The label is the unit (`°C`, `%`, `ppm`), or a date, or why there is no value.
- With nothing to show (no reading yet, no answer, a clock not set) the value is `--`:
  `ui_value(NULL)` or `ui_value("")` draws it. The label keeps the unit, or says why in words:
  `no data` after a failed answer, `loading` while the first is out.

Call them after `gfx_clear()`, in any order. They are functions in `plugin_wasm.h` that use the
calls above, so a screen that needs something else (a map, a chart) draws it with those and still
uses `GFX_FONT_TEXT` at `UI_LEVEL_TEXT` for its words. `template/` uses all three parts.

### Settings

`void plugin_setting_get(const char *key, char *out, int cap, const char *def)` copies the user's
value for `key` into `out` (at most `cap` bytes with the terminator), or `def` when it is not set.
Values are strings: a select gives its option's `key` (never its label, which depends on the
language), a toggle `"0"` or `"1"`, a number its digits. Read them in `on_config`. The keys are
the ones `plugin.json` declares in `"params"`; the web page shows the form, and a plugin reads
only its own settings.

### Sensors

```c
wasm_sensor_t r;
if (plugin_sensor_get(SENSOR_TEMP, &r, sizeof(r))) { /* r.value */ }
```

`SENSOR_TEMP` (degrees Celsius), `SENSOR_HUM` (percent), `SENSOR_CO2` (ppm), `SENSOR_PM25` and
`SENSOR_PM10` (µg/m³). It returns true when the reading is valid: false before the first reading
and when it is too old. `r.age_ms` is its age. The preview reports 22.5 °C, 45 %, 650 ppm, and
12 and 20 µg/m³.

### Time

`void core_localtime(wasm_tm_t *out, int size)` fills `out` with the device's local time, as C's
`struct tm` has it: `sec`, `min`, `hour`, `mday`, `mon` (0 to 11), `year` (since 1900), `wday`,
`yday`. Pass `sizeof(wasm_tm_t)`.

### Timers

`void core_timer_start(int id, int ms, bool periodic)` and `void core_timer_stop(int id)`. A
plugin has two timers, ids 0 and 1; each fires `on_event(CORE_EV_TIMER, id)` after `ms`
milliseconds (at least 100), once or every `ms`. Start them on `CORE_EV_SHOW` and stop them on
`CORE_EV_HIDE`: a screen that is not shown has nothing to update.

### Redrawing and the network state

`void core_redraw(void)` asks for a `draw` once the current callback returns. `bool
core_online(void)` says whether the device has a network address.

### Network

```c
bool core_net_get(const char *url, int max_bytes);
```

An HTTPS GET, answered later: true when the request was queued. The rules:

- the URL is `https://` and its host is one of `"hosts"` in `plugin.json`, exactly, with no port
  and no user name;
- one request at a time: a second call returns false until `on_net` came;
- a body longer than `max_bytes`, or than 32 KB, is not handed over: `on_net` gets `len` 0;
- false in a web preview's `draw`, so draw never fetches.

When the answer is in, the device calls `net_buffer(len)`; return a buffer of at least `len`
bytes in the plugin's own memory (a static array), or 0 to drop it. Then `on_net(status, len)`
with the HTTP status (0 when there was no response) and the length copied; a body comes only
with status 200. Parse the body there, store what `draw` needs, and call `core_redraw()`.
`examples/nbu/plugin.c` is a complete one, including a request wanted while another is out.

### JSON

The device reads JSON for a plugin, at its own speed, in a buffer in the plugin's memory: a 20 KB
answer read by a loop in the plugin would run out of instructions. A value is found by its
offset in the text, the text's start being 0, and -1 means "not there"; a value cut short or not
valid JSON counts as not there, so an answer cut at `max_bytes` gives nothing rather than half a
list.

| Call                                             | What it gives                                      |
| ------------------------------------------------ | -------------------------------------------------- |
| `int json_find(js, len, at, "key")`              | the value of member `key` of the object at `at`    |
| `int json_first(js, len, at)`                    | the first element of the array at `at`             |
| `int json_next(js, len, at)`                     | the element after the one at `at`                  |
| `int json_string(js, len, at, out, cap)`         | the string at `at`, decoded into `out`: its length |
| `int json_enum(js, len, at, list, sizeof(list))` | which of `"red\0yellow\0"` the string at `at` is   |

```c
// [{"cc":"USD","rate":41.83}]
int first = json_first(s_body, len, 0);
char cc[8];
json_string(s_body, len, json_find(s_body, len, first, "cc"), cc, sizeof(cc));
int rate = json_find(s_body, len, first, "rate");  // the number's text starts at s_body + rate
```

`json_enum` compares on the device too: matching a name against a list of 27 costs one call.
`standard/alerts/plugin.c` reads the whole alerts API this way in about 11,000 instructions.

### Stats

`void core_stats(const char *json)` publishes a short JSON object (at most 255 bytes) of the
plugin's own counters, served at `GET /plugin/stats?id=<id>` until the next call. Optional;
`standard/alerts` reports its fetches and the alert in the chosen region.

### The C library

There is no full libc. `sdk/libc_mini.c` is linked in and has `memcpy`, `memmove`, `memset`,
`memcmp`, `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strchr`, `strcspn`, `strstr`,
`snprintf` and `vsnprintf`. `snprintf` knows `%d %i %u %x %X %c %s %%`, the flags `-` and `0`, a
width, a precision for `%s` and `l`; it has no `%f`, so print a fraction as integers (see the
template's temperature). There is no `malloc`: use static arrays. Anything else the code calls is
an undefined symbol, and the build stops with its name.

## plugin.json

```json
{
  "id": "nbu_rate",
  "name": { "en": "NBU exchange rate", "uk": "Курс НБУ" },
  "description": {
    "en": "The National Bank of Ukraine's official rate of the hryvnia",
    "uk": "Офіційний курс гривні від Нацбанку"
  },
  "category": "finance",
  "version": "1.1.0",
  "hosts": ["bank.gov.ua"],
  "params": [
    {
      "key": "currency",
      "label": { "en": "Currency", "uk": "Валюта" },
      "type": "select",
      "options": [
        { "key": "usd", "label": { "en": "USD" } },
        { "key": "eur", "label": { "en": "EUR" } }
      ],
      "def": "usd"
    }
  ]
}
```

Every text the user reads is given per language: `{"en": "...", "uk": "..."}`. English is
required; Ukrainian is optional, and without it the page shows the English one. The device's own
screens use English.

| Field         | Rule                                                                                              |
| ------------- | ------------------------------------------------------------------------------------------------- |
| `id`          | 1 to 15 of `a-z`, `0-9` and `_`, not `sys`. Installing a plugin with an installed id replaces it. |
| `name`        | texts of up to 47 bytes of UTF-8 each (a Cyrillic letter is 2 bytes)                              |
| `description` | texts of up to 255 bytes each                                                                     |
| `category`    | one of `climate time weather security finance transport news home fun tools examples other`       |
| `version`     | up to 15 bytes, any text such as `1.0.0`                                                          |
| `hosts`       | up to 4 lowercase host names such as `bank.gov.ua`, no `https://`, paths or ports; `[]` for none  |
| `params`      | up to 16 settings; `[]` for none                                                                  |

A setting is `{"key", "label", "type", "def"}`: `key` 1 to 15 of `a-z`, `0-9` and `_`, `label`
texts of up to 63 bytes each, and `type` one of

- `select`, with `"options"`: 1 to 64 of `{"key", "label"}`, each key 1 to 23 of `a-z`, `0-9` and
  `_`, unique, and `def` one of the keys. The device stores the key, so a label can change, or be
  read in another language, without touching what the user chose;
- `text`, `def` up to 63 bytes, empty allowed;
- `number`, `def` an integer of up to 9 digits, as a string;
- `toggle`, `def` `"0"` or `"1"`.

No other fields, no key twice in one object, and the whole file at most 8 KB as build.sh writes
it. build.sh checks every rule and says which one failed; the device checks them again.

## Limits

| Limit             | Value                                                 | What happens past it                     |
| ----------------- | ----------------------------------------------------- | ---------------------------------------- |
| time per callback | 50,000 WebAssembly instructions and 200 ms            | the plugin is stopped                    |
| memory            | whole 64 KB pages that never grow, 8 KB of them stack | build.sh links as many as the data needs |
| network body      | 32 KB, one request at a time                          | not handed over, or refused              |
| timers            | 2, at least 100 ms                                    | shorter periods are raised to 100 ms     |
| plugins installed | as many as the plugin storage and working memory hold | the install is refused: space or memory  |

The device runs about 1.6 million instructions a second, so the budget is about 31 ms of the
plugin's own work. Measured: the clock's `draw` takes about 2,800 instructions, the alert map's
`draw` about 9,700 and its parsing of a 21 KB answer about 11,400. The store's "Memory for plugins"
shows the plugin storage (about 160 KB, the standard plugins taking about 50 KB of it) and the
working memory (4 MB, a plugin of one page takes about 80 KB), each as used and free. A stopped
plugin shows "stopped" on the device and on its page until the device restarts or it is installed
again.

## When it goes wrong

The preview and the device give the same reasons. In the web page each comes with what to fix.

| Seen                                   | Means                                                      | Fix                                                          |
| -------------------------------------- | ---------------------------------------------------------- | ------------------------------------------------------------ |
| build: `undefined symbol: name`        | a function that is neither in the SDK nor in `libc_mini.c` | use an SDK call, or write the function in `plugin.c`         |
| build: a `plugin.json` rule            | the field breaks the rule named                            | fix `plugin.json`                                            |
| refused: `section`                     | the file has no `plugin.json` inside                       | build with `build.sh`                                        |
| refused: `old_sdk`                     | built for older firmware (plugin.json format 1)            | rewrite plugin.json as above, build with `build.sh`          |
| refused: `space` or `memory`           | no room left in the plugin storage or the working memory   | delete a plugin you no longer use                            |
| refused: `heap`                        | the device is short of its own memory for the moment       | try again in a minute                                        |
| refused: `import name`                 | the module calls a function the device does not have       | as for an undefined symbol                                   |
| refused: `pages`                       | memory that could grow while the plugin runs               | build with `build.sh`                                        |
| refused: `table`                       | a function table of over 1024 entries                      | build with `build.sh`                                        |
| refused: `start` or `autorun name`     | code that would run before the limits apply                | remove `__attribute__((constructor))`, build with `build.sh` |
| refused: `export_type name`            | an export with the wrong signature                         | copy the signature from the table above                      |
| refused: `no_draw`                     | no `draw` export                                           | add `WASM_EXPORT("draw")`                                    |
| stopped: `instruction limit exceeded`  | a callback ran too long, often a loop that never ends      | do less per call; spread work over timer ticks               |
| stopped: `time limit exceeded`         | 200 ms spent, mostly in drawing calls                      | draw less text per frame                                     |
| stopped: `out of bounds memory access` | a pointer outside the plugin's memory                      | check buffer sizes and string terminators                    |

In the web page, a plugin that stopped or did not load shows why on its own page (Information,
Reason).

## Examples

- [template/](template/README.md): text from a setting, the time, the temperature, a timer. Start
  here.
- [examples/nbu/](examples/nbu/README.md): the National Bank's exchange rate, with a network
  request and its JSON.
- `standard/`: the screens the device comes with, built the same way as any other plugin. The
  firmware build builds them with `build.sh` and the image carries them; the device installs them
  at its first start.
  - [clock](standard/clock/README.md): three settings.
  - [temperature](standard/temperature/README.md) and [co2](standard/co2/README.md): sensors,
    and the LED in co2.
  - [humidity](standard/humidity/README.md): the LED as a comfort band.
  - [pm25](standard/pm25/README.md) and [pm10](standard/pm10/README.md): a caption over the
    value, and six LED colours.
  - [alerts](standard/alerts/README.md): an 8-bit map, 21 KB of JSON a call, stats.

## Writing a plugin with an AI assistant

Give the assistant this file, `sdk/plugin_wasm.h`, `template/plugin.c` and `template/plugin.json`,
then describe the screen you want. Ask it for `plugin.c` and `plugin.json` only, in C, with no
other files or libraries. Run `./preview.sh` on the result and paste back anything it prints after
"refused" or "stopped", and look at the PNG frames: the assistant cannot see the screen.
