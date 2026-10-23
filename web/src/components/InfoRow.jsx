import { cn } from "../lib/cn";

export function InfoList({ className, children }) {
  return <dl className={cn("divide-y divide-separator", className)}>{children}</dl>;
}

export function InfoRow({ label, children }) {
  return (
    <div className="flex items-baseline justify-between gap-4 py-2.5">
      <dt className="text-sm text-muted">{label}</dt>
      <dd className="text-right text-sm">{children}</dd>
    </div>
  );
}
