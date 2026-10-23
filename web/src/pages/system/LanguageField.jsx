import { SelectField } from "../../components/SelectField";
import { useLang } from "../../i18n";

// Not translated: each language under its own name.
const OPTIONS = [
  { key: "en", label: "English" },
  { key: "uk", label: "Українська" },
];

export function LanguageField() {
  const { lang, setLang, t } = useLang();

  return (
    <SelectField label={t("system.language")} options={OPTIONS} value={lang} onChange={setLang} />
  );
}
