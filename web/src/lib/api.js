import useSWR, { mutate } from "swr";

export function postForm(path, fields) {
  return fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(fields).toString(),
  });
}

export function postJson(path, body) {
  return fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

// A 401 means the session is gone (it expired, or another tab signed out): /status
// then says so, and the page shows the sign-in.
export const signedOut = () => mutate("/status");

export async function signIn(password) {
  const response = await postJson("/login", { password });

  if (response.ok) {
    await signedOut();
  }

  return response;
}

export async function signOut() {
  await fetch("/logout", { method: "POST" });
  await signedOut();
}

// SWR schedules a key's next poll only after its request settles, so a request the
// device never answers would stop that key's polling: hence the timeout.
async function get(path, read) {
  const response = await fetch(path, { signal: AbortSignal.timeout(6000) });

  if (response.status === 401) {
    signedOut();
  }

  if (!response.ok) {
    throw new Error(`${path}: HTTP ${response.status}`);
  }

  return read(response);
}

const json = (path) => get(path, (response) => response.json());

// The framebuffer, and the indicator (RGB LED) colour from the X-Led header.
const screen = (path) =>
  get(path, async (response) => ({
    fb: new Uint8Array(await response.arrayBuffer()),
    led: response.headers.get("X-Led") || "off",
  }));

// The device serves one request at a time, so while a firmware file is sent the polls
// would only queue up beside the upload and slow it down.
let polling = true;

export function pausePolling(paused) {
  polling = !paused;
}

const isPaused = () => !polling;

// SWR stops the interval while a key holds an error and backs off (5 s, 10 s, 20 s,
// ...), so after a device reboot the page would stay stale for minutes. Here a failed
// key is retried at its own interval, keeping the last good data meanwhile.
export const poll = (ms) => ({
  refreshInterval: ms,
  isPaused,
  onErrorRetry: (_error, _key, _config, revalidate, options) => {
    setTimeout(revalidate, ms, options);
  },
});

export const useStatus = () => useSWR("/status", json, poll(8000));

export const useAlerts = () => useSWR("/alerts", json, poll(8000));

// Polled because another tab or the device itself can change settings and plugins.
export const useConfig = () => useSWR("/config", json, poll(15000));

// Polled fast while the page waits for an upload or a removal, since the device takes
// it up on its next second. A failed poll is retried at the slow interval either way.
// onAnswer(plugins) runs on every answer, an unchanged one too.
export const usePlugins = (waiting, onAnswer) =>
  useSWR("/plugins", json, {
    ...poll(10000),
    refreshInterval: waiting ? 1500 : 10000,
    keepPreviousData: true,
    onSuccess: onAnswer,
  });

// Rendered on the device off-screen, so every plugin can be shown at once, not just
// the one on the panel now.
export const usePluginScreen = (id) => {
  const key = id ? `/screen?id=${encodeURIComponent(id)}` : null;
  return useSWR(key, screen, poll(6000));
};
