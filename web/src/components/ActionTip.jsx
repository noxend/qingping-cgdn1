import { CircleInfo } from "@gravity-ui/icons";
import { Button, Popover } from "@heroui/react";

import { useT } from "../i18n";

// What an action will do, behind an info button beside it: a tap opens it, so phones
// see it too. With no hint the child renders untouched.
export function ActionTip({ hint, children }) {
  const t = useT();

  if (!hint) {
    return children;
  }

  return (
    <div className="flex items-center gap-2">
      <div className="min-w-0 flex-1">{children}</div>

      <Popover>
        <Button isIconOnly variant="ghost" aria-label={t("tip.what")}>
          <CircleInfo />
        </Button>
        <Popover.Content className="max-w-64">
          <Popover.Dialog aria-label={t("tip.what")}>
            <p className="text-sm">{hint}</p>
          </Popover.Dialog>
        </Popover.Content>
      </Popover>
    </div>
  );
}
