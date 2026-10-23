import { useState } from "react";
import { Button, FieldError, Form, Input, Label, TextField } from "@heroui/react";

import { BareLayout } from "../components/BareLayout";
import { TitledCard } from "../components/TitledCard";
import { signIn } from "../lib/api";
import { useT } from "../i18n";

export function Login() {
  const t = useT();

  const [password, setPassword] = useState("");
  const [error, setError] = useState(null);
  const [busy, setBusy] = useState(false);

  const onChange = (value) => {
    setPassword(value);
    setError(null);
  };

  const onSubmit = async (e) => {
    e.preventDefault();
    setBusy(true);

    try {
      const response = await signIn(password);

      if (response.status === 401) {
        setError(t("login.wrong"));
      } else if (response.status === 429) {
        setError(t("login.wait"));
      } else if (!response.ok) {
        setError(t("login.failed"));
      }
    } catch {
      setError(t("login.failed"));
    } finally {
      setBusy(false);
    }
  };

  return (
    <BareLayout>
      <TitledCard title={t("login.title")}>
        <Form className="flex flex-col gap-4" onSubmit={onSubmit}>
          {/* Lets a password manager file the password under this device. */}
          <input hidden readOnly autoComplete="username" name="username" value="cgdn1" />

          <TextField
            autoFocus
            isRequired
            isInvalid={Boolean(error)}
            name="password"
            type="password"
            value={password}
            onChange={onChange}
          >
            <Label>{t("login.password")}</Label>
            <Input autoComplete="current-password" variant="secondary" />
            <FieldError>{error}</FieldError>
          </TextField>

          <Button fullWidth isPending={busy} type="submit" variant="primary">
            {t("login.submit")}
          </Button>
        </Form>
      </TitledCard>
    </BareLayout>
  );
}
