import { useCallback, useEffect, useLayoutEffect, useRef } from "react";
import useSWR from "swr";

import { refused } from "../../lib/answer";
import { poll, readJson, readText, request } from "../../lib/api";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

// One request to the device: {ok, status, text}.
async function devCall(path) {
  const response = await request(path);
  return { response, text: await response.text() };
}

// The device answers errors as {"error": "..."} (API 1); older firmware sent text. The
// text is the firmware's own English and is shown as it comes.
function errorText({ response, text }) {
  try {
    const body = JSON.parse(text);
    if (body && typeof body.error === "string") {
      return body.error;
    }
  } catch {
    // Not JSON: an older firmware's plain text.
  }

  return text.trim() || "HTTP " + response.status;
}

// Runs a device action and reports a failure in a toast. Resolves to the response
// text, or null when it failed.
export function useDevAction() {
  const t = useT();

  return useCallback(
    async (label, path) => {
      try {
        const answer = await devCall(path);

        if (refused(t, answer.response)) {
          return null;
        }

        if (!answer.response.ok) {
          notify(`${label}: ${errorText(answer)}`, "danger");
          return null;
        }

        return answer.text;
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
// The same key and reader as useStatus, polled faster while this page is open.
export const useDevStatus = () => useSWR("/status", readJson, poll(3000));

// The first line of GET /touch: gesture counters and the carousel's timing figures.
export const useTouchStats = () =>
  useSWR("/touch", async (path) => (await readText(path)).split("\n")[0], poll(2000));

export const hex2 = (n) => n.toString(16).toUpperCase().padStart(2, "0");
