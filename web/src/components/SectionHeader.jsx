export function SectionHeader({ title, count }) {
  return (
    <div className="mb-5 flex items-end justify-between gap-4">
      <h2 className="text-lg font-semibold tracking-[-0.01em]">
        {title}
        {count !== undefined && (
          <span className="ml-2 font-mono text-sm font-normal text-muted tnum">{count}</span>
        )}
      </h2>
    </div>
  );
}
