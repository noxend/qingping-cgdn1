import { ArrowDownToLine, Play, Plus } from "@gravity-ui/icons";
import { Button } from "@heroui/react";

import { MoreMenu } from "./MoreMenu";
import { useT } from "../../i18n";

// What can be done with the plugin next: install it (a deleted standard plugin), show
// it now, add it to the screens, and the rest in a menu.
export function PluginActions({
  plugin,
  onScreens,
  onAdd,
  onRemove,
  onSetActive,
  isBusy,
  listChanging,
  store,
}) {
  const t = useT();

  const { id, installed, disabled, active } = plugin;
  const canShow = onScreens && !disabled && !active;
  const showing = isBusy("show", id);
  const adding = isBusy("add", id);

  return (
    <div className="flex shrink-0 items-center gap-2">
      {!installed && (
        <Button
          variant="primary"
          isPending={store.isPending("install", id)}
          isDisabled={Boolean(store.pending)}
          onPress={() => store.install(plugin)}
        >
          <ArrowDownToLine />
          {t("store.install")}
        </Button>
      )}

      {canShow && (
        <Button variant="primary" isPending={showing} onPress={() => onSetActive(id)}>
          <Play />
          {t("plugin.show")}
        </Button>
      )}

      {installed && !onScreens && (
        <Button
          variant="primary"
          isPending={adding}
          isDisabled={listChanging}
          onPress={() => onAdd(id)}
        >
          <Plus />
          {t("plugin.add")}
        </Button>
      )}

      {installed && (
        <MoreMenu
          plugin={plugin}
          onScreens={onScreens}
          onRemove={onRemove}
          listChanging={listChanging}
          store={store}
        />
      )}
    </div>
  );
}
