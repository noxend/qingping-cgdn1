import { Button, Card, Fieldset, Form, Radio, RadioGroup } from "@heroui/react";

import { NightTimeField, PercentSlider } from "./NightFields";
import { toTime } from "./nightTime";
import { SwitchField } from "../../components/SwitchField";
import { useT } from "../../i18n";

function ScreenChoice({ value, label }) {
  return (
    <Radio value={value}>
      <Radio.Content>
        <Radio.Control>
          <Radio.Indicator />
        </Radio.Control>
        {label}
      </Radio.Content>
    </Radio>
  );
}

// Off or dimmed is one choice at night; the slider belongs to "dim" only.
function ScreenAtNight({ night, setNight, isDisabled }) {
  const t = useT();
  const choice = night.screen_off ? "off" : "dim";

  return (
    <>
      <RadioGroup
        aria-label={t("display.screen")}
        isDisabled={isDisabled}
        value={choice}
        variant="secondary"
        onChange={(value) => setNight("screen_off", value === "off")}
      >
        <ScreenChoice value="dim" label={t("display.screenDim")} />
        <ScreenChoice value="off" label={t("display.screenOff")} />
      </RadioGroup>

      {choice === "dim" && (
        <PercentSlider
          isDisabled={isDisabled}
          label={t("display.nightScreen")}
          minValue={10}
          value={night.screen_pct}
          onChange={(value) => setNight("screen_pct", value)}
        />
      )}
    </>
  );
}

export function NightCard({ night, setNight, onSaveNight, saving }) {
  const t = useT();

  const off = !night.enabled;
  // A cleared time segment leaves the field empty; nothing is sent until it is whole.
  const timesWhole = Boolean(toTime(night.start) && toTime(night.end));

  const onSubmit = (e) => {
    e.preventDefault();
    onSaveNight();
  };

  return (
    <Card>
      <Card.Content>
        <Form className="flex flex-col gap-6" onSubmit={onSubmit}>
          <SwitchField
            label={t("display.night")}
            isSelected={night.enabled}
            onChange={(on) => setNight("enabled", on)}
          />

          <Fieldset>
            <Fieldset.Legend>{t("display.schedule")}</Fieldset.Legend>
            <Fieldset.Group>
              <NightTimeField
                isDisabled={off}
                label={t("display.nightFrom")}
                value={night.start}
                onChange={(value) => setNight("start", value)}
              />
              <NightTimeField
                isDisabled={off}
                label={t("display.nightUntil")}
                value={night.end}
                onChange={(value) => setNight("end", value)}
              />
            </Fieldset.Group>
          </Fieldset>

          <Fieldset>
            <Fieldset.Legend>{t("display.atNight")}</Fieldset.Legend>
            <Fieldset.Group>
              <ScreenAtNight night={night} setNight={setNight} isDisabled={off} />
              <PercentSlider
                isDisabled={off}
                label={t("display.nightLed")}
                minValue={0}
                value={night.led_pct}
                onChange={(value) => setNight("led_pct", value)}
              />
            </Fieldset.Group>
          </Fieldset>

          <Button
            fullWidth
            isDisabled={!timesWhole}
            isPending={saving}
            type="submit"
            variant="primary"
          >
            {t("display.nightSave")}
          </Button>
        </Form>
      </Card.Content>
    </Card>
  );
}
