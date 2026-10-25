import { Button, Skeleton } from "@heroui/react";

import { Notice } from "../components/Notice";
import { useT } from "../i18n";

// A placeholder for the page until the device answers, or the failure with a way to
// try again.
export function Loading({ failed }) {
  const t = useT();

  if (failed) {
    return (
      <Notice status="danger" title={t("app.offline.title")} description={t("app.offline.text")}>
        <Button className="mt-3" size="sm" variant="secondary" onPress={() => location.reload()}>
          {t("app.offline.retry")}
        </Button>
      </Notice>
    );
  }

  return (
    <div className="flex flex-col gap-4" aria-busy="true">
      <Skeleton className="h-9 w-56 rounded-md" />
      <Skeleton className="h-4 w-80 max-w-full rounded-md" />
      <Skeleton className="mt-6 h-72 rounded-[0.875rem]" />
    </div>
  );
}
