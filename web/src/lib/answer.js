import { notify } from "./notify";

// The device's {"error": "..."} from a failed response, or "" when it gave none.
export async function deviceError(response) {
  const body = await response.json().catch(() => ({}));
  return typeof body.error === "string" ? body.error : "";
}

// True, with a notice, when the device refused the request: signed out (request in
// api.js has already sent the page back to the sign-in), a wrong password tried again
// too soon, or no password set yet (then only the setup AP may set one).
export function refused(t, response) {
  if (response.status === 401) {
    notify(t("app.signedOut"), "warning");
    return true;
  }

  if (response.status === 429) {
    notify(t("system.waitRetry"), "warning");
    return true;
  }

  if (response.status === 403) {
    notify(t("system.needAp"), "warning");
    return true;
  }

  return false;
}

// True when the device took the request. Otherwise a notice says why: a refusal above,
// or `failed` with the device's own reason under it.
export async function accepted(t, response, failed) {
  if (refused(t, response)) {
    return false;
  }

  if (!response.ok) {
    notify(failed, "danger", await deviceError(response));
    return false;
  }

  return true;
}
