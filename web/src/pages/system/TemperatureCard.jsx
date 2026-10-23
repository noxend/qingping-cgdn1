import { useState } from "react";
import { Button, Description, Form, Label, NumberField } from "@heroui/react";

import { TitledCard } from "../../components/TitledCard";
import { postJson, useConfig, useSensors } from "../../lib/api";
import { notify } from "../../lib/notify";
import { LOCALES, useLang } from "../../i18n";

// The firmware's bounds (temp_offset.h): -3.0 to +3.0 °C in steps of 0.1 °C.
const LIMIT = 3;
const STEP = 0.1;

const CELSIUS = {
  style: "unit",
  unit: "celsius",
  minimumFractionDigits: 1,
  maximumFractionDigits: 1,
};

// The temperature calibration: an offset the device adds to every reading. Hidden on a
// firmware whose /config does not report one.
export function TemperatureCard() {
  const { t, lang } = useLang();

  const { data: config, mutate: updateConfig } = useConfig();
  const saved = config?.temp_offset;
  const { data: sensors, mutate: updateSensors } = useSensors(saved !== undefined);

  const [edit, setEdit] = useState(null);
  const [saving, setSaving] = useState(false);

  if (saved === undefined) {
    return null;
  }

  // A cleared field is NaN; nothing is sent until it holds a number again.
  const offset = edit ?? saved;
  const temp = sensors?.temp;
  const now = temp?.valid && new Intl.NumberFormat(LOCALES[lang], CELSIUS).format(temp.value);

  const onSubmit = async (e) => {
    e.preventDefault();
    const sent = Math.round(offset * 10) / 10;
    setSaving(true);

    try {
      const response = await postJson("/config", { temp_offset: sent });
      if (!response.ok) {
        notify(t("toast.saveFailed"), "danger");
        return;
      }

      await updateConfig((d) => ({ ...d, temp_offset: sent }));
      setEdit((current) => (current === offset ? null : current));
      updateSensors();
      notify(t("toast.saved"));
    } catch {
      notify(t("toast.saveFailed"), "danger");
    } finally {
      setSaving(false);
    }
  };

  return (
    <TitledCard title={t("system.temperature")}>
      <Form className="flex flex-col gap-6" onSubmit={onSubmit}>
        <NumberField
          fullWidth
          formatOptions={{ ...CELSIUS, signDisplay: "exceptZero" }}
          maxValue={LIMIT}
          minValue={-LIMIT}
          step={STEP}
          value={offset}
          variant="secondary"
          onChange={setEdit}
        >
          <Label>{t("system.tempOffset")}</Label>
          <NumberField.Group>
            <NumberField.DecrementButton />
            <NumberField.Input />
            <NumberField.IncrementButton />
          </NumberField.Group>
          {now && <Description>{t("system.tempNow", { value: now })}</Description>}
        </NumberField>

        <Button
          fullWidth
          isDisabled={Number.isNaN(offset)}
          isPending={saving}
          type="submit"
          variant="primary"
        >
          {t("system.tempSave")}
        </Button>
      </Form>
    </TitledCard>
  );
}
