import { Redirect, Route, Switch } from "wouter";

import { Developer } from "../pages/dev/Developer";
import { Display } from "../pages/display/Display";
import { Home } from "../pages/home/Home";
import { PluginPage } from "../pages/plugin/PluginPage";
import { Store } from "../pages/Store";
import { System } from "../pages/system/System";

export function Routes({ device, store, query }) {
  const storeProps = {
    catalog: device.catalog,
    screens: device.screens,
    onAdd: device.onAdd,
    isBusy: device.isBusy,
    listChanging: device.listChanging,
    store,
    query,
  };

  return (
    <Switch>
      <Route path="/plugin/:id">
        {(params) => (
          <PluginPage
            id={params.id}
            catalog={device.catalog}
            screens={device.screens}
            live={device.live}
            vals={device.vals}
            setVal={device.setVal}
            onAdd={device.onAdd}
            onRemove={device.onRemove}
            onSave={device.onSave}
            onSetActive={device.onSetActive}
            isBusy={device.isBusy}
            listChanging={device.listChanging}
            store={store}
          />
        )}
      </Route>

      <Route path="/category/:cat">
        {(params) => <Store {...storeProps} category={decodeURIComponent(params.cat)} />}
      </Route>

      <Route path="/store">
        <Store {...storeProps} />
      </Route>

      <Route path="/display">
        <Display
          night={device.night}
          savedNight={device.savedNight}
          setNight={device.setNight}
          onSaveNight={device.onSaveNight}
          saving={device.isBusy("night")}
        />
      </Route>

      <Route path="/system">
        <System status={device.status} plugins={store.device} />
      </Route>

      <Route path="/settings">
        <Redirect to="/system" replace />
      </Route>

      <Route path="/dev">
        {device.status?.dev ? <Developer /> : device.status && <Redirect to="/" replace />}
      </Route>

      <Route>
        <Home catalog={device.catalog} screens={device.screens} live={device.live} store={store} />
      </Route>
    </Switch>
  );
}
