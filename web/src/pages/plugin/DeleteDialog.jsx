import { TrashBin } from "@gravity-ui/icons";
import { Button } from "@heroui/react";

import { ConfirmDialog } from "../../components/ConfirmDialog";
import { useT } from "../../i18n";

// Asks before a plugin and its settings are erased from the device.
export function DeleteDialog({ plugin, store, onClose }) {
  const t = useT();

  const confirm = (
    <Button slot="close" variant="danger" onPress={() => store.uninstall({ id: plugin.id })}>
      <TrashBin />
      {t("plugin.delete")}
    </Button>
  );

  return (
    <ConfirmDialog
      isOpen
      onOpenChange={(open) => !open && onClose()}
      status="danger"
      title={t("plugin.deleteTitle", { name: plugin.name })}
      text={t("plugin.deleteText")}
      cancelLabel={t("store.cancel")}
      confirm={confirm}
    />
  );
}
