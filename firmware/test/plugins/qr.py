"""The setup page's QR code must read back as the hotspot it stands for. Usage: qr.py <out>"""

import os
import sys

import zxingcpp
from PIL import Image

out = sys.argv[1]
hotspot = "WIFI:T:WPA;S:CGDN1-setup-C08D;P:mwx7k2pq9tre;;"
# SSID a;b:c\d and password p,q"r12345, escaped as the format asks.
escaped = r"WIFI:T:WPA;S:a\;b\:c\\d;P:p\,q\"r12345;;"
cases = {
    "menu_wifi": hotspot,
    "menu_wifi_qr": hotspot,
    "menu_wifi_online": hotspot,
    "menu_setup_boot": hotspot,
    "menu_wifi_escaped": escaped,
}
failed = 0
for name, want in cases.items():
    frame = Image.open(os.path.join(out, name + ".png"))
    small = frame.resize((frame.width * 3 // 5, frame.height * 3 // 5), Image.BILINEAR)
    for label, image in (("drawn", frame), ("60 %", small)):
        got = [r.text for r in zxingcpp.read_barcodes(image, formats=zxingcpp.BarcodeFormat.QRCode)]
        ok = got == [want]
        print(f"{name:18} qr {label:5} {'ok' if ok else 'FAIL: read ' + repr(got)}")
        failed += not ok
sys.exit(1 if failed else 0)
