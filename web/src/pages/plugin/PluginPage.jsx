import { Breadcrumbs, Button } from "@heroui/react";
import { navigate } from "wouter/use-hash-location";

import { HelpSections } from "./HelpSections";
import { InfoCard } from "./InfoCard";
import { PluginActions } from "./PluginActions";
import { SettingsCard } from "./SettingsCard";
import { Notice } from "../../components/Notice";
import { PageTitle } from "../../components/PageTitle";
import { PluginMedia } from "../../components/PluginMedia";
import { StatusChip } from "../../components/StatusChip";
import { useLang, useT } from "../../i18n";

function PluginBreadcrumbs({ plugin }) {
  const t = useT();

  return (
    <Breadcrumbs className="mb-6">
      <Breadcrumbs.Item href="#/store">{t("nav.store")}</Breadcrumbs.Item>
      <Breadcrumbs.Item href={"#/category/" + encodeURIComponent(plugin.category)}>
        {t("category." + plugin.category)}
      </Breadcrumbs.Item>
      <Breadcrumbs.Item>{plugin.name}</Breadcrumbs.Item>
    </Breadcrumbs>
  );
}

function NotFound() {
  const t = useT();

  return (
    <>
      <PageTitle title={t("plugin.notFound")} />
      <Button variant="secondary" onPress={() => navigate("/store")}>
        {t("plugin.toStore")}
      </Button>
    </>
  );
}

export function PluginPage({
  id,
  catalog,
  screens,
  live,
  vals,
  setVal,
  onAdd,
  onRemove,
  onSave,
  onSetActive,
  isBusy,
  listChanging,
  store,
}) {
  const { t, entry } = useLang();

  const plugin = catalog.find((s) => s.id === id);

  // While its own removal finishes the plugin may already be gone from the catalogue;
  // the page then waits for the move to the store instead of saying it is not found.
  if (!plugin && store.isPending("remove", id)) {
    return null;
  }

  if (!plugin) {
    return <NotFound />;
  }

  // A plugin the core switched off is still installed: it keeps its settings and comes
  // back after a reboot.
  const off = Boolean(plugin.disabled);
  const onScreens = screens.includes(plugin.id);
  const showingNow = onScreens && !off && plugin.active;
  const help = entry("help." + plugin.id) || {};

  return (
    <>
      <PluginBreadcrumbs plugin={plugin} />

      <div className="flex flex-col gap-6 lg:flex-row lg:items-end lg:justify-between">
        <div className="max-w-2xl">
          <h1 className="text-4xl font-bold tracking-tight sm:text-5xl">{plugin.name}</h1>
          <p className="mt-3 text-lg leading-relaxed text-muted">{plugin.description}</p>

          <div className="mt-3 flex flex-wrap items-center gap-3 text-sm leading-5 text-muted">
            <span>{t("category." + plugin.category)}</span>
            {showingNow && <StatusChip>{t("plugin.showingNow")}</StatusChip>}
            {off && <StatusChip color="danger">{t("plugin.switchedOff")}</StatusChip>}
          </div>
        </div>

        <PluginActions
          plugin={plugin}
          onScreens={onScreens}
          onAdd={onAdd}
          onRemove={onRemove}
          onSetActive={onSetActive}
          isBusy={isBusy}
          listChanging={listChanging}
          store={store}
        />
      </div>

      <div className="mt-10 grid grid-cols-1 gap-x-12 gap-y-12 lg:grid-cols-[1fr_340px]">
        <div className="flex flex-col gap-12">
          <PluginMedia plugin={plugin} />
          <HelpSections help={help} />
        </div>

        {/* On a phone the settings come before the description and the preview. */}
        <aside className="order-first flex flex-col gap-6 lg:order-none">
          {off && (
            <Notice
              status="danger"
              title={t("plugin.disabled.title")}
              description={t("plugin.disabled.text")}
            />
          )}

          <SettingsCard
            plugin={plugin}
            onScreens={onScreens}
            live={live}
            vals={vals}
            setVal={setVal}
            onSave={onSave}
            saving={isBusy("save", plugin.id)}
          />

          <InfoCard plugin={plugin} stored={store.installedOf(plugin.id)} credit={help.credit} />
        </aside>
      </div>
    </>
  );
}
