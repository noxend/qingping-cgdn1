import { toast } from "@heroui/react";

// `detail` is a second, smaller line: the device's own words, for whoever builds or
// debugs it.
export function notify(text, kind = "success", detail) {
  const options = detail ? { description: detail } : undefined;

  if (kind === "danger") {
    toast.danger(text, options);
  } else if (kind === "warning") {
    toast.warning(text, options);
  } else {
    toast.success(text, options);
  }
}
