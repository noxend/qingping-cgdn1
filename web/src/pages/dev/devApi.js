import { useCallback, useEffect, useLayoutEffect, useRef } from "react";
import useSWR from "swr";

import { sessionGone } from "../../lib/answer";
import { poll } from "../../lib/api";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

// One request to the device: {ok, status, text}.
async function devCall(path) {
  const response = await fetch(path, { signal: AbortSignal.timeout(10000) });
  return { ok: response.ok, status: response.status, text: await response.text() };
}

// The device answers errors as {"error": "..."} (API 1); older firmware sent text. The
// text is the firmware's own English and is shown as it comes.
function errorText(response) {
  try {
    const body = JSON.parse(response.text);
    if (body && typeof body.error === "string") {
      return body.error;
    }
  } catch {
    // Not JSON: an older firmware's plain text.
  }

  return response.text.trim() || "HTTP " + response.status;
}

// Runs a device action and reports a failure in a toast. Resolves to the response
// text, or null when it failed.
export function useDevAction() {
  const t = useT();

  return useCallback(
    async (label, path) => {
      try {
        const response = await devCall(path);

        if (response.status === 401) {
          sessionGone(t);
          return null;
        }

        if (!response.ok) {
          notify(`${label}: ${errorText(response)}`, "danger");
          return null;
        }

        return response.text;
      } catch {
        notify(t("dev.noAnswer", { label }), "danger");
        return null;
      }
    },
    [t],
  );
}

// Calls fn at most every `ms` while a slider is dragged, and once more with the last
// value, so the device ends on what the slider shows.
export function useThrottled(fn, ms) {
  const last = useRef(0);
  const timer = useRef(null);
  const latest = useRef(fn);

  useLayoutEffect(() => {
    latest.current = fn;
  });

  useEffect(() => () => clearTimeout(timer.current), []);

  return useCallback(
    (...args) => {
      const call = () => {
        last.current = Date.now();
        latest.current(...args);
      };

      clearTimeout(timer.current);

      const wait = last.current + ms - Date.now();
      if (wait <= 0) {
        call();
      } else {
        timer.current = setTimeout(call, wait);
      }
    },
    [ms],
  );
}

// /status with the diagnostics: reset reason, Wi-Fi, heap, stacks, the last crash.
export const useDevStatus = () =>
  useSWR("/status", async (path) => JSON.parse((await devCall(path)).text), poll(3000));

export const useSensors = () =>
  useSWR(
    "/sensors",
    async (path) => (await fetch(path, { signal: AbortSignal.timeout(6000) })).json(),
    poll(3000),
  );

// The first line of GET /touch: gesture counters and the carousel's timing figures.
export const useTouchStats = () =>
  useSWR("/touch", async (path) => (await devCall(path)).text.split("\n")[0], poll(2000));

export const hex2 = (n) => n.toString(16).toUpperCase().padStart(2, "0");
