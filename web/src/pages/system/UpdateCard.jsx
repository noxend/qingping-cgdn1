import { useState } from "react";
import { Button, Label, ProgressBar } from "@heroui/react";

import { devicePost } from "./devicePost";
import { InstallFromFile } from "./InstallFromFile";
import { ActionTip } from "../../components/ActionTip";
import { Notice } from "../../components/Notice";
import { SwitchField } from "../../components/SwitchField";
import { TitledCard } from "../../components/TitledCard";
import { accepted } from "../../lib/answer";
import { request, useStatus } from "../../lib/api";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

// The states of an update the device is installing.
const RUNNING = ["queued", "downloading", "verifying", "rebooting"];

const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

function OfferedUpdate({ status, onChange }) {
  const t = useT();

  const { ota, update } = status;
  const running = RUNNING.includes(ota?.state);
  const percent = ota?.total ? Math.round((100 * ota.done) / ota.total) : 0;

  const install = async () => {
    try {
      if (await devicePost(t, "/update", undefined, t("system.updateFailed"))) {
        notify(t("system.updateStarted"));
        onChange();
      }
    } catch {
      notify(t("system.updateFailed"), "danger");
    }
  };

  return (
    <div className="flex flex-col gap-3">
      <div>
        <p className="text-sm font-medium">
          {t("system.updateOffered", { version: update.version })}
        </p>
        {update.notes && <p className="text-sm text-muted">{update.notes}</p>}
      </div>

      {running && (
        <ProgressBar aria-label={t("system.updateProgress")} value={percent}>
          <Label>{t(`system.ota.${ota.state}`)}</Label>
          <ProgressBar.Output />
          <ProgressBar.Track>
            <ProgressBar.Fill />
          </ProgressBar.Track>
        </ProgressBar>
      )}

      {ota?.state === "failed" && (
        <div className="text-sm">
          <p className="text-danger">{t("system.updateError")}</p>
          <p className="text-xs text-muted">{ota.err}</p>
        </div>
      )}

      <ActionTip hint={t("system.updateHint")}>
        <Button fullWidth variant="primary" isPending={running} onPress={install}>
          {t("system.updateTo", { version: update.version })}
        </Button>
      </ActionTip>
    </div>
  );
}

// The device fetches the release list now, and /status says when it has the answer.
function CheckButton({ reloadStatus }) {
  const t = useT();
  const [checking, setChecking] = useState(false);

  const waitForAnswer = async () => {
    let status;
    for (let i = 0; i < 10; i++) {
      await sleep(1500);
      status = await reloadStatus();
      if (!status?.checking) {
        break;
      }
    }
    return status;
  };

  const check = async () => {
    setChecking(true);

    try {
      const response = await request("/update/check", { method: "POST" });
      if (!(await accepted(t, response, t("system.checkFailed")))) {
        return;
      }

      const status = await waitForAnswer();
      if (status?.check_failed) {
        notify(t("system.checkFailed"), "danger");
      } else if (status && !status.checking && !status.update) {
        notify(t("system.noUpdate"));
      }
    } catch {
      notify(t("system.checkFailed"), "danger");
    } finally {
      setChecking(false);
    }
  };

  return (
    <Button fullWidth variant="secondary" isPending={checking} onPress={check}>
      {t("system.check")}
    </Button>
  );
}

export function UpdateCard({ status }) {
  const t = useT();
  const [autoBusy, setAutoBusy] = useState(false);

  // The same SWR key as the page's poll, so a revalidation shows a change at once.
  const { mutate: reloadStatus } = useStatus();

  const setAuto = async (on) => {
    setAutoBusy(true);

    try {
      await devicePost(t, "/update/auto", on ? "on=1" : "on=0", t("toast.saveFailed"));
      await reloadStatus();
    } catch {
      notify(t("toast.saveFailed"), "danger");
    } finally {
      setAutoBusy(false);
    }
  };

  const known = status !== undefined;
  const summary = status?.update
    ? t("system.updateAvailable")
    : t("system.upToDate", { version: status?.fw });

  return (
    <TitledCard title={t("system.update")} description={known && summary}>
      <div className="flex flex-col gap-4">
        {status?.rolled_back && (
          <Notice
            status="warning"
            title={t("system.rolledBack", { version: status.rolled_back, fw: status.fw })}
          />
        )}

        {known &&
          (status.update ? (
            <OfferedUpdate status={status} onChange={reloadStatus} />
          ) : (
            <CheckButton reloadStatus={reloadStatus} />
          ))}

        {known && (
          <SwitchField
            label={t("system.autoUpdate")}
            description={t("system.autoUpdateHint")}
            isSelected={status.auto_update}
            isDisabled={autoBusy}
            onChange={setAuto}
          />
        )}

        <InstallFromFile busy={RUNNING.includes(status?.ota?.state)} />
      </div>
    </TitledCard>
  );
}
