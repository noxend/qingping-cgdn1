export function SectionHeader({ title, count }) {
  return (
    <div className="mb-5 flex items-end justify-between gap-4">
      <h2 className="text-xl font-semibold tracking-tight">
        {title}
        {count !== undefined && <span className="ml-2 font-normal text-muted">{count}</span>}
      </h2>
    </div>
  );
}
