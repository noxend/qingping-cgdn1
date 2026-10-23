import { useMemo } from "react";
import { parseTime } from "@internationalized/date";

import { useDeviceZone, useNow } from "../../lib/timeZones";

// "HH:MM" from the device to a Time for the field, or null.
export function toTime(text) {
  if (!text) {
    return null;
  }

  try {
    return parseTime(text);
  } catch {
    return null;
  }
}

export function toText(time) {
  if (!time) {
    return null;
  }

  const two = (n) => String(n).padStart(2, "0");
  return `${two(time.hour)}:${two(time.minute)}`;
}

export const toMinute = (time) => time.hour * 60 + time.minute;

// The firmware's night_in_window (night_rules.c): an end before the start runs through
// midnight, and an equal start and end is never night.
export function inWindow(start, end, now) {
  if (start === end) {
    return false;
  }

  if (start < end) {
    return now >= start && now < end;
  }

  return now >= start || now < end;
}

// The minute of the day on the device, kept current while the page is open.
export function useDeviceMinute() {
  const zone = useDeviceZone();
  const now = useNow();

  const clock = useMemo(
    () =>
      new Intl.DateTimeFormat("en-GB", {
        timeZone: zone.iana,
        hour: "numeric",
        minute: "numeric",
        hourCycle: "h23",
      }),
    [zone],
  );
  const parts = clock.formatToParts(now);
  const part = (type) => Number(parts.find((p) => p.type === type).value);
  return part("hour") * 60 + part("minute");
}
