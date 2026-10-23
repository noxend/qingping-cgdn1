import { useState } from "react";
import { Button, Label, Slider } from "@heroui/react";

import { CodeBlock } from "./CodeBlock";
import { useDevAction, useTouchStats } from "./devApi";
import { TitledCard } from "../../components/TitledCard";
import { postJson, useConfig } from "../../lib/api";
import { localize } from "../../lib/manifest";
import { notify } from "../../lib/notify";
import { useLang } from "../../i18n";

export function DevTouch() {
  const { t, lang } = useLang();
  const run = useDevAction();

  const { data: stats } = useTouchStats();
  const { data: config } = useConfig();
  const screens = (config?.catalog ?? [])
    .map((entry) => localize(entry, lang))
    .filter((s) => s.installed);

  const [range, setRange] = useState([60, 200]);
  const [trace, setTrace] = useState("");

  const swipe = () => {
    const [from, to] = range;
    run(t("dev.touch.swipe"), `/touch?inject=slide&from=${from}&to=${to}`);
  };

  const loadTrace = async () => {
    const text = await run(t("dev.touch.log"), "/touch");
    if (text !== null) {
      setTrace(text.split("\n").slice(-60).join("\n"));
    }
  };

  const show = async (id) => {
    try {
      const response = await postJson("/config", { active: id });
      if (response.ok) {
        notify(t("dev.touch.switched"));
      } else {
        notify(t("toast.switchFailed"), "danger");
      }
    } catch {
      notify(t("dev.touch.deviceNoAnswer"), "danger");
    }
  };

  return (
    <TitledCard title={t("dev.touch.title")}>
      <div className="flex flex-col gap-5">
        <p className="font-mono text-xs text-muted">{stats || "…"}</p>

        <Slider minValue={0} maxValue={255} step={1} value={range} onChange={setRange}>
          <div className="flex items-center justify-between">
            <Label>{t("dev.touch.range")}</Label>
            <Slider.Output />
          </div>
          <Slider.Track>
            <Slider.Fill />
            <Slider.Thumb index={0} />
            <Slider.Thumb index={1} />
          </Slider.Track>
        </Slider>

        <div className="flex flex-wrap gap-2">
          <Button variant="secondary" onPress={swipe}>
            {t("dev.touch.playSwipe")}
          </Button>
          <Button
            variant="secondary"
            onPress={() => run(t("dev.touch.hold"), "/touch?inject=hold")}
          >
            {t("dev.touch.hold")}
          </Button>
          <Button variant="secondary" onPress={loadTrace}>
            {t("dev.touch.showLog")}
          </Button>
        </div>

        <div className="flex flex-wrap gap-2">
          {screens.map((s) => (
            <Button key={s.id} size="sm" variant="secondary" onPress={() => show(s.id)}>
              {s.name}
            </Button>
          ))}
        </div>

        {trace && <CodeBlock className="max-h-64 overflow-auto">{trace}</CodeBlock>}

        <p className="text-sm text-muted">{t("dev.touch.hint")}</p>
      </div>
    </TitledCard>
  );
}
