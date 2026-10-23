import { accepted } from "../../lib/answer";
import { request, TIMEOUT_MS } from "../../lib/api";

// A POST to /ota, /update, /update/auto, /reboot or /reset: false, with a notice, when
// the device refused it. `failed` is the notice for a refusal of another kind, with
// the device's own reason under it. A firmware upload gets the long timeout; a device
// that stops answering ends the request, and with it the polling pause.
export async function devicePost(t, path, body, failed) {
  const timeoutMs = path === "/ota" ? TIMEOUT_MS.firmware : TIMEOUT_MS.action;
  const response = await request(path, { method: "POST", body, timeoutMs });

  return accepted(t, response, failed);
}
