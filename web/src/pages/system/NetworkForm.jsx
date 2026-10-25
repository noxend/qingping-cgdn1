import { useEffect, useEffectEvent, useState } from "react";
import { ArrowsRotateRight, Eye, EyeSlash } from "@gravity-ui/icons";
import {
  Button,
  ComboBox,
  Description,
  EmptyState,
  FieldError,
  Form,
  Input,
  InputGroup,
  Label,
  ListBox,
  TextField,
} from "@heroui/react";

import { ActionTip } from "../../components/ActionTip";
import { accepted, refused } from "../../lib/answer";
import { postForm } from "../../lib/api";
import { notify } from "../../lib/notify";
import { useT } from "../../i18n";

const CONFIG_PASSWORD = /^[\x21-\x7e]{8,63}$/;

// Empty for an open network, 8 to 63 bytes, or a 64-digit hex key (wifi_setup.c).
function wifiPasswordOk(value) {
  const bytes = new TextEncoder().encode(value).length;
  return bytes === 0 || (bytes >= 8 && bytes <= 63) || /^[0-9a-fA-F]{64}$/.test(value);
}

// The list scans when it first opens; Scan looks again.
function NetworkPicker({ label, nets, scanning, onScan, onOpen, value, onChange }) {
  const t = useT();

  // A typed name that matches none of the networks found leaves the list empty too.
  const empty = () => (
    <EmptyState className="px-3 py-4 text-center">
      {scanning && t("system.scanning")}
      {!scanning && (nets.length ? t("system.noMatch") : t("system.noNetsHint"))}
    </EmptyState>
  );

  return (
    <ComboBox
      isRequired
      allowsCustomValue
      allowsEmptyCollection
      className="w-full"
      inputValue={value}
      menuTrigger="focus"
      name="ssid"
      variant="secondary"
      onInputChange={onChange}
      onOpenChange={(open) => open && onOpen()}
      onSelectionChange={(key) => key && onChange(String(key))}
    >
      <Label>{label}</Label>
      <ComboBox.InputGroup>
        <Input maxLength={32} placeholder={t("system.netPlaceholder")} />
        <ComboBox.Trigger />
      </ComboBox.InputGroup>
      <Description>{t("system.hiddenNet")}</Description>

      <ComboBox.Popover>
        <div className="flex items-center justify-between gap-3 border-b border-separator p-2 ps-3">
          <span className="text-sm text-muted">
            {nets.length > 0 && t("system.found", { n: nets.length })}
          </span>
          <Button size="sm" variant="secondary" isPending={scanning} onPress={onScan}>
            <ArrowsRotateRight />
            {t("system.scan")}
          </Button>
        </div>

        <ListBox renderEmptyState={empty}>
          {nets.map((name) => (
            <ListBox.Item key={name} id={name} textValue={name}>
              {name}
              <ListBox.ItemIndicator />
            </ListBox.Item>
          ))}
        </ListBox>
      </ComboBox.Popover>

      <FieldError>{t("system.pickNet")}</FieldError>
    </ComboBox>
  );
}

function WifiPasswordField({ value, onChange }) {
  const t = useT();
  const [shown, setShown] = useState(false);

  return (
    <TextField
      name="wifi-password"
      validate={(v) => (wifiPasswordOk(v) ? null : t("system.wifiPassRules"))}
      value={value}
      onChange={onChange}
    >
      <Label>{t("system.wifiPass")}</Label>
      <InputGroup variant="secondary">
        <InputGroup.Input maxLength={64} type={shown ? "text" : "password"} />
        <InputGroup.Suffix className="border-s-0 ps-0 pe-1">
          <Button
            isIconOnly
            aria-label={shown ? t("system.hidePass") : t("system.showPass")}
            size="sm"
            variant="ghost"
            onPress={() => setShown((v) => !v)}
          >
            {shown ? <Eye className="size-4" /> : <EyeSlash className="size-4" />}
          </Button>
        </InputGroup.Suffix>
      </InputGroup>
      <FieldError />
    </TextField>
  );
}

function ConfigPasswordField({ value, onChange }) {
  const t = useT();

  return (
    <TextField
      isRequired
      name="config-password"
      validate={(v) => (CONFIG_PASSWORD.test(v) ? null : t("system.cfgPassRules"))}
      value={value}
      onChange={onChange}
    >
      <Label>{t("system.cfgPassNew")}</Label>
      <InputGroup variant="secondary">
        <InputGroup.Input autoComplete="new-password" maxLength={63} />
      </InputGroup>
      <Description>{t("system.cfgPassNewHint")}</Description>
      <FieldError />
    </TextField>
  );
}

export function NetworkForm({ status, onSaved }) {
  const t = useT();

  // A device with no password yet (a new one, or after a factory reset): the password
  // typed here becomes it, and the device takes it only over its setup AP.
  const firstSetup = status?.cfg === false;

  const [nets, setNets] = useState([]);
  const [net, setNet] = useState("");
  const [wifiPassword, setWifiPassword] = useState("");
  const [configPassword, setConfigPassword] = useState("");
  // A new device needs no password to scan, so the form opens on a scan.
  const [scanning, setScanning] = useState(firstSetup);
  const [scanned, setScanned] = useState(false);
  const [saving, setSaving] = useState(false);

  // The networks the device sees, or null after a failure, which it shows in a toast.
  const findNets = async () => {
    try {
      const response = await postForm("/scan", {});
      if (refused(t, response)) {
        return null;
      }

      return (await response.json()).nets || [];
    } catch {
      notify(t("system.scanFailed"), "danger");
      return null;
    }
  };

  const listNets = () =>
    findNets()
      .then((found) => {
        if (found) {
          setNets(found);
        }
      })
      .finally(() => {
        setScanning(false);
        setScanned(true);
      });

  const scan = () => {
    setScanning(true);
    listNets();
  };

  // Once per form: a scan knocks the station off its channel for a moment.
  const openList = () => {
    if (!scanned && !scanning) {
      scan();
    }
  };

  const scanOnOpen = useEffectEvent(() => {
    if (firstSetup) {
      listNets();
    }
  });

  useEffect(() => {
    scanOnOpen();
  }, []);

  const save = async () => {
    const name = net.trim();
    const form = { ssid: name, pass: wifiPassword };
    if (firstSetup) {
      form.token = configPassword;
    }

    setSaving(true);

    try {
      const response = await postForm("/save", form);
      if (!(await accepted(t, response, t("toast.saveFailed")))) {
        return;
      }

      onSaved(name);
    } catch {
      // The device restarts right after saving, which can cut the reply short.
      onSaved(name);
    } finally {
      setSaving(false);
    }
  };

  const onSubmit = (e) => {
    e.preventDefault();
    save();
  };

  return (
    <Form className="flex flex-col gap-4" onSubmit={onSubmit}>
      {firstSetup && <ConfigPasswordField value={configPassword} onChange={setConfigPassword} />}

      <NetworkPicker
        label={firstSetup ? t("system.net") : t("system.newNet")}
        nets={nets}
        scanning={scanning}
        onScan={scan}
        onOpen={openList}
        value={net}
        onChange={setNet}
      />

      <WifiPasswordField value={wifiPassword} onChange={setWifiPassword} />

      <ActionTip hint={t("system.saveHint")}>
        <Button fullWidth isPending={saving} type="submit" variant="primary">
          {t("system.saveConnect")}
        </Button>
      </ActionTip>
    </Form>
  );
}
