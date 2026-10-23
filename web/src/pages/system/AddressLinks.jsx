import { Link } from "@heroui/react";

// The addresses to open once the device is on the home network.
export function AddressLinks({ status }) {
  const host = status?.host || "cgdn1.local";

  const urls = ["http://" + host + "/"];
  if (status?.ip) {
    urls.push("http://" + status.ip + "/");
  }

  return (
    <ul className="flex flex-col gap-1">
      {urls.map((url) => (
        <li key={url}>
          <Link href={url} className="font-medium">
            {url}
          </Link>
        </li>
      ))}
    </ul>
  );
}
