// `action` is the page's own button, on the right.
export function PageTitle({ title, sub, action }) {
  return (
    <div className="mb-8 flex flex-wrap items-end justify-between gap-4">
      <div className="min-w-0">
        <h1 className="text-3xl font-bold tracking-tight sm:text-4xl">{title}</h1>
        {sub && <p className="mt-2 text-muted">{sub}</p>}
      </div>

      {action}
    </div>
  );
}
