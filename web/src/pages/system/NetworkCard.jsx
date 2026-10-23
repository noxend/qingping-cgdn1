import { useEffect, useState } from "react";
import { Button } from "@heroui/react";

import { AddressLinks } from "./AddressLinks";
import { Fold } from "./Fold";
import { NetworkForm } from "./NetworkForm";
import { Notice } from "../../components/Notice";
import { StatusChip } from "../../components/StatusChip";
import { TitledCard } from "../../components/TitledCard";
import { useStatus } from "../../lib/api";
import { useT } from "../../i18n";

// Long enough for the restart and a failed join, so a failure from before the save is
// not shown as the answer. A boot id in /status would make this exact.
const JOIN_GRACE_MS = 30000;

// After a save: where the device will be, kept on screen when the setup hotspot goes
// away with the restart. The poll reaches the device again if the phone rejoins the
// hotspot, and then says whether it joined.
function Joining({ name, status, onRetry }) {
  const t = useT();
  const [settled, setSettled] = useState(false);

  useEffect(() => {
    const timer = setTimeout(() => setSettled(true), JOIN_GRACE_MS);
    return () => clearTimeout(timer);
  }, []);

  // The address is public, and there only while the device is on its network.
  const joined = Boolean(status?.ip);
  const fail = settled && status?.wifi_fail;

  if (fail) {
    return (
      <div className="flex flex-col gap-4">
        <Notice
          status="danger"
          title={t("system.join.failed", { name })}
          description={t(`system.wifiFail.${fail}`)}
        />
        <Button fullWidth variant="secondary" onPress={onRetry}>
          {t("system.changeNet")}
        </Button>
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-3 text-sm">
      <p className="font-medium">
        {joined ? t("system.join.done", { name }) : t("system.join.title", { name })}
      </p>
      <p className="text-muted">{t("system.join.text", { name })}</p>
      <AddressLinks status={joined ? status : undefined} />
      <p className="text-muted">{t("system.join.info")}</p>
    </div>
  );
}

function ChangeNetwork({ status, editing, setEditing, onSaved }) {
  const t = useT();
  const fail = status?.wifi_fail;

  return (
    <div className="flex flex-col gap-4">
      {fail && (
        <Notice
          status="warning"
          title={t("system.cantJoin")}
          description={t(`system.wifiFail.${fail}`)}
        />
      )}

      <Fold
        label={t("system.changeNet")}
        variant="secondary"
        bodyClassName="pt-4"
        isExpanded={editing}
        onExpandedChange={setEditing}
      >
        <NetworkForm status={status} onSaved={onSaved} />
      </Fold>
    </div>
  );
}

// The network the device is on, why it cannot join, and the form behind "Change
// network". A new device opens straight on the form. `joining` (the network just
// saved) lives with the parent: after the first setup the page switches from the
// setup to the sign-in, and it must survive that.
export function NetworkCard({ status, joining, setJoining }) {
  const t = useT();
  const { mutate } = useStatus();
  const [editing, setEditing] = useState(false);

  const connected = status?.wifi === "connected";
  const firstSetup = status?.cfg === false;

  // The cached /status describes the network from before the save: drop what it says
  // about joining, so only an answer from after the restart fills it in again.
  const onSaved = (name) => {
    const before = { wifi: "not connected", ssid: "", ip: "", wifi_fail: "" };
    mutate((s) => s && { ...s, ...before }, { revalidate: false });
    setJoining(name);
  };

  const onRetry = () => {
    setJoining(null);
    setEditing(true);
  };

  let body;
  if (joining) {
    body = <Joining name={joining} status={status} onRetry={onRetry} />;
  } else if (firstSetup) {
    body = <NetworkForm status={status} onSaved={onSaved} />;
  } else {
    body = (
      <ChangeNetwork status={status} editing={editing} setEditing={setEditing} onSaved={onSaved} />
    );
  }

  const chip = (
    <StatusChip color={connected ? "success" : "warning"}>
      {connected ? t("system.connectedTo", { ssid: status.ssid }) : t("system.disconnected")}
    </StatusChip>
  );

  return (
    <TitledCard
      title={firstSetup && !joining ? t("setup.card") : "Wi-Fi"}
      description={!firstSetup && !joining && chip}
    >
      {body}
    </TitledCard>
  );
}
