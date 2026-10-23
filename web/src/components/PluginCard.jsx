import { navigate } from "wouter/use-hash-location";

import { PluginMedia } from "./PluginMedia";
import { useT } from "../i18n";

// `action` goes beside the name: a button or a chip, as the page needs.
export function PluginCard({ plugin, action }) {
  const t = useT();
  const open = () => navigate("/plugin/" + plugin.id);

  return (
    <div className="group flex flex-col">
      <button
        onClick={open}
        className="block rounded-xl text-left transition-opacity hover:opacity-90"
      >
        <PluginMedia plugin={plugin} />
      </button>

      <div className="mt-4 flex items-center justify-between gap-3">
        <div className="min-w-0">
          <button onClick={open} className="block truncate text-left font-semibold hover:underline">
            {plugin.name}
          </button>
          <p className="mt-0.5 line-clamp-2 text-sm leading-snug text-muted">
            {plugin.description}
          </p>
          <p className="mt-1.5 text-xs text-muted">{t("category." + plugin.category)}</p>
        </div>

        {action}
      </div>
    </div>
  );
}
