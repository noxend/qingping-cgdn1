# qingping-cgdn1

Custom firmware for the Qingping Air Monitor Lite (CGDN1). It replaces the stock firmware on the
device's ESP32 and turns its screen into a set of pages you flip through with a finger, each one a
small plugin, set up from a web interface.

![The screens](docs/images/screens/all.png)

![The web interface, light and dark](docs/images/web/themes.png)

## What it does

- **Screens as plugins**: clock, temperature, humidity, CO2, PM2.5, PM10 and a map of air raid
  alerts in Ukraine. Slide along the touch strip for the next one. Delete any, install it again, or upload
  your own as a WebAssembly file.
- **Indicator LED** that follows the screen: CO2, particle and humidity levels, or the alert in your
  region.
- **Night mode** on a schedule, and **burn-in protection** for the OLED panel.
- **Device menu** on the power button: power off, restart, Wi-Fi setup, factory reset, info.
- **Web interface** at `http://cgdn1.local/` with a password: screens, settings, night mode, Wi-Fi,
  updates.
- **Signed updates over Wi-Fi** that roll back by themselves when a new version does not start.

It does not connect to Apple Home, Home Assistant or the Qingping app yet (HomeKit is planned to be
looked at, with a Home Assistant API as the fallback); the full list is in [using.md](docs/using.md#what-it-does-not-do).

## Documentation

For owners of the device:

- [Installing the firmware](docs/install.md): [opening the case](docs/install.md#3-open-the-case),
  wiring a USB-UART adapter, backing up the stock firmware, the first flash, Wi-Fi setup.
  Back up the stock firmware first: it holds the device's own HomeKit setup data and keys, and
  without it there is no known way back to Apple Home or the Qingping app.
- [Using the device](docs/using.md): screens, the touch strip, the power button and its menu, the
  web interface, updates, factory reset.
- [Release notes 1.0.0](docs/release-notes/1.0.0.md)

The screens:

- [Clock](plugins/standard/clock/README.md)
- [Temperature](plugins/standard/temperature/README.md)
- [Humidity](plugins/standard/humidity/README.md)
- [CO2](plugins/standard/co2/README.md)
- [PM2.5](plugins/standard/pm25/README.md)
- [PM10](plugins/standard/pm10/README.md)
- [Air raid alerts](plugins/standard/alerts/README.md)
- [NBU exchange rate](plugins/examples/nbu/README.md) (an example to install yourself)

For developers:

- [Development](docs/development.md): the repository layout and formatting.
- [Writing a plugin](plugins/README.md)
- [Firmware](firmware/README.md): building, signing, flashing, OTA, the lock-out protection,
  tests.
- [Web interface](web/README.md) and [the site root page](site/README.md)
- [Hardware](docs/hardware.md): the board, the test pads, the display, the buses, all reverse
  engineered.

## Credits

Air raid alert data: [neptun.in.ua](https://neptun.in.ua). Region boundaries:
[geoBoundaries](https://www.geoboundaries.org) (OpenStreetMap, ODbL) and
[Natural Earth](https://www.naturalearthdata.com).

Qingping's stock firmware and flash dumps are not in this repository: they are Qingping's code.
