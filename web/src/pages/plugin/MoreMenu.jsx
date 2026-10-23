import { useState } from "react";
import { CircleMinus, Ellipsis, TrashBin } from "@gravity-ui/icons";
import { Button, Dropdown, Label } from "@heroui/react";

import { DeleteDialog } from "./DeleteDialog";
import { useT } from "../../i18n";

// "Remove from the screens" while the plugin is on them, and "Delete" from the device.
export function MoreMenu({ plugin, onScreens, onRemove, listChanging, store }) {
  const t = useT();
  const [asking, setAsking] = useState(false);

  const disabledKeys = [];
  if (onScreens && listChanging) {
    disabledKeys.push("remove");
  }
  if (store.pending) {
    disabledKeys.push("delete");
  }

  const onAction = (key) => {
    if (key === "remove") {
      onRemove(plugin.id);
    }
    if (key === "delete") {
      setAsking(true);
    }
  };

  return (
    <>
      <Dropdown>
        <Button isIconOnly aria-label={t("plugin.more")} variant="secondary">
          <Ellipsis />
        </Button>

        <Dropdown.Popover placement="bottom end">
          <Dropdown.Menu disabledKeys={disabledKeys} onAction={onAction}>
            {onScreens && (
              <Dropdown.Item id="remove" textValue={t("plugin.remove")}>
                <CircleMinus className="size-4 shrink-0 text-muted" />
                <Label>{t("plugin.remove")}</Label>
              </Dropdown.Item>
            )}
            <Dropdown.Item id="delete" textValue={t("plugin.delete")} variant="danger">
              <TrashBin className="size-4 shrink-0 text-danger" />
              <Label>{t("plugin.delete")}</Label>
            </Dropdown.Item>
          </Dropdown.Menu>
        </Dropdown.Popover>
      </Dropdown>

      {asking && <DeleteDialog plugin={plugin} store={store} onClose={() => setAsking(false)} />}
    </>
  );
}
