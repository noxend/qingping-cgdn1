import { accepted } from "../../lib/answer";

// Long enough for a firmware upload over a slow link (1.6 MB took up to 55 s); a device
// that stops answering ends the request, and with it the polling pause.
const POST_TIMEOUT_MS = 180 * 1000;

// A POST to /ota, /update, /update/auto, /reboot or /reset: false, with a notice, when
// the device refused it. `failed` is the notice for a refusal of another kind, with
// the device's own reason under it.
export async function devicePost(t, path, body, failed) {
  const response = await fetch(path, {
    method: "POST",
    body,
    signal: AbortSignal.timeout(POST_TIMEOUT_MS),
  });

  return accepted(t, response, failed);
}
