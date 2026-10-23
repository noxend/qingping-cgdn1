import { DevDanger } from "./DevDanger";
import { DevDiagnostics } from "./DevDiagnostics";
import { DevDisplay } from "./DevDisplay";
import { DevLed } from "./DevLed";
import { DevPins } from "./DevPins";
import { DevTouch } from "./DevTouch";
import { useDevStatus } from "./devApi";
import { Notice } from "../../components/Notice";
import { PageTitle } from "../../components/PageTitle";
import { useT } from "../../i18n";

// Shown whenever /status says "dev". The diagnostics come from /status itself; the
// display, LED, touch, pin and crash controls need the test endpoints, which only an
// image built with test_hooks has and /status then reports as "test_hooks"
// (firmware/README.md, "Test builds").
export function Developer() {
  const t = useT();
  const { data: status } = useDevStatus();

  const tools = (
    <>
      <DevDisplay />
      <DevLed />
      <DevTouch />
      <DevPins />
      <DevDanger />
    </>
  );

  const noTools = (
    <Notice
      className="lg:col-span-2"
      title={t("dev.noHooks.title")}
      description={t("dev.noHooks.text")}
    />
  );

  return (
    <>
      <PageTitle title={t("dev.title")} />

      <div className="grid gap-6 lg:grid-cols-2">
        <DevDiagnostics status={status} />
        {status && (status.test_hooks ? tools : noTools)}
      </div>
    </>
  );
}
