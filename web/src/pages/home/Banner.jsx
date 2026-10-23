const FRAME_CLASS = [
  "overflow-hidden rounded-[20px] ring-1 ring-border",
  "bg-gradient-to-br from-surface to-background",
].join(" ");

const GRID_CLASS = [
  "grid items-center gap-8 p-7 sm:p-10",
  "lg:grid-cols-[1fr_1.15fr] lg:gap-12 lg:p-12",
].join(" ");

// The plugin on the screen now: its name, a short status or description, its preview
// and the primary actions.
export function Banner({ kicker, title, children, actions, media }) {
  return (
    <div className={FRAME_CLASS}>
      <div className={GRID_CLASS}>
        <div className="min-w-0">
          {kicker && <p className="text-sm text-muted">{kicker}</p>}
          <h2 className="mt-2 text-3xl font-bold tracking-tight lg:text-4xl">{title}</h2>
          <div className="mt-4">{children}</div>
          {actions && <div className="mt-7 flex flex-wrap gap-3">{actions}</div>}
        </div>

        <div>{media}</div>
      </div>
    </div>
  );
}
