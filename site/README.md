# site

`index.html` is the root page of `https://cgdn1.swithy.com`: it tells a visitor, in English and
Ukrainian, to open the monitor at `http://cgdn1.local/` from the same Wi-Fi network.
`tools/publish_web.py` copies it there along with the hosted web interface (`/v/<version>/`,
`manifest.json`). The same host serves the firmware releases (`/fw/<version>/cgdn1.bin`,
`firmware.json`), published by `tools/publish_fw.py`.
