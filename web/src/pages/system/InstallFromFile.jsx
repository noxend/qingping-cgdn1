import { useRef, useState } from "react";
import { Button } from "@heroui/react";
import { useSWRConfig } from "swr";

import { devicePost } from "./devicePost";
import { Fold } from "./Fold";
import { ActionTip } from "../../components/ActionTip";
import { pausePolling } from "../../lib/api";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

// A .bin from a build, for whoever builds the firmware: folded away from the owner's path.
export function InstallFromFile({ busy }) {
  const t = useT();

  // Every key at once: a key that failed before the upload is not polled again by SWR
  // until it is revalidated (its retry fell inside the pause).
  const { mutate: reloadAll } = useSWRConfig();

  const input = useRef(null);
  const [file, setFile] = useState(null);
  const [uploading, setUploading] = useState(false);

  const upload = async () => {
    setUploading(true);
    pausePolling(true);

    try {
      const firmware = await file.arrayBuffer();
      if (await devicePost(t, "/ota", firmware, t("system.uploadFailed"))) {
        notify(t("system.uploadedRebooting"));
      }
    } catch {
      // As with the network save, the reply can be lost to the reboot.
      notify(t("system.uploadedRebooting"));
    } finally {
      pausePolling(false);
      reloadAll(() => true);
      setUploading(false);
    }
  };

  return (
    <Fold label={t("system.fromFile")} variant="ghost" bodyClassName="flex flex-col gap-3 pt-3">
      <input
        ref={input}
        type="file"
        accept=".bin"
        hidden
        onChange={(e) => setFile(e.target.files?.[0] || null)}
      />

      <Button fullWidth variant="secondary" onPress={() => input.current?.click()}>
        {file ? file.name : t("system.chooseBin")}
      </Button>

      <ActionTip hint={t("system.uploadHint")}>
        <Button
          fullWidth
          variant="secondary"
          isDisabled={!file || busy}
          isPending={uploading}
          onPress={upload}
        >
          {t("system.upload")}
        </Button>
      </ActionTip>
    </Fold>
  );
}
