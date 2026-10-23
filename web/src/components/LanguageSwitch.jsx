import { ToggleButton, ToggleButtonGroup } from "@heroui/react";

import { LANGS, useLang } from "../i18n";

// The language, not the country: "UA" would name Ukraine.
const LABELS = { uk: "УКР", en: "EN" };

// The default look marks the selected language too faintly on the dark theme; this is
// the docs' custom-styles pattern for ToggleButtonGroup.
const LANG_BUTTON =
  "text-muted data-[selected=true]:bg-accent data-[selected=true]:text-accent-foreground";

export function LanguageSwitch() {
  const { lang, setLang, t } = useLang();

  return (
    <ToggleButtonGroup
      aria-label={t("header.language")}
      className="shrink-0"
      disallowEmptySelection
      selectionMode="single"
      selectedKeys={[lang]}
      onSelectionChange={(keys) => setLang([...keys][0])}
    >
      {LANGS.map((l, i) => (
        <ToggleButton key={l} id={l} className={LANG_BUTTON}>
          {i > 0 && <ToggleButtonGroup.Separator />}
          {LABELS[l]}
        </ToggleButton>
      ))}
    </ToggleButtonGroup>
  );
}
