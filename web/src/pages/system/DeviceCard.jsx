import { ArrowRightFromSquare } from "@gravity-ui/icons";
import { Button } from "@heroui/react";

import { PowerAction } from "./PowerAction";
import { TitledCard } from "../../components/TitledCard";
import { signOut } from "../../lib/api";
import { useT } from "../../i18n";

// What is done to the device as a whole, the factory reset last.
export function DeviceCard({ status }) {
  const t = useT();

  // "host" came with /reboot and /reset (1.0.0); the hosted page also reaches older firmware.
  const canRestart = status !== undefined && "host" in status;

  return (
    <TitledCard title={t("system.device")}>
      <div className="flex flex-col gap-3">
        <div className="flex flex-wrap gap-3">
          {canRestart && (
            <PowerAction
              path="/reboot"
              label={t("system.restart")}
              title={t("system.restartTitle")}
              text={t("system.restartText")}
              done={t("system.restarting")}
              variant="secondary"
            />
          )}
          <Button className="min-w-fit flex-1" variant="secondary" onPress={signOut}>
            <ArrowRightFromSquare />
            {t("system.signOut")}
          </Button>
        </div>

        {canRestart && (
          <div className="flex">
            <PowerAction
              path="/reset"
              label={t("system.reset")}
              title={t("system.resetTitle")}
              text={t("system.resetText")}
              done={t("system.resetting")}
              variant="danger-soft"
            />
          </div>
        )}
      </div>
    </TitledCard>
  );
}
