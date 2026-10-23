import { Label, Meter } from "@heroui/react";

import { TitledCard } from "../../components/TitledCard";
import { formatBytes } from "../../lib/bytes";
import { useLang } from "../../i18n";

// Amber past 75 %, red past 90 %.
function MemoryBar({ label, used, total, word }) {
  const share = total > 0 ? (100 * used) / total : 0;
  const color = share > 90 ? "danger" : share > 75 ? "warning" : "accent";

  return (
    <Meter value={share} color={color} size="sm" valueLabel={word}>
      <Label>{label}</Label>
      <Meter.Output />
      <Meter.Track>
        <Meter.Fill />
      </Meter.Track>
    </Meter>
  );
}

// The memory plugins live in, from GET /plugins: the plugin storage on flash and the
// working memory (PSRAM) they run in, and the device's own memory as a line, since it
// has no total worth a bar. How many plugins fit depends on these alone.
export function MemoryCard({ plugins }) {
  const { t, lang } = useLang();

  const bytes = (n) => formatBytes(n, lang);
  const usedFree = (used, free) => t("memory.usedFree", { used: bytes(used), free: bytes(free) });

  const { storage, psram, heap } = plugins;
  const psramUsed = psram.total - psram.free;

  return (
    <TitledCard title={t("memory.title")}>
      <div className="flex flex-col gap-5">
        <MemoryBar
          label={t("memory.storage")}
          used={storage.modules}
          total={storage.modules + storage.free}
          word={usedFree(storage.modules, storage.free)}
        />

        {psram.total > 0 && (
          <MemoryBar
            label={t("memory.psram")}
            used={psramUsed}
            total={psram.total}
            word={usedFree(psramUsed, psram.free)}
          />
        )}

        {heap && (
          <p className="text-sm text-muted">{t("memory.heapFree", { free: bytes(heap.free) })}</p>
        )}
      </div>
    </TitledCard>
  );
}
