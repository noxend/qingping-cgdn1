# PM10

Plugin id `pm10`. Coarse particles (PM10) from the device's Plantower particulate sensor, in whole
µg/m³, in large digits under a `PM10` caption. The label reads `ug/m3`: the panel's text font has
no µ or ³.

![PM10](../../../docs/images/screens/pm10.png)

## Settings

None.

## Data

The firmware reads the sensor once a second, as the stock firmware does, and shows the value the
stock would: as measured while PM2.5 is up to 61, above that the stock's correction, 999 at
most. The first two readings after power on are skipped. The screen checks the value every second while it is shown;
with no valid reading it shows `--`.

## LED

The stock firmware's levels and colours:

| PM10, µg/m³ | LED |
|---|---|
| up to 50 | green |
| 51 to 150 | yellow |
| 151 to 250 | orange |
| 251 to 350 | red |
| 351 to 450 | purple |
| above 450 | dark red |
| no reading | off |
