import { useCallback, useMemo, useRef, useState } from "react";

import { postJson, useAlerts, useConfig, useStatus } from "../lib/api";
import { localize, optionLabel } from "../lib/manifest";
import { notify } from "../lib/notify";
import { useLang } from "../i18n";

// One empty list until /config arrives, so memoised children keep a stable prop.
const NONE = [];

// The firmware's own defaults for the night profile (night_rules.c).
const NIGHT_DEFAULT = {
  enabled: false,
  start: "22:00",
  end: "08:00",
  screen_pct: 30,
  screen_off: false,
  led_pct: 20,
};

function withValues(config, id, values) {
  const catalog = config.catalog.map((s) => {
    if (s.id !== id) {
      return s;
    }
    return { ...s, values: { ...s.values, ...values } };
  });

  return { ...config, catalog };
}

// The edits whose value differs from what was sent.
function unsent(edits = {}, sent) {
  const left = {};

  for (const [key, value] of Object.entries(edits)) {
    if (value !== sent[key]) {
      left[key] = value;
    }
  }

  return left;
}

// Every value a plugin's settings form shows: the device's, with unsaved edits on top.
function formValues(catalog, edits) {
  const values = {};

  for (const s of catalog) {
    values[s.id] = {};
    for (const p of s.params || []) {
      values[s.id][p.key] = edits[s.id]?.[p.key] ?? p.value;
    }
  }

  return values;
}

// The device state the pages read and the actions that change it. Unsaved edits win
// over the device's values until saved, so a poll does not overwrite them.
export function useDeviceConfig() {
  const { t, lang } = useLang();

  const { data: status } = useStatus();
  const { data: alerts, mutate: reloadLive } = useAlerts();
  const { data: config, error: configError, mutate: updateConfig } = useConfig();

  const [busy, setBusy] = useState(null);
  const [edits, setEdits] = useState({});
  const [nightEdits, setNightEdits] = useState({});
  const listBusy = useRef(false);

  const catalog = useMemo(
    () => (config?.catalog ?? NONE).map((entry) => localize(entry, lang)),
    [config, lang],
  );

  // The alert plugin's counters, with the region worded as its setting's option.
  const live = useMemo(() => {
    const plugin = catalog.find((s) => s.id === "alerts");
    return alerts && { ...alerts, region: optionLabel(plugin, "region", alerts.region) };
  }, [alerts, catalog]);

  // `enabled` leaves out a plugin the device switched off, though it is still in the
  // stored list. Every list the page posts starts from `screens`, so an add or a
  // removal elsewhere never drops it.
  const enabled = config?.enabled ?? NONE;
  const screens = useMemo(() => {
    const off = catalog.filter((s) => s.disabled && !enabled.includes(s.id)).map((s) => s.id);
    return [...enabled, ...off];
  }, [catalog, enabled]);

  const vals = useMemo(() => formValues(catalog, edits), [catalog, edits]);

  const setVal = useCallback((sid, key, value) => {
    setEdits((e) => ({ ...e, [sid]: { ...e[sid], [key]: value } }));
  }, []);

  // A firmware without the night profile in /config shows the defaults.
  const savedNight = useMemo(() => ({ ...NIGHT_DEFAULT, ...config?.night }), [config]);
  const night = useMemo(() => ({ ...savedNight, ...nightEdits }), [savedNight, nightEdits]);

  const setNight = useCallback((key, value) => {
    setNightEdits((e) => ({ ...e, [key]: value }));
  }, []);

  // POST /config with `body`; `afterSave` brings the cached /config up to date.
  // `busyWith`, {kind, id}, names the request while it is on its way.
  const send = async ({ busyWith, body, done, failed, afterSave }) => {
    setBusy(busyWith);

    try {
      const response = await postJson("/config", body);
      if (!response.ok) {
        notify(failed, "danger");
        return;
      }

      await afterSave();
      notify(done);
    } catch {
      notify(failed, "danger");
    } finally {
      setBusy(null);
    }
  };

  const setEnabledList = async ({ list, busyWith, done, failed }) => {
    if (listBusy.current) {
      return;
    }

    listBusy.current = true;
    await send({
      busyWith,
      body: { enabled: list },
      done,
      failed,
      afterSave: async () => {
        await updateConfig((d) => ({ ...d, enabled: list }));
        reloadLive();
      },
    });
    listBusy.current = false;
  };

  const onAdd = (id, done = t("toast.added")) =>
    setEnabledList({
      list: [...screens, id],
      busyWith: { kind: "add", id },
      done,
      failed: t("toast.addFailed"),
    });

  const onRemove = (id, done = t("toast.removed")) =>
    setEnabledList({
      list: screens.filter((x) => x !== id),
      busyWith: { kind: "remove", id },
      done,
      failed: t("toast.removeFailed"),
    });

  const onSetActive = (id) =>
    send({
      busyWith: { kind: "show", id },
      body: { active: id },
      done: t("toast.shown"),
      failed: t("toast.switchFailed"),
      afterSave: async () => {
        await updateConfig();
        reloadLive();
      },
    });

  const onSave = (sid) => {
    const sent = vals[sid];

    return send({
      busyWith: { kind: "save", id: sid },
      body: { screen: sid, values: sent },
      done: t("toast.saved"),
      failed: t("toast.saveFailed"),
      // The saved values reach the cache before the edits are dropped, so the form
      // keeps them even if the re-read that follows fails. Fields changed while the
      // request was in flight stay edited.
      afterSave: async () => {
        await updateConfig((d) => withValues(d, sid, sent));
        setEdits((e) => ({ ...e, [sid]: unsent(e[sid], sent) }));
        reloadLive();
      },
    });
  };

  const onSaveNight = () =>
    send({
      busyWith: { kind: "night" },
      body: { night },
      done: t("toast.saved"),
      failed: t("toast.saveFailed"),
      afterSave: async () => {
        await updateConfig((d) => ({ ...d, night }));
        setNightEdits((e) => unsent(e, night));
      },
    });

  return {
    status,
    live,
    catalog,
    screens,
    loaded: config !== undefined,
    configError,
    isBusy: (kind, id) => busy?.kind === kind && busy.id === id,
    // An install or removal is in flight. Each one posts the whole list, so a second
    // one started meanwhile would be built from the stale list and undo the first.
    listChanging: busy?.kind === "add" || busy?.kind === "remove",
    vals,
    setVal,
    night,
    savedNight,
    setNight,
    onAdd,
    onRemove,
    onSave,
    onSaveNight,
    onSetActive,
    // Reads /config again, after a plugin was installed, updated or deleted.
    reload: () => updateConfig(),
  };
}
