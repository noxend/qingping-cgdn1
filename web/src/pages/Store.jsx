import { Button, EmptyState } from "@heroui/react";

import { PageTitle } from "../components/PageTitle";
import { PluginCard } from "../components/PluginCard";
import { PluginGrid } from "../components/PluginGrid";
import { SectionHeader } from "../components/SectionHeader";
import { StatusChip } from "../components/StatusChip";
import { UploadPlugin } from "../components/UploadPlugin";
import { formatBytes } from "../lib/bytes";
import { CATEGORIES } from "../lib/manifest";
import { useLang, useT } from "../i18n";

// Install a deleted standard plugin, add one to the screens, or a chip saying where it is.
function CardAction({ plugin, onScreens, onAdd, isBusy, listChanging, store }) {
  const t = useT();

  if (!plugin.installed) {
    return (
      <Button
        size="sm"
        variant="secondary"
        className="shrink-0"
        isPending={store.isPending("install", plugin.id)}
        isDisabled={Boolean(store.pending)}
        onPress={() => store.install(plugin)}
      >
        {t("store.install")}
      </Button>
    );
  }

  if (plugin.disabled) {
    return <StatusChip color="danger">{t("plugin.switchedOff")}</StatusChip>;
  }

  if (onScreens) {
    return <StatusChip>{t("store.onScreens")}</StatusChip>;
  }

  return (
    <Button
      size="sm"
      variant="secondary"
      className="shrink-0"
      isPending={isBusy("add", plugin.id)}
      isDisabled={listChanging}
      onPress={() => onAdd(plugin.id)}
    >
      {t("store.add")}
    </Button>
  );
}

// The catalogue, filtered by category or by the search query. The heading adds one's
// own plugin and says how much plugin storage is free.
export function Store({ catalog, screens, query, category, onAdd, isBusy, listChanging, store }) {
  const { t, lang } = useLang();

  const q = query.trim().toLowerCase();
  const words = (s) => [s.name, s.description, t("category." + s.category)].join(" ");
  const matches = (s) => !q || words(s).toLowerCase().includes(q);
  const list = catalog.filter((s) => (!category || s.category === category) && matches(s));

  const storage = store.device?.storage;
  const bytes = (n) => formatBytes(n, lang);
  const room =
    storage &&
    t("memory.storageFree", {
      free: bytes(storage.free),
      total: bytes(storage.modules + storage.free),
    });

  const title = CATEGORIES.includes(category) ? t("category." + category) : t("store.title");

  return (
    <>
      <PageTitle title={title} sub={room} action={<UploadPlugin store={store} />} />

      <SectionHeader title={q ? t("store.results") : t("store.all")} count={list.length} />

      {list.length === 0 ? (
        <EmptyState className="py-10 text-center">{t("store.none")}</EmptyState>
      ) : (
        <PluginGrid>
          {list.map((s) => (
            <PluginCard
              key={s.id}
              plugin={s}
              action={
                <CardAction
                  plugin={s}
                  onScreens={screens.includes(s.id)}
                  onAdd={onAdd}
                  isBusy={isBusy}
                  listChanging={listChanging}
                  store={store}
                />
              }
            />
          ))}
        </PluginGrid>
      )}
    </>
  );
}
