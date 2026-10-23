import { Button } from "@heroui/react";

import { AlertStatus } from "../../components/AlertStatus";
import { ParamField } from "../../components/ParamField";
import { TitledCard } from "../../components/TitledCard";
import { useT } from "../../i18n";

function SettingsForm({ plugin, vals, setVal, onSave, saving }) {
  const t = useT();

  if (!plugin.installed) {
    return <p className="text-sm text-muted">{t("plugin.installToConfigure")}</p>;
  }

  if (plugin.params.length === 0) {
    return <p className="text-sm text-muted">{t("plugin.noSettings")}</p>;
  }

  const save = () => onSave(plugin.id);

  return (
    <>
      {plugin.params.map((p) => (
        <ParamField
          key={p.key}
          sid={plugin.id}
          p={p}
          value={vals[plugin.id]?.[p.key] ?? ""}
          setVal={setVal}
        />
      ))}

      <Button fullWidth variant="primary" isPending={saving} onPress={save}>
        {t("plugin.save")}
      </Button>
    </>
  );
}

// The device keeps an installed plugin's settings by its id, on the screens or not.
export function SettingsCard({ plugin, onScreens, live, vals, setVal, onSave, saving }) {
  const t = useT();

  return (
    <TitledCard title={t("plugin.settings")}>
      <div className="flex flex-col gap-3">
        <SettingsForm plugin={plugin} vals={vals} setVal={setVal} onSave={onSave} saving={saving} />
      </div>

      {onScreens && plugin.id === "alerts" && (
        <div className="mt-6 border-t border-separator pt-5">
          <AlertStatus live={live} />
        </div>
      )}
    </TitledCard>
  );
}
