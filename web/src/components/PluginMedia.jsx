import { Skeleton } from "@heroui/react";

import { DeviceScreen } from "./DeviceScreen";
import { usePluginScreen } from "../lib/api";
import { cn } from "../lib/cn";

// A plugin's own screen, rendered on the device off-screen. A plugin that is not
// installed has nothing to render: the panel stays dark.
export function PluginMedia({ plugin, className }) {
  const { data } = usePluginScreen(plugin.installed ? plugin.id : null);

  if (!plugin.installed) {
    return <DeviceScreen className={className} />;
  }

  if (!data) {
    return <Skeleton className={cn("aspect-[5/4] rounded-[9%]", className)} />;
  }

  return <DeviceScreen fb={data.fb} led={data.led} className={className} />;
}
