import { useEffect, useState } from "react";
import { useTheme } from "@heroui/react";

import { Loading } from "./Loading";
import { SignedIn } from "./SignedIn";
import { BareLayout } from "../components/BareLayout";
import { useStatus } from "../lib/api";
import { Login } from "../pages/Login";
import { Setup } from "../pages/system/Setup";

// A new device shows its setup, a set-up one its sign-in, and the rest of the interface
// only to a signed-in browser. A firmware from before sign-in (API 1) gets its own
// built-in page, which matches it.
export default function App() {
  useTheme("system");

  const { data: status, error } = useStatus();

  // The network the first setup saved. The setup page stays to say where the device
  // went, though the device then has a password and would show the sign-in.
  const [setupJoining, setSetupJoining] = useState(null);

  const old = status && status.api < 2 && window.location.pathname !== "/app";

  useEffect(() => {
    if (old) {
      window.location.replace("/app" + window.location.hash);
    }
  }, [old]);

  if (!status || old) {
    return (
      <BareLayout>
        <Loading failed={Boolean(error) && !status} />
      </BareLayout>
    );
  }

  if (status.cfg === false || setupJoining) {
    return <Setup status={status} joining={setupJoining} setJoining={setSetupJoining} />;
  }

  if (!status.signed_in) {
    return <Login />;
  }

  return <SignedIn />;
}
