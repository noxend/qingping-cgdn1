// Writes the signed firmware manifests run.sh feeds the harness into the current
// directory, built with tools/publish_fw.mjs's own functions over the given image.
// Usage: node make_releases.mjs <path to publish_fw.mjs> <image.bin>

import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

const [publisher, imagePath] = process.argv.slice(2);
const p = await import(pathToFileURL(publisher).href);
const data = readFileSync(imagePath);
const seq = Math.floor(Date.now() / 1000);

function write(name, env) {
  writeFileSync(name, JSON.stringify(env));
}

function release(name, over = {}) {
  const {
    version = "0.27.6",
    // Ukrainian and English: the payload has to carry non-ASCII notes.
    notes = "Нова версія: firmware update from the web",
    seq: at = seq,
    ...fields
  } = over;
  const [inner] = p.payloadFor(data, version, notes, at);
  const payload = Buffer.from(JSON.stringify({ ...inner, ...fields }));
  write(name, p.envelope(payload));
  return payload;
}

const real = release("real.json");
const env = JSON.parse(readFileSync("real.json", "utf8"));
const tampered = Buffer.from(real.toString().replace("0.27.6", "0.27.7"));
write("tampered.json", { ...env, payload: tampered.toString("base64") });
write("otherkey.json", p.envelope(real, "other.pem"));
release("offpath.json", { url: "https://cgdn1.swithy.com/v/x/cgdn1.bin" });
release("quote.json", { notes: 'a "quoted" note' });
release("same.json", { version: "0.27.5", seq: seq + 1 });
release("older.json", { version: "0.27.9", seq: seq - 1000 });
