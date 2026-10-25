import { useRef, useState } from "react";
import { Button, ProgressBar, toast } from "@heroui/react";
import { useSWRConfig } from "swr";

import { Fold } from "./Fold";
import { ActionTip } from "../../components/ActionTip";
import { accepted } from "../../lib/answer";
import { pausePolling, TIMEOUT_MS, upload } from "../../lib/api";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

// A loading toast while the firmware goes up, its bar showing the share sent.
function uploadToast(t) {
  const title = t("system.uploading");
  const id = toast(title, { isLoading: true, timeout: 0 });

  const show = (share) => {
    const bar = (
      <ProgressBar aria-label={title} value={Math.round(100 * share)} className="mt-1 w-48">
        <ProgressBar.Track>
          <ProgressBar.Fill />
        </ProgressBar.Track>
      </ProgressBar>
    );

    toast.update(id, title, { isLoading: true, description: bar });
  };

  return { show, close: () => toast.close(id) };
}

// A .bin from a build, for whoever builds the firmware: folded away from the owner's path.
export function InstallFromFile({ busy }) {
  const t = useT();

  // Every key at once: a key that failed before the upload is not polled again by SWR
  // until it is revalidated (its retry fell inside the pause).
  const { mutate: reloadAll } = useSWRConfig();

  const input = useRef(null);
  const [file, setFile] = useState(null);
  const [uploading, setUploading] = useState(false);

  const send = async () => {
    setUploading(true);
    pausePolling(true);
    const progress = uploadToast(t);
    progress.show(0);

    try {
      const firmware = await file.arrayBuffer();
      const response = await upload("/ota", firmware, {
        timeoutMs: TIMEOUT_MS.firmware,
        onProgress: progress.show,
      });

      if (await accepted(t, response, t("system.uploadFailed"))) {
        notify(t("system.uploadedRebooting"));
      }
    } catch {
      // As with the network save, the reply can be lost to the reboot.
      notify(t("system.uploadedRebooting"));
    } finally {
      progress.close();
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
          onPress={send}
        >
          {t("system.upload")}
        </Button>
      </ActionTip>
    </Fold>
  );
}
