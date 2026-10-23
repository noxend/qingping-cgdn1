import { useEffect, useRef, useState } from "react";
import { Drawer, Toast } from "@heroui/react";
import { useLocation } from "wouter";

import { Header } from "./Header";
import { Loading } from "./Loading";
import { Routes } from "./Routes";
import { Sidebar } from "./Sidebar";
import { useDeviceConfig } from "../hooks/useDeviceConfig";
import { usePluginStore } from "../hooks/usePluginStore";
import { notify } from "../lib/notify";
import { useT } from "../i18n";

const SIDEBAR_CLASS = [
  "sticky top-0 hidden h-screen w-64 shrink-0 px-3 py-5 lg:block",
  "border-r border-separator bg-sidebar",
].join(" ");

// A new plugin goes onto the screens; an update leaves them as the user set them. A
// deleted plugin leaves the screens, and its page gives way to the store, which offers
// a deleted standard plugin again.
function useStoreFollowingDevice(device, location, navigate) {
  const t = useT();

  const onInstalled = (id, update) => {
    if (update) {
      device.reload();
      notify(t("toast.updated"));
    } else if (device.screens.includes(id)) {
      device.reload();
      notify(t("toast.installed"));
    } else {
      device.onAdd(id, t("toast.installed"));
    }
  };

  const onUninstalled = (id) => {
    if (location === "/plugin/" + id) {
      navigate("/store");
    }

    if (device.screens.includes(id)) {
      device.onRemove(id, t("toast.deleted"));
    } else {
      device.reload();
      notify(t("toast.deleted"));
    }
  };

  return usePluginStore({ onInstalled, onUninstalled });
}

function useSearchShortcut(searchRef) {
  useEffect(() => {
    const onKey = (e) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "k") {
        e.preventDefault();
        searchRef.current?.focus();
      }
    };

    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [searchRef]);
}

export function SignedIn() {
  const [location, navigate] = useLocation();
  const device = useDeviceConfig();
  const store = useStoreFollowingDevice(device, location, navigate);

  const [query, setQuery] = useState("");
  const [menuOpen, setMenuOpen] = useState(false);
  const searchRef = useRef(null);

  useSearchShortcut(searchRef);

  useEffect(() => {
    window.scrollTo(0, 0);
  }, [location]);

  const onSearch = (value) => {
    setQuery(value);

    const inStore = location.startsWith("/store") || location.startsWith("/category/");
    if (value && !inStore) {
      navigate("/store");
    }
  };

  const nav = {
    path: location,
    catalog: device.catalog,
    screens: device.screens,
    status: device.status,
  };

  return (
    <div className="flex min-h-screen">
      <aside className={SIDEBAR_CLASS}>
        <Sidebar {...nav} />
      </aside>

      <Toast.Provider placement="bottom end" />

      <Drawer>
        <Drawer.Backdrop isOpen={menuOpen} onOpenChange={setMenuOpen}>
          <Drawer.Content placement="left">
            <Drawer.Dialog className="bg-sidebar">
              <Drawer.CloseTrigger />
              <Drawer.Body className="px-3 py-5">
                <Sidebar {...nav} onPick={() => setMenuOpen(false)} />
              </Drawer.Body>
            </Drawer.Dialog>
          </Drawer.Content>
        </Drawer.Backdrop>
      </Drawer>

      <div className="min-w-0 flex-1">
        <Header
          query={query}
          onSearch={onSearch}
          onMenu={() => setMenuOpen(true)}
          searchRef={searchRef}
          update={device.status?.update}
        />

        <main className="mx-auto max-w-6xl px-5 py-8 sm:px-8 sm:py-10">
          {device.loaded ? (
            <Routes device={device} store={store} query={query} />
          ) : (
            <Loading failed={Boolean(device.configError)} />
          )}
        </main>
      </div>
    </div>
  );
}
