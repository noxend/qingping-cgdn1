import { Label, Slider, TimeField } from "@heroui/react";

import { toText, toTime } from "./nightTime";

export function NightTimeField({ label, value, onChange, isDisabled }) {
  return (
    <TimeField
      fullWidth
      granularity="minute"
      hourCycle={24}
      isDisabled={isDisabled}
      shouldForceLeadingZeros
      value={toTime(value)}
      onChange={(time) => onChange(toText(time))}
    >
      <Label>{label}</Label>
      <TimeField.Group variant="secondary">
        <TimeField.Input>{(segment) => <TimeField.Segment segment={segment} />}</TimeField.Input>
      </TimeField.Group>
    </TimeField>
  );
}

export function PercentSlider({ label, value, onChange, minValue, isDisabled }) {
  return (
    <Slider
      isDisabled={isDisabled}
      maxValue={100}
      minValue={minValue}
      step={10}
      value={value}
      onChange={onChange}
    >
      <Label>{label}</Label>
      <Slider.Output>{({ state }) => `${state.values[0]} %`}</Slider.Output>
      <Slider.Track>
        <Slider.Fill />
        <Slider.Thumb />
      </Slider.Track>
    </Slider>
  );
}
