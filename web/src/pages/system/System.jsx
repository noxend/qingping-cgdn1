import { useState } from "react";

import { DeviceCard } from "./DeviceCard";
import { GeneralCard } from "./GeneralCard";
import { MemoryCard } from "./MemoryCard";
import { NetworkCard } from "./NetworkCard";
import { UpdateCard } from "./UpdateCard";
import { PageTitle } from "../../components/PageTitle";
import { useT } from "../../i18n";

export function System({ status, plugins }) {
  const t = useT();
  const [joining, setJoining] = useState(null);

  return (
    <>
      <PageTitle title={t("system.title")} />

      <div className="grid grid-cols-1 items-start gap-6 lg:grid-cols-2">
        <div className="flex flex-col gap-6">
          <NetworkCard status={status} joining={joining} setJoining={setJoining} />
          <GeneralCard />
        </div>

        <div className="flex flex-col gap-6">
          <UpdateCard status={status} />
          {plugins && <MemoryCard plugins={plugins} />}
        </div>

        <DeviceCard status={status} />
      </div>
    </>
  );
}
