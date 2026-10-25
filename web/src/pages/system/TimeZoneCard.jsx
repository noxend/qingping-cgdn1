import { useMemo, useState } from "react";

import { SelectField } from "../../components/SelectField";
import { TitledCard } from "../../components/TitledCard";
import { postJson, useConfig } from "../../lib/api";
import { notify } from "../../lib/notify";
import { TIME_ZONES, useDeviceZone, useNow } from "../../lib/timeZones";
import { LOCALES, useLang } from "../../i18n";

// The device's time zone, saved as soon as another is picked. Hidden on a firmware
// whose /config does not report one.
export function TimeZoneCard() {
  const { t, lang } = useLang();

  const { data: config, mutate: updateConfig } = useConfig();
  const zone = useDeviceZone();
  const now = useNow();
  const [saving, setSaving] = useState(false);

  const clock = useMemo(
    () =>
      new Intl.DateTimeFormat(LOCALES[lang], {
        timeZone: zone.iana,
        hour: "2-digit",
        minute: "2-digit",
      }),
    [lang, zone],
  );

  if (config?.time_zone === undefined) {
    return null;
  }

  const options = TIME_ZONES.map((z) => ({ key: z.key, label: z[lang] }));
  const time = clock.format(now);

  const onChange = async (key) => {
    if (!key || key === zone.key) {
      return;
    }

    setSaving(true);

    try {
      const response = await postJson("/config", { time_zone: key });
      if (!response.ok) {
        notify(t("toast.saveFailed"), "danger");
        return;
      }

      await updateConfig((d) => ({ ...d, time_zone: key }));
      notify(t("toast.saved"));
    } catch {
      notify(t("toast.saveFailed"), "danger");
    } finally {
      setSaving(false);
    }
  };

  return (
    <TitledCard title={t("system.time")}>
      <SelectField
        label={t("system.timeZone")}
        description={t("system.timeNow", { time })}
        isDisabled={saving}
        options={options}
        value={zone.key}
        onChange={onChange}
      />
    </TitledCard>
  );
}
