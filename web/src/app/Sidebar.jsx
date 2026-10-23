import { Code, Display, Gear, LayoutCells, ShoppingBag, Tag } from "@gravity-ui/icons";
import { Link } from "@heroui/react";

import { StatusChip } from "../components/StatusChip";
import { cn } from "../lib/cn";
import { useT } from "../i18n";

function NavItem({ icon: Icon, label, to, count, path, onPick }) {
  const active = path === to;

  return (
    <Link
      href={"#" + to}
      aria-current={active ? "page" : undefined}
      onPress={onPick}
      className={cn(
        "flex w-full items-center gap-3 rounded-lg px-3 py-2 text-sm no-underline transition-colors",
        active ? "bg-default text-foreground" : "text-muted hover:bg-surface hover:text-foreground",
      )}
    >
      <Icon className="size-4 shrink-0" />
      <span className="flex-1 truncate">{label}</span>
      {count !== undefined && <span className="font-mono text-xs text-muted tnum">{count}</span>}
    </Link>
  );
}

function countByCategory(catalog) {
  const counts = {};

  for (const s of catalog) {
    if (s.category) {
      counts[s.category] = (counts[s.category] || 0) + 1;
    }
  }

  return Object.entries(counts);
}

function DeviceStatus({ status }) {
  const t = useT();
  const connected = status.wifi === "connected";

  return (
    <div className="mt-auto rounded-lg bg-surface px-3 py-3 ring-1 ring-border">
      <StatusChip color={connected ? "success" : "warning"}>
        {connected ? t("status.connected") : t("status.setupAp")}
      </StatusChip>
      <p className="mt-1.5 font-mono text-xs text-muted">
        {t("system.version", { version: status.fw })}
      </p>
    </div>
  );
}

// The navigation column, shared by the desktop sidebar and the mobile drawer.
export function Sidebar({ path, catalog, screens, status, onPick }) {
  const t = useT();

  const categories = countByCategory(catalog);
  const item = { path, onPick };

  return (
    <div className="flex h-full flex-col">
      <Link href="#/" className="mb-7 block px-3 no-underline" onPress={onPick}>
        <p className="font-mono font-semibold tracking-[0.04em]">CGDN1</p>
        <p className="text-xs text-muted">Qingping Air Monitor</p>
      </Link>

      <div className="flex flex-col gap-0.5">
        <NavItem {...item} icon={LayoutCells} label={t("nav.mine")} to="/" count={screens.length} />
        <NavItem {...item} icon={ShoppingBag} label={t("nav.store")} to="/store" />
        <NavItem {...item} icon={Display} label={t("nav.display")} to="/display" />
      </div>

      {categories.length > 0 && (
        <>
          <p className="mb-1.5 mt-7 px-3 text-xs text-muted">{t("nav.categories")}</p>
          <div className="flex flex-col gap-0.5">
            {categories.map(([category, count]) => (
              <NavItem
                {...item}
                key={category}
                icon={Tag}
                label={t("category." + category)}
                to={"/category/" + encodeURIComponent(category)}
                count={count}
              />
            ))}
          </div>
        </>
      )}

      <div className="mt-7 flex flex-col gap-0.5">
        <NavItem {...item} icon={Gear} label={t("nav.system")} to="/system" />
        {status?.dev && <NavItem {...item} icon={Code} label={t("nav.dev")} to="/dev" />}
      </div>

      {status && <DeviceStatus status={status} />}
    </div>
  );
}
