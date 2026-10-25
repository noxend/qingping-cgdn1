import { accepted } from "../../lib/answer";
import { request } from "../../lib/api";

// A POST to /update, /update/auto, /reboot or /reset: false, with a notice, when the
// device refused it. `failed` is the notice for a refusal of another kind, with the
// device's own reason under it.
export async function devicePost(t, path, body, failed) {
  const response = await request(path, { method: "POST", body });

  return accepted(t, response, failed);
}
