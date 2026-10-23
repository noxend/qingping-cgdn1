import { NetworkCard } from "./NetworkCard";
import { BareLayout } from "../../components/BareLayout";
import { PageTitle } from "../../components/PageTitle";
import { useT } from "../../i18n";

// A new device, or one after a factory reset: its Wi-Fi and the password to sign in with.
export function Setup({ status, joining, setJoining }) {
  const t = useT();

  return (
    <BareLayout>
      <PageTitle title={t("setup.title")} />
      <NetworkCard status={status} joining={joining} setJoining={setJoining} />
    </BareLayout>
  );
}
