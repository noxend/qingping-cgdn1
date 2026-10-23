import { CircleFill } from "@gravity-ui/icons";
import { Chip } from "@heroui/react";

// A dot and a label, as in HeroUI's Chip "Statuses" example.
export function StatusChip({ color = "success", children }) {
  return (
    <Chip color={color} size="sm" variant="soft">
      <CircleFill width={6} />
      <Chip.Label>{children}</Chip.Label>
    </Chip>
  );
}
