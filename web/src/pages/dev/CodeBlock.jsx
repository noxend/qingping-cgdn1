import { cn } from "../../lib/cn";

export function CodeBlock({ className, children }) {
  return (
    <pre className={cn("overflow-x-auto rounded-lg bg-default p-3 font-mono text-xs", className)}>
      {children}
    </pre>
  );
}
