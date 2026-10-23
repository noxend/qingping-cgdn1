import { useState } from "react";
import { Button, Input, Label, Slider, TextField } from "@heroui/react";

import { hex2, useDevAction, useThrottled } from "./devApi";
import { SelectField } from "../../components/SelectField";
import { TitledCard } from "../../components/TitledCard";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

// [button, the label its toast names, the /oled query]
const COMMANDS = [
  ["dev.display.off", "dev.display.offDone", "c=AE"],
  ["dev.display.on", "dev.display.onDone", "c=AF"],
  ["dev.display.reset", "dev.display.resetDone", "reset=1"],
  ["dev.display.test", "dev.display.test", "test=1"],
  ["dev.display.restore", "dev.display.restoreDone", "test=0"],
];

export function DevDisplay() {
  const t = useT();
  const run = useDevAction();

  const [level, setLevel] = useState(208);
  const [raw, setRaw] = useState("81,D0");
  const [flip, setFlip] = useState("0");

  const flips = [0, 1, 2, 3].map((i) => ({ key: String(i), label: t(`dev.display.flip${i}`) }));

  // Contrast is the two-byte command 0x81, value. 0xD0 is what the init table sets.
  const sendLevel = useThrottled((value) => {
    run(t("dev.display.brightness"), `/oled?c=81,${hex2(value)}`);
  }, 120);

  const onLevel = (value) => {
    setLevel(value);
    sendLevel(value);
  };

  const onFlip = (key) => {
    setFlip(key);
    run(t("dev.display.orientation"), `/oled?flip=${key}`);
  };

  const command = (label, query) => async () => {
    if (await run(label, `/oled?${query}`)) {
      notify(t("dev.done", { label }));
    }
  };

  const rawCommand = command(t("dev.display.command"), `c=${encodeURIComponent(raw)}`);

  return (
    <TitledCard title={t("dev.display.title")}>
      <div className="flex flex-col gap-5">
        <Slider minValue={0} maxValue={255} step={1} value={level} onChange={onLevel}>
          <div className="flex items-center justify-between">
            <Label>{t("dev.display.brightnessLabel")}</Label>
            <Slider.Output />
          </div>
          <Slider.Track>
            <Slider.Fill />
            <Slider.Thumb />
          </Slider.Track>
        </Slider>

        <div className="flex flex-wrap gap-2">
          {COMMANDS.map(([label, done, query]) => (
            <Button key={query} variant="secondary" onPress={command(t(done), query)}>
              {t(label)}
            </Button>
          ))}
        </div>

        <SelectField
          label={t("dev.display.orientation")}
          options={flips}
          value={flip}
          onChange={onFlip}
        />

        <div className="flex items-end gap-3">
          <TextField className="flex-1" variant="secondary">
            <Label>{t("dev.display.raw")}</Label>
            <Input variant="secondary" value={raw} onChange={(e) => setRaw(e.target.value)} />
          </TextField>
          <Button variant="secondary" onPress={rawCommand}>
            {t("dev.display.send")}
          </Button>
        </div>

        <p className="text-sm text-muted">{t("dev.display.rawHint")}</p>
      </div>
    </TitledCard>
  );
}
