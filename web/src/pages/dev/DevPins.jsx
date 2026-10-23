import { useState } from "react";
import { Button } from "@heroui/react";

import { CodeBlock } from "./CodeBlock";
import { useDevAction } from "./devApi";
import { SwitchField } from "../../components/SwitchField";
import { TitledCard } from "../../components/TitledCard";
import { useT } from "../../i18n";

function PinSwitch({ pin, label, hint }) {
  const run = useDevAction();
  const [on, setOn] = useState(false);

  const onChange = (next) => {
    setOn(next);
    run(`GPIO${pin}`, `/gpio?pin=${pin}&v=${next ? 1 : 0}`);
  };

  return <SwitchField label={label} description={hint} isSelected={on} onChange={onChange} />;
}

export function DevPins() {
  const t = useT();
  const run = useDevAction();
  const [scan, setScan] = useState("");

  const scanI2c = async () => {
    const text = await run("I2C", "/i2c");
    if (text !== null) {
      setScan(text);
    }
  };

  return (
    <TitledCard title={t("dev.pins.title")}>
      <div className="flex flex-col gap-5">
        <PinSwitch pin={13} label={t("dev.pins.gpio13")} hint={t("dev.pins.gpio13Hint")} />
        <PinSwitch pin={18} label={t("dev.pins.gpio18")} hint={t("dev.pins.gpio18Hint")} />

        <div>
          <Button variant="secondary" onPress={scanI2c}>
            {t("dev.pins.scan")}
          </Button>
          {scan && <CodeBlock className="mt-3">{scan}</CodeBlock>}
          <p className="mt-2 text-sm text-muted">{t("dev.pins.hint")}</p>
        </div>
      </div>
    </TitledCard>
  );
}
