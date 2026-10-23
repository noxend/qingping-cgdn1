import { useEffect, useState } from "react";

import { refused } from "../lib/answer";
import { request, TIMEOUT_MS, usePlugins } from "../lib/api";
import { notify } from "../lib/notify";
import { useLang } from "../i18n";

// The device takes an upload up on its next second; past this the page stops waiting.
// The device keeps its job and removal queue in RAM, so after a restart it never
// reports the end.
const PENDING_MAX_MS = 60 * 1000;

// Refusals about how the plugin was built: the owner reads this line, and the build
// hint goes under it.
const OWNER = {
  section: "store.owner.notPlugin",
  invalid: "store.owner.notPlugin",
  sdk: "store.owner.newer",
  old_sdk: "store.owner.older",
  json: "store.owner.broken",
  fields: "store.owner.broken",
  field: "store.owner.broken",
  import: "store.owner.broken",
  pages: "store.owner.broken",
  table: "store.owner.broken",
  start: "store.owner.broken",
  autorun: "store.owner.broken",
  export_type: "store.owner.broken",
  no_draw: "store.owner.broken",
  load: "store.owner.broken",
};

function send({ kind, id, bytes }) {
  const upload = kind === "upload";

  return request("/plugins/" + kind, {
    method: "POST",
    headers: { "Content-Type": upload ? "application/wasm" : "application/json" },
    body: upload ? bytes : JSON.stringify({ id }),
    timeoutMs: upload ? TIMEOUT_MS.plugin : TIMEOUT_MS.action,
  });
}

// The plugins on the device and the room for more: uploading a module, installing a
// standard plugin from the device's own copy, deleting one. onInstalled(id, update)
// and onUninstalled(id) follow a change.
export function usePluginStore({ onInstalled, onUninstalled }) {
  const { t, entry } = useLang();

  // The action the device is working on. Its `after` is the job number before it,
  // null while the request is still on its way.
  const [pending, setPending] = useState(null);
  // Why the last upload was refused, {text, detail}, shown in the upload dialog.
  const [uploadProblem, setUploadProblem] = useState(null);

  // The code the device gave and the name it is about (an import, an export, a
  // plugin.json field), in words. A name with a sentence of its own gets it; a code
  // without one reads as the general failure.
  const failure = (code, name) => {
    const specific = `store.err.${code}.${name}`;
    const general = "store.err." + code;

    let text = t("store.failed");
    if (name && entry(specific)) {
      text = t(specific);
    } else if (entry(general)) {
      text = t(general, { name });
    }

    return OWNER[code] ? { text: t(OWNER[code]), detail: text } : { text };
  };

  const report = (kind, problem) => {
    if (kind === "upload") {
      setUploadProblem(problem);
    } else {
      notify(problem.text, "danger", problem.detail);
    }
  };

  // Each answer to the poll can end the action. An install ends when the device reports
  // a job newer than the one before it, a removal when the device no longer lists the
  // plugin.
  const settle = (plugins) => {
    if (!pending || pending.after === null) {
      return;
    }

    if (pending.kind === "remove") {
      if (!(plugins.installed ?? []).some((p) => p.id === pending.id)) {
        onUninstalled(pending.id);
        setPending(null);
      }
      return;
    }

    const job = plugins.job;
    if (!job || job.n <= pending.after) {
      return;
    }

    if (job.stage === "done") {
      onInstalled(pending.id, pending.update);
      setPending(null);
    } else if (job.stage === "failed") {
      report(pending.kind, failure(job.code, job.name));
      setPending(null);
    }
  };

  const { data, mutate } = usePlugins(Boolean(pending), settle);
  const installed = data?.installed ?? [];

  const installedOf = (id) => installed.find((p) => p.id === id);

  useEffect(() => {
    if (!pending) {
      return;
    }

    const timer = setTimeout(() => {
      notify(t("store.err.timeout"), "warning");
      setPending(null);
    }, PENDING_MAX_MS);

    return () => clearTimeout(timer);
  }, [pending, t]);

  // True when the device took the POST. A refusal says why, as {"error": "field",
  // "field": "hosts"}, {"error": "memory"} and the like.
  const jobStarted = async (response, kind) => {
    if (refused(t, response)) {
      return false;
    }

    if (response.status === 409) {
      notify(t("store.err.busy"), "warning");
      return false;
    }

    if (response.ok) {
      return true;
    }

    const body = await response.json().catch(() => ({}));
    const problem = body.error
      ? failure(body.error, body.field ?? "")
      : { text: t("store.failed") };
    report(kind, problem);
    return false;
  };

  // The job number is read first: the action is over once the device reports a newer one.
  const start = async (action) => {
    if (pending) {
      return;
    }

    if (action.kind === "upload") {
      setUploadProblem(null);
    }

    const starting = { ...action, update: Boolean(installedOf(action.id)), after: null };
    setPending(starting);

    try {
      const response = await request("/plugins", { timeoutMs: TIMEOUT_MS.poll });
      if (refused(t, response)) {
        setPending(null);
        return;
      }

      const before = response.ok ? await response.json() : null;
      if (!before?.job) {
        report(action.kind, { text: t("store.failed") });
        setPending(null);
        return;
      }

      if (!(await jobStarted(await send(action), action.kind))) {
        setPending(null);
        return;
      }

      setPending({ ...starting, after: before.job.n });
      mutate();
    } catch {
      report(action.kind, { text: t("store.failed") });
      setPending(null);
    }
  };

  return {
    device: data,
    installedOf,
    pending,
    isPending: (kind, id) => pending?.kind === kind && pending.id === id,
    uploadProblem,
    // A file refused in the page, before it reaches the device (lib/wasmManifest.js);
    // null clears the last refusal.
    refuseUpload: (code) => setUploadProblem(code ? failure(code, "") : null),
    uninstall: (plugin) => start({ kind: "remove", id: plugin.id }),
    install: (plugin) => start({ kind: "install", id: plugin.id }),
    upload: (id, bytes) => start({ kind: "upload", id, bytes }),
  };
}
