import { useState } from "react";
import {
  Button,
  ColorArea,
  ColorField,
  ColorPicker,
  ColorSlider,
  ColorSwatch,
  ColorSwatchPicker,
  Label,
} from "@heroui/react";

import { useDevAction, useThrottled } from "./devApi";
import { TitledCard } from "../../components/TitledCard";
import { useT } from "../../i18n";

// The colours the screens use for the LED, plus white and blue for checking channels.
const PRESETS = ["#00FF00", "#FF9600", "#FF0000", "#FFFFFF", "#0000FF", "#FFFF00"];

function channels(color) {
  const rgb = color.toFormat("rgb");
  return ["red", "green", "blue"].map((channel) => rgb.getChannelValue(channel));
}

export function DevLed() {
  const t = useT();
  const run = useDevAction();
  const [color, setColor] = useState("#009600");

  const send = useThrottled((rgb) => {
    run("LED", `/oled?rgb=${rgb.join(",")}`);
  }, 120);

  const onColor = (next) => {
    setColor(next);
    send(channels(next));
  };

  const turnOff = () => {
    setColor("#000000");
    run("LED", "/oled?rgb=0,0,0");
  };

  return (
    <TitledCard title={t("dev.led.title")}>
      <ColorPicker value={color} onChange={onColor}>
        <div className="flex flex-col gap-4">
          <ColorArea
            aria-label={t("dev.led.color")}
            colorSpace="hsb"
            xChannel="saturation"
            yChannel="brightness"
            className="h-40 w-full max-w-none"
          >
            <ColorArea.Thumb />
          </ColorArea>
          <ColorSlider aria-label={t("dev.led.hue")} colorSpace="hsb" channel="hue">
            <ColorSlider.Track>
              <ColorSlider.Thumb />
            </ColorSlider.Track>
          </ColorSlider>
          <ColorField>
            <Label>HEX</Label>
            <ColorField.Group>
              <ColorField.Prefix>
                <ColorSwatch size="xs" />
              </ColorField.Prefix>
              <ColorField.Input />
            </ColorField.Group>
          </ColorField>
          <ColorSwatchPicker aria-label={t("dev.led.presets")} size="sm">
            {PRESETS.map((c) => (
              <ColorSwatchPicker.Item key={c} color={c}>
                <ColorSwatchPicker.Swatch />
                <ColorSwatchPicker.Indicator />
              </ColorSwatchPicker.Item>
            ))}
          </ColorSwatchPicker>
        </div>
      </ColorPicker>
      <div className="mt-4 flex gap-2">
        <Button variant="secondary" onPress={turnOff}>
          {t("dev.led.off")}
        </Button>
      </div>
      <p className="mt-3 text-sm text-muted">{t("dev.led.hint")}</p>
    </TitledCard>
  );
}
