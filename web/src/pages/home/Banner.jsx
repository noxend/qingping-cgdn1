const FRAME_CLASS = "overflow-hidden rounded-[0.875rem] bg-surface ring-1 ring-border";

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
          {kicker && <div className="text-sm text-muted">{kicker}</div>}
          <h2 className="mt-2 text-[1.75rem] font-semibold tracking-[-0.02em] lg:text-[2rem]">
            {title}
          </h2>
          <div className="mt-4">{children}</div>
          {actions && <div className="mt-7 flex flex-wrap gap-3">{actions}</div>}
        </div>

        <div>{media}</div>
      </div>
    </div>
  );
}
