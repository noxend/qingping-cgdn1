import { ArrowRightFromSquare } from "@gravity-ui/icons";
import { Button } from "@heroui/react";

import { AddressLinks } from "./AddressLinks";
import { PowerAction } from "./PowerAction";
import { InfoList, InfoRow } from "../../components/InfoRow";
import { TitledCard } from "../../components/TitledCard";
import { signOut } from "../../lib/api";
import { useT } from "../../i18n";

export function DeviceCard({ status }) {
  const t = useT();

  const connected = status?.wifi === "connected";
  // "host" came with /reboot and /reset (1.0.0); the hosted page also reaches older firmware.
  const canRestart = status !== undefined && "host" in status;

  const footer = (
    <div className="flex w-full flex-col gap-3">
      {canRestart && (
        <div className="flex w-full flex-wrap gap-3">
          <PowerAction
            path="/reboot"
            label={t("system.restart")}
            title={t("system.restartTitle")}
            text={t("system.restartText")}
            done={t("system.restarting")}
            variant="secondary"
          />
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

      <Button fullWidth variant="secondary" onPress={signOut}>
        <ArrowRightFromSquare />
        {t("system.signOut")}
      </Button>
    </div>
  );

  return (
    <TitledCard title={t("system.device")} footer={footer}>
      <InfoList>
        {connected && status.host && (
          <InfoRow label={t("system.address")}>
            <AddressLinks status={status} />
          </InfoRow>
        )}
        <InfoRow label={t("system.firmware")}>{status ? status.fw : "…"}</InfoRow>
      </InfoList>
    </TitledCard>
  );
}
