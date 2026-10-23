import { Button } from "@heroui/react";

import { CodeBlock } from "./CodeBlock";
import { InfoList, InfoRow } from "../../components/InfoRow";
import { TitledCard } from "../../components/TitledCard";
import { useSensors } from "../../lib/api";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

function uptime(t, seconds) {
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const key = d ? "dev.diag.uptimeDays" : "dev.diag.uptimeValue";
  return t(key, { d, h, m, s: seconds % 60 });
}

const reading = (t, r, unit) => (r?.valid ? `${r.value} ${unit}` : t("dev.diag.none"));

function DeviceFigures({ status }) {
  const t = useT();
  const drops = status.disc;

  return (
    <InfoList>
      <InfoRow label={t("dev.diag.firmware")}>
        {t("dev.diag.fwSlot", { fw: status.fw, slot: status.slot })}
      </InfoRow>
      <InfoRow label={t("dev.diag.uptime")}>{uptime(t, status.up_s)}</InfoRow>
      <InfoRow label={t("dev.diag.reset")}>{status.reset}</InfoRow>
      <InfoRow label="Wi-Fi">{t("dev.diag.wifi", { rssi: status.rssi, ch: status.ch })}</InfoRow>
      <InfoRow label={t("dev.diag.drops")}>
        {drops?.n}
        {drops?.n > 0 && t("dev.diag.lastDrop", { reason: drops.reason, at: drops.at_s })}
      </InfoRow>
      <InfoRow label={t("dev.diag.heapMin")}>
        {t("dev.diag.kb", { n: (status.heap_min / 1024).toFixed(1) })}
      </InfoRow>
    </InfoList>
  );
}

function SensorFigures({ status, sensors }) {
  const t = useT();

  return (
    <InfoList>
      <InfoRow label={t("dev.diag.temp")}>{reading(t, sensors?.temp, "°C")}</InfoRow>
      <InfoRow label={t("dev.diag.hum")}>{reading(t, sensors?.hum, "%")}</InfoRow>
      <InfoRow label="CO₂">{reading(t, sensors?.co2, "ppm")}</InfoRow>
      {Object.entries(status.stack_free || {}).map(([name, bytes]) => (
        <InfoRow key={name} label={t("dev.diag.stack", { name })}>
          {t("dev.diag.bytes", { n: bytes })}
        </InfoRow>
      ))}
    </InfoList>
  );
}

function LastCrash({ crash }) {
  const t = useT();

  const copy = async () => {
    try {
      await navigator.clipboard.writeText(crash);
      notify(t("dev.diag.copied"));
    } catch {
      notify(t("dev.diag.copyFailed"), "warning");
    }
  };

  return (
    <div className="mt-4">
      <div className="flex items-center justify-between">
        <p className="text-sm font-medium">{t("dev.diag.crash")}</p>
        {crash && (
          <Button size="sm" variant="secondary" onPress={copy}>
            {t("dev.diag.copy")}
          </Button>
        )}
      </div>

      {crash ? (
        <CodeBlock className="mt-2">{crash}</CodeBlock>
      ) : (
        <p className="mt-1 text-sm text-muted">{t("dev.diag.noCrash")}</p>
      )}
    </div>
  );
}

export function DevDiagnostics({ status }) {
  const t = useT();
  const { data: sensors } = useSensors(true, 3000);

  const crash = status?.crash ? JSON.stringify(status.crash, null, 2) : null;

  return (
    <TitledCard title={t("dev.diag.title")} className="lg:col-span-2">
      {status ? (
        <>
          <div className="mt-2 grid gap-x-10 lg:grid-cols-2">
            <DeviceFigures status={status} />
            <SensorFigures status={status} sensors={sensors} />
          </div>
          <LastCrash crash={crash} />
        </>
      ) : (
        <p className="mt-3 text-sm text-muted">{t("dev.diag.unavailable")}</p>
      )}
    </TitledCard>
  );
}
