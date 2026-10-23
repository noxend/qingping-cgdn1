import { inWindow, toMinute, toTime, useDeviceMinute } from "./nightTime";
import { useT } from "../../i18n";

// What the device does now under the saved profile, as one line.
export function NightState({ saved }) {
  const t = useT();
  const minute = useDeviceMinute();

  const start = toTime(saved.start);
  const end = toTime(saved.end);

  // An empty window (start equal to end) is never night, the same as switched off.
  const usable = saved.enabled && start && end && toMinute(start) !== toMinute(end);
  if (!usable) {
    return t("display.nightDisabled");
  }

  if (inWindow(toMinute(start), toMinute(end), minute)) {
    return t("display.nightUntilEnd", { end: saved.end });
  }

  return t("display.day", { start: saved.start });
}
