# CGDN1 web interface (React + HeroUI)

The device's web interface: sign-in, the screens and their settings, the plugin store, night mode,
Wi-Fi, updates, in English and Ukrainian. How it is used is in
[docs/using.md](../docs/using.md#the-web-interface); the API it talks to is in
[firmware/README.md](../firmware/README.md#sign-in).

It reaches the browser two ways:

- **Hosted**: the device serves a small shell that loads the bundle from
  `https://cgdn1.swithy.com/v/<version>/`, named by a manifest the device checks against its key.
  `tools/publish_web.mjs` builds, signs and publishes it.
- **Built into the firmware**: one gzipped file, served when the hosted bundle cannot load.

## Build and embed

```bash
npm install                   # at the repository root; web is an npm workspace
npm run build                 # Vite single-file -> web/dist/index.html (everything inlined)
gzip -9 -c web/dist/index.html > firmware/components/api/app.html.gz
```

`firmware/components/api/CMakeLists.txt` embeds `app.html.gz`. It is committed so the firmware
builds without Node; regenerate it with the commands above after changing the app.

## Without the device

`mock_device.py` answers the device's API in its formats, so the page can be worked on with hot
reload and no board. It builds the standard plugins at start, so it needs wasi-sdk
(`plugins/get-wasi-sdk.sh`); the password is `test`.

```bash
python3 -I web/mock_device.py --port 8137
DEVICE=http://localhost:8137 npm run dev -w web
```
