import { useEffect, useState } from "react";
import { parseTime } from "@internationalized/date";

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

// The device keeps one time zone, Kyiv (TZ_KYIV in net.c), and /status carries no
// clock, so the device's minute is the browser's clock read in that zone.
// "Europe/Kiev" is the older name, which every browser's zone data still resolves.
const DEVICE_CLOCK = new Intl.DateTimeFormat("en-GB", {
  timeZone: "Europe/Kiev",
  hour: "numeric",
  minute: "numeric",
  hourCycle: "h23",
});

function deviceMinute() {
  const parts = DEVICE_CLOCK.formatToParts(new Date());
  const part = (type) => Number(parts.find((p) => p.type === type).value);
  return part("hour") * 60 + part("minute");
}

// The minute of the day on the device, kept current while the page is open.
export function useDeviceMinute() {
  const [minute, setMinute] = useState(deviceMinute);

  useEffect(() => {
    const timer = setInterval(() => setMinute(deviceMinute()), 10000);
    return () => clearInterval(timer);
  }, []);

  return minute;
}
