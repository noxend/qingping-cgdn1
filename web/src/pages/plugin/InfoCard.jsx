import { Link } from "@heroui/react";

import { InfoList, InfoRow } from "../../components/InfoRow";
import { TitledCard } from "../../components/TitledCard";
import { formatBytes } from "../../lib/bytes";
import { useLang } from "../../i18n";

// Why the device stopped the plugin, in words when the dictionary has them.
function StopReason({ code }) {
  const { t, entry } = useLang();

  if (entry("store.err." + code)) {
    return t("store.err." + code, { name: "" });
  }

  return <span className="font-mono text-xs">{code}</span>;
}

// `stored` is the plugin as GET /plugins has it, with its size on the device.
export function InfoCard({ plugin, stored, credit }) {
  const { t, lang } = useLang();

  const stopped = plugin.installed && plugin.state !== "ready";

  return (
    <TitledCard title={t("plugin.info")}>
      <InfoList>
        <InfoRow label={t("plugin.category")}>{t("category." + plugin.category)}</InfoRow>

        {plugin.version && <InfoRow label={t("plugin.version")}>{plugin.version}</InfoRow>}

        {stored && <InfoRow label={t("plugin.size")}>{formatBytes(stored.size, lang)}</InfoRow>}

        {stopped && <InfoRow label={t("plugin.runtime")}>{t("plugin.stopped")}</InfoRow>}

        {plugin.error && (
          <InfoRow label={t("plugin.why")}>
            <StopReason code={plugin.error} />
          </InfoRow>
        )}

        {credit && (
          <InfoRow label={credit.label}>
            <Link
              href={credit.href}
              target="_blank"
              rel="noopener noreferrer"
              className="text-foreground underline-offset-4 hover:underline"
            >
              {credit.text}
            </Link>
          </InfoRow>
        )}
      </InfoList>
    </TitledCard>
  );
}
