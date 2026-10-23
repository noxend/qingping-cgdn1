import { Tooltip } from "@heroui/react";

// What an action will do, on hover. With no hint the child renders untouched.
export function ActionTip({ hint, children }) {
  if (!hint) {
    return children;
  }

  return (
    <Tooltip delay={350}>
      <Tooltip.Trigger>{children}</Tooltip.Trigger>
      <Tooltip.Content>{hint}</Tooltip.Content>
    </Tooltip>
  );
}
