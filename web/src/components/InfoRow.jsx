import { cn } from "../lib/cn";

export function InfoList({ className, children }) {
  return <dl className={cn("divide-y divide-separator", className)}>{children}</dl>;
}

// `figure` sets a number or a version in the figures face.
export function InfoRow({ label, figure, children }) {
  return (
    <div className="flex items-baseline justify-between gap-4 py-2.5">
      <dt className="text-sm text-muted">{label}</dt>
      <dd className={cn("text-right text-sm", figure && "font-mono tnum")}>{children}</dd>
    </div>
  );
}
