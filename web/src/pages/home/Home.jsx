import { Button, Card } from "@heroui/react";
import { navigate } from "wouter/use-hash-location";

import { Banner } from "./Banner";
import { AlertStatus } from "../../components/AlertStatus";
import { DeviceScreen } from "../../components/DeviceScreen";
import { PageTitle } from "../../components/PageTitle";
import { PluginCard } from "../../components/PluginCard";
import { PluginGrid } from "../../components/PluginGrid";
import { PluginMedia } from "../../components/PluginMedia";
import { SectionHeader } from "../../components/SectionHeader";
import { StatusChip } from "../../components/StatusChip";
import { UploadPlugin } from "../../components/UploadPlugin";
import { usePluginScreen } from "../../lib/api";
import { useT } from "../../i18n";

// "On screen now", with the device's LED beside it while it is lit.
function Kicker({ plugin }) {
  const t = useT();
  const { data } = usePluginScreen(plugin.installed ? plugin.id : null);

  const led = data?.led;
  const lit = led && led !== "off";

  return (
    <span className="inline-flex items-center gap-2">
      {lit && <span className="size-2 rounded-full" style={{ background: led }} />}
      {t("home.onScreen")}
    </span>
  );
}

function OnScreenNow({ plugin, live }) {
  const t = useT();

  const actions = (
    <>
      <Button variant="primary" onPress={() => navigate("/plugin/" + plugin.id)}>
        {t("home.configure")}
      </Button>
      <Button variant="secondary" onPress={() => navigate("/store")}>
        {t("home.store")}
      </Button>
    </>
  );

  return (
    <Banner
      kicker={<Kicker plugin={plugin} />}
      title={plugin.name}
      media={<PluginMedia plugin={plugin} />}
      actions={actions}
    >
      {plugin.id === "alerts" ? (
        <AlertStatus live={live} size="lg" />
      ) : (
        <p className="text-muted">{plugin.description}</p>
      )}
    </Banner>
  );
}

function NothingOnScreens({ store }) {
  const t = useT();

  return (
    <Card className="text-center">
      <Card.Content className="py-6">
        <DeviceScreen className="mx-auto mb-5 w-36" />
        <p className="text-muted">{t("home.empty")}</p>
        <div className="mt-4 flex flex-wrap justify-center gap-3">
          <Button variant="secondary" onPress={() => navigate("/store")}>
            {t("home.goStore")}
          </Button>
          <UploadPlugin store={store} />
        </div>
      </Card.Content>
    </Card>
  );
}

// The plugins on the screens: a banner for the one shown now, then a grid of them all.
export function Home({ catalog, screens, live, store }) {
  const t = useT();

  const byId = Object.fromEntries(catalog.map((s) => [s.id, s]));
  const mine = screens.map((id) => byId[id]).filter(Boolean);
  const now = mine.find((s) => s.active);

  const switchedOff = <StatusChip color="danger">{t("plugin.switchedOff")}</StatusChip>;

  return (
    <>
      <PageTitle title={t("home.title")} />

      {now && <OnScreenNow plugin={now} live={live} />}

      <div className="mt-12">
        <SectionHeader title={t("home.onScreens")} count={mine.length} />

        {mine.length === 0 ? (
          <NothingOnScreens store={store} />
        ) : (
          <PluginGrid>
            {mine.map((s) => (
              <PluginCard key={s.id} plugin={s} action={s.disabled && switchedOff} />
            ))}
          </PluginGrid>
        )}
      </div>
    </>
  );
}
