import useSWR, { mutate } from "swr";

// A 401 means the session is gone (it expired, or another tab signed out): /status
// then says so, and the page shows the sign-in.
export const signedOut = () => mutate("/status");

// How long the page waits for the device before it gives up. A poll ends before the
// next one is due; an action gets longer; an upload gets what a slow link needs (a
// plugin module, or 1.6 MB of firmware, which took up to 55 s).
export const TIMEOUT_MS = {
  poll: 6000,
  action: 10000,
  plugin: 30000,
  firmware: 180000,
};

// Every request to the device goes through here: it ends after timeoutMs, and a 401
// sends the page back to the sign-in.
export async function request(path, { timeoutMs = TIMEOUT_MS.action, ...init } = {}) {
  const signal = AbortSignal.timeout(timeoutMs);
  const response = await fetch(path, { ...init, signal });

  if (response.status === 401) {
    signedOut();
  }

  return response;
}

// request() for a body whose upload progress the page shows, which fetch cannot report:
// onProgress gets the share sent so far, from 0 to 1.
export function upload(path, body, { timeoutMs, onProgress }) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open("POST", path);
    xhr.timeout = timeoutMs;

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        onProgress(e.loaded / e.total);
      }
    };

    xhr.onload = () => {
      if (xhr.status === 401) {
        signedOut();
      }

      resolve(new Response(xhr.responseText || null, { status: xhr.status }));
    };

    xhr.onerror = xhr.ontimeout = () => reject(new Error(`${path}: no answer`));
    xhr.send(body);
  });
}

export function postForm(path, fields) {
  return request(path, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(fields).toString(),
  });
}

export function postJson(path, body) {
  return request(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

export async function signIn(password) {
  const response = await postJson("/login", { password });

  if (response.ok) {
    await signedOut();
  }

  return response;
}

export async function signOut() {
  await request("/logout", { method: "POST" });
  await signedOut();
}

// SWR schedules a key's next poll only after its request settles, so a request the
// device never answers would stop that key's polling: hence the poll timeout.
async function get(path, read) {
  const response = await request(path, { timeoutMs: TIMEOUT_MS.poll });

  if (!response.ok) {
    throw new Error(`${path}: HTTP ${response.status}`);
  }

  return read(response);
}

export const readJson = (path) => get(path, (response) => response.json());

export const readText = (path) => get(path, (response) => response.text());

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

export const useStatus = () => useSWR("/status", readJson, poll(8000));

export const useAlerts = () => useSWR("/alerts", readJson, poll(8000));

// Nothing is asked while `active` is false.
export const useSensors = (active, ms = 8000) =>
  useSWR(active ? "/sensors" : null, readJson, poll(ms));

// Polled because another tab or the device itself can change settings and plugins.
export const useConfig = () => useSWR("/config", readJson, poll(15000));

// Polled fast while the page waits for an upload or a removal, since the device takes
// it up on its next second. A failed poll is retried at the slow interval either way.
// onAnswer(plugins) runs on every answer, an unchanged one too.
export const usePlugins = (waiting, onAnswer) =>
  useSWR("/plugins", readJson, {
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
