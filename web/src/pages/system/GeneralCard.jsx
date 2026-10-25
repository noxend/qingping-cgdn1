import { LanguageField } from "./LanguageField";
import { TemperatureField } from "./TemperatureField";
import { TimeZoneField } from "./TimeZoneField";
import { TitledCard } from "../../components/TitledCard";
import { useT } from "../../i18n";

// How the page and the device present things: the page's language, the device's clock
// and its temperature correction.
export function GeneralCard() {
  const t = useT();

  return (
    <TitledCard title={t("system.general")}>
      <div className="flex flex-col gap-6">
        <LanguageField />
        <TimeZoneField />
        <TemperatureField />
      </div>
    </TitledCard>
  );
}
