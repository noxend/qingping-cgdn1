import { NightCard } from "./NightCard";
import { NightState } from "./NightState";
import { PageTitle } from "../../components/PageTitle";
import { useT } from "../../i18n";

export function Display({ night, savedNight, setNight, onSaveNight, saving }) {
  const t = useT();

  return (
    <>
      <PageTitle title={t("display.title")} sub={<NightState saved={savedNight} />} />

      <div className="grid grid-cols-1 items-start gap-6 lg:grid-cols-2">
        <NightCard night={night} setNight={setNight} onSaveNight={onSaveNight} saving={saving} />
      </div>
    </>
  );
}
