import { useEffect, useState } from "react";
import { Button } from "@heroui/react";

import { useDevAction } from "./devApi";
import { TitledCard } from "../../components/TitledCard";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

// The first press arms the button for four seconds; the second one crashes the device.
export function DevDanger() {
  const t = useT();
  const run = useDevAction();
  const [armed, setArmed] = useState(false);

  useEffect(() => {
    if (!armed) {
      return undefined;
    }

    const timer = setTimeout(() => setArmed(false), 4000);
    return () => clearTimeout(timer);
  }, [armed]);

  const crash = async () => {
    if (!armed) {
      setArmed(true);
      return;
    }

    setArmed(false);
    await run(t("dev.danger.crash"), "/oled?crash=1");
    notify(t("dev.danger.crashed"), "warning");
  };

  return (
    <TitledCard title={t("dev.danger.title")}>
      <p className="text-sm text-muted">{t("dev.danger.text")}</p>
      <Button className="mt-4" variant="danger-soft" onPress={crash}>
        {armed ? t("dev.danger.confirm") : t("dev.danger.simulate")}
      </Button>
    </TitledCard>
  );
}
