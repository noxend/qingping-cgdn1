import { cn } from "../lib/cn";
import { useT } from "../i18n";

const LEVELS = {
  none: { word: "alerts.calm", color: "#34C77B" },
  yellow: { word: "alerts.alert", color: "#F0B93E" },
  red: { word: "alerts.alert", color: "#EF5350" },
};

// The live air-raid state for the chosen region: a coloured dot and word, plus the
// country-wide count.
export function AlertStatus({ live, size = "md" }) {
  const t = useT();

  const level = LEVELS[live?.level] || LEVELS.none;
  const dot = {
    background: level.color,
    boxShadow: `0 0 10px ${level.color}`,
  };

  return (
    <div>
      <p className="text-sm text-muted">{live?.region || t("alerts.noRegion")}</p>

      <div className="mt-1.5 flex items-center gap-2.5">
        <span className="h-2.5 w-2.5 rounded-full" style={dot} />
        <span
          className={cn("font-semibold tracking-tight", size === "lg" ? "text-3xl" : "text-xl")}
          style={{ color: level.color }}
        >
          {t(level.word)}
        </span>
      </div>

      <p className="mt-1 text-sm text-muted tnum">
        {live ? t("alerts.count", { n: live.alerts }) : t("alerts.loading")}
      </p>
    </div>
  );
}
