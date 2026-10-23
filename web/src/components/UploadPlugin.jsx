import { useRef, useState } from "react";
import { Plus } from "@gravity-ui/icons";
import { Button, Link, Modal } from "@heroui/react";

import { InfoList, InfoRow } from "./InfoRow";
import { Notice } from "./Notice";
import { formatBytes } from "../lib/bytes";
import { textIn } from "../lib/manifest";
import { readModuleManifest } from "../lib/wasmManifest";
import { useLang, useT } from "../i18n";

const PLUGIN_GUIDE = "https://github.com/noxend/qingping-cgdn1/blob/main/plugins/README.md";

// Nobody signed the module, so the hosts it may reach are shown before it is installed.
function ChosenModule({ chosen, store }) {
  const { t, lang } = useLang();

  const { manifest, bytes } = chosen;
  const installed = store.installedOf(manifest.id);
  const size = formatBytes(bytes.byteLength, lang);

  return (
    <>
      <p className="font-medium">{textIn(manifest.name, lang)}</p>
      <p className="mt-1 text-sm text-muted">{textIn(manifest.description, lang)}</p>

      <InfoList className="mt-2">
        <InfoRow label={t("plugin.version")}>{manifest.version}</InfoRow>
        <InfoRow label={t("plugin.size")}>{size}</InfoRow>
        {manifest.hosts.length > 0 && (
          <InfoRow label={t("upload.hosts")}>{manifest.hosts.join(", ")}</InfoRow>
        )}
      </InfoList>

      {installed && (
        <p className="mt-4 text-sm text-muted">
          {t("upload.replaces", { version: installed.version })}
        </p>
      )}
    </>
  );
}

// Reads a module's plugin.json in the page; null with the refusal recorded when the
// file cannot be a plugin for this device.
async function readModule(file, store) {
  if (store.device && file.size > store.device.storage.free) {
    store.refuseUpload("space");
    return null;
  }

  let bytes;
  try {
    bytes = await file.arrayBuffer();
  } catch {
    store.refuseUpload("section");
    return null;
  }

  const manifest = readModuleManifest(bytes);
  if (!manifest || manifest.refused) {
    store.refuseUpload(manifest?.refused ?? "section");
    return null;
  }

  return { manifest, bytes };
}

// The dialog stays open until the device answers, so a refusal is read here.
export function UploadPlugin({ store }) {
  const t = useT();

  const input = useRef(null);
  const [open, setOpen] = useState(false);
  const [chosen, setChosen] = useState(null);
  const [sent, setSent] = useState(false);

  const uploading = store.pending?.kind === "upload";
  const busy = sent || uploading;

  // The upload this dialog sent is over: a refusal keeps the dialog open to say why.
  if (sent && !uploading) {
    setSent(false);
    setChosen(null);
    if (!store.uploadProblem) {
      setOpen(false);
    }
  }

  const show = () => {
    store.refuseUpload(null);
    setChosen(null);
    setOpen(true);
  };

  const pick = async (file) => {
    if (!file) {
      return;
    }

    store.refuseUpload(null);
    setChosen(null);

    const module = await readModule(file, store);
    if (module) {
      setChosen(module);
    }
  };

  const install = () => {
    setSent(true);
    store.upload(chosen.manifest.id, chosen.bytes);
  };

  const problem = !busy && store.uploadProblem;

  return (
    <Modal>
      <Button
        variant="primary"
        className="shrink-0"
        isDisabled={Boolean(store.pending) || store.device?.runtime === false}
        onPress={show}
      >
        <Plus />
        {t("upload.add")}
      </Button>

      <Modal.Backdrop
        isOpen={open}
        onOpenChange={setOpen}
        isDismissable={!busy}
        isKeyboardDismissDisabled={busy}
      >
        <Modal.Container size="md">
          <Modal.Dialog>
            <Modal.Header>
              <Modal.Heading>{t("upload.add")}</Modal.Heading>
            </Modal.Header>

            <Modal.Body>
              <input
                ref={input}
                type="file"
                accept=".wasm,application/wasm"
                hidden
                onChange={(e) => {
                  pick(e.target.files?.[0]);
                  // The same file chosen again is a new choice.
                  e.target.value = "";
                }}
              />

              {chosen ? (
                <ChosenModule chosen={chosen} store={store} />
              ) : (
                <p className="text-sm leading-relaxed text-muted">
                  {t("upload.text")}{" "}
                  <Link href={PLUGIN_GUIDE} target="_blank" rel="noopener noreferrer">
                    {t("upload.guide")}
                  </Link>
                </p>
              )}

              {problem && (
                <Notice
                  status="danger"
                  className="mt-4"
                  title={problem.text}
                  description={problem.detail}
                />
              )}
            </Modal.Body>

            <Modal.Footer>
              <Button variant="secondary" slot="close" isDisabled={busy}>
                {t("store.cancel")}
              </Button>

              {chosen ? (
                <Button variant="primary" isPending={busy} onPress={install}>
                  {t("upload.install")}
                </Button>
              ) : (
                <Button variant="primary" onPress={() => input.current?.click()}>
                  {t("upload.choose")}
                </Button>
              )}
            </Modal.Footer>
          </Modal.Dialog>
        </Modal.Container>
      </Modal.Backdrop>
    </Modal>
  );
}
