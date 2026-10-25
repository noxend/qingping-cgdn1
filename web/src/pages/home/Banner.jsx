import { Card } from "@heroui/react";

const GRID_CLASS = [
  "grid items-center gap-8 p-7 sm:p-10",
  "lg:grid-cols-[1fr_1.15fr] lg:gap-12 lg:p-12",
].join(" ");

// The plugin on the screen now: its name, a short status or description, its preview
// and the primary actions.
export function Banner({ kicker, title, children, actions, media }) {
  return (
    <Card className="overflow-hidden p-0">
      <div className={GRID_CLASS}>
        <div className="min-w-0">
          {kicker && <div className="text-sm text-muted">{kicker}</div>}
          <h2 className="mt-2 text-[1.75rem] font-semibold tracking-tight lg:text-[2rem]">
            {title}
          </h2>
          <div className="mt-4">{children}</div>
          {actions && <div className="mt-7 flex flex-wrap gap-3">{actions}</div>}
        </div>

        <div>{media}</div>
      </div>
    </Card>
  );
}
