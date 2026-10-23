import { useState } from "react";
import { Button } from "@heroui/react";

import { devicePost } from "./devicePost";
import { ConfirmDialog } from "../../components/ConfirmDialog";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

// Restart or factory reset: asks, then says what follows.
export function PowerAction({ path, label, title, text, done, variant }) {
  const t = useT();

  const [open, setOpen] = useState(false);
  const [sending, setSending] = useState(false);

  const danger = variant === "danger-soft";

  const run = async () => {
    setSending(true);

    try {
      if (await devicePost(t, path, undefined, t("system.actionFailed"))) {
        setOpen(false);
        notify(done);
      }
    } catch {
      // The device answers before it restarts, so no answer means it did not get this.
      notify(t("system.noAnswer"), "danger");
    } finally {
      setSending(false);
    }
  };

  const trigger = (
    <Button className="flex-1" variant={variant} onPress={() => setOpen(true)}>
      {label}
    </Button>
  );

  const confirm = (
    <Button variant={danger ? "danger" : "primary"} isPending={sending} onPress={run}>
      {label}
    </Button>
  );

  return (
    <ConfirmDialog
      trigger={trigger}
      isOpen={open}
      onOpenChange={setOpen}
      status={danger ? "danger" : "warning"}
      title={title}
      text={text}
      cancelLabel={t("system.cancel")}
      confirm={confirm}
    />
  );
}
