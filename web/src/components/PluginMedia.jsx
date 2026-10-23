import { DeviceScreen } from "./DeviceScreen";
import { usePluginScreen } from "../lib/api";

// A plugin's own screen, rendered on the device off-screen. Until the frame comes, and
// for a plugin that is not installed, the panel stays dark, as the device's does.
export function PluginMedia({ plugin, className }) {
  const { data } = usePluginScreen(plugin.installed ? plugin.id : null);

  return <DeviceScreen fb={data?.fb} led={data?.led} className={className} />;
}
