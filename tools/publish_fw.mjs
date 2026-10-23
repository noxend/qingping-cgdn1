#!/usr/bin/env node
// Build the firmware, sign its release manifest and publish both to cgdn1.swithy.com.
//
// The image goes to ~/sites/cgdn1/fw/<version>/cgdn1.bin on the Air (Caddy serves it, tunnel
// "air" publishes it, see the homelab repository), then firmware.json is replaced to name it.
// Old images stay where they are.
//
// firmware.json is {"payload": base64(P), "sig": base64(DER ECDSA P-256 over P)}, the envelope
// of manifest.json (publish_web.mjs), signed with the same key, where P is
//   {"kind": "firmware", "seq": <unix time>, "version": "0.27.6",
//    "url": "https://cgdn1.swithy.com/fw/0.27.6/cgdn1.bin", "sha256": "<64 hex>",
//    "size": <bytes>, "notes": "..."}
// The device (firmware/components/net/fw_manifest.c) offers it as an update when the version is
// above its own; fw_update.c then holds the downloaded image to this size, hash and version
// before esp_ota_end checks the image's own signature.
//
// The image is built here from a clean tree with DEV_BUILD off, and its version is FW_VERSION
// (components/api/version.h). Refused: uncommitted changes under firmware/ or plugins/ (the image
// embeds the standard plugins built from plugins/standard), a DEV_BUILD build, one with the test
// endpoints of cgdn1-dev, a version not above the live one. --dry-run builds and signs, uploads
// nothing; with --allow-dirty it also takes a dirty tree, which a real publish never does.

import { execFileSync } from "node:child_process";
import { createHash, sign, verify } from "node:crypto";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { homedir, tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";

const ROOT = dirname(dirname(fileURLToPath(import.meta.url)));
const FW = join(ROOT, "firmware");
const IMAGE = join(FW, "build", "cgdn1.bin");
const VERSION_H = join(FW, "components", "api", "version.h");
const KEY = join(homedir(), ".config", "cgdn1", "web_signing_key.pem");
const PUB = join(FW, "components", "net", "web_manifest_pub.pem");
const SITE = "https://cgdn1.swithy.com";
// Cloudflare's Browser Integrity Check answers 403 (error 1010) to a script's own
// User-Agent; a browser's passes.
const UA = "Mozilla/5.0 (Macintosh) publish_fw.mjs";
// The device's limits (fw_manifest.c, signed_envelope.c): what does not fit is refused there.
const MAX_PAYLOAD = 1536;
const MAX_NOTES = 240;
const SLOT = 0x1e0000;
// esp_app_desc_t sits at the start of the first segment: magic, then version and project.
const DESC_MAGIC = Buffer.from("3254cdab", "hex");

const USAGE = `Usage: node tools/publish_fw.mjs --notes "..." [--host sasha@100.94.155.123]
                                [--dry-run [--allow-dirty]] [--out firmware.json]

  --notes        what changed, shown on the device's page (required)
  --host         where the site lives (default sasha@100.94.155.123)
  --dry-run      build and sign, upload nothing
  --allow-dirty  with --dry-run: a dirty tree
  --out          also write the signed firmware.json here`;

function run(cmd, args) {
  console.log("+", cmd, ...args);
  execFileSync(cmd, args, { stdio: "inherit" });
}

function now() {
  return Math.floor(Date.now() / 1000);
}

function fwVersion() {
  const text = readFileSync(VERSION_H, "utf8");
  return text.match(/^#define FW_VERSION "([0-9.]+)"/m)[1];
}

// Compares dotted versions part by part, as Python compares lists of ints.
function compareVersions(a, b) {
  const x = a.split(".").map(Number);
  const y = b.split(".").map(Number);
  for (let i = 0; i < Math.min(x.length, y.length); i++) {
    if (x[i] !== y[i]) {
      return x[i] - y[i];
    }
  }
  return x.length - y.length;
}

function notesProblem(notes) {
  const bytes = Buffer.byteLength(notes);
  if (bytes > MAX_NOTES) {
    return `notes are ${bytes} bytes, the device takes ${MAX_NOTES}`;
  }
  if ([...notes].some((c) => c < " " || '"\\\x7f'.includes(c))) {
    return "notes may not hold quotes, backslashes or control characters";
  }
  return null;
}

function imageDesc(data) {
  const i = data.indexOf(DESC_MAGIC);
  if (i < 0) {
    return [null, null];
  }
  const field = (off) => {
    const raw = data.subarray(i + off, i + off + 32);
    const end = raw.indexOf(0);
    return raw.subarray(0, end < 0 ? raw.length : end).toString();
  };
  return [field(16), field(48)];
}

export function payloadFor(data, version, notes, seq) {
  const inner = {
    kind: "firmware",
    seq,
    version,
    url: `${SITE}/fw/${version}/cgdn1.bin`,
    sha256: createHash("sha256").update(data).digest("hex"),
    size: data.length,
    notes,
  };
  return [inner, Buffer.from(JSON.stringify(inner))];
}

function signPayload(payload, key) {
  return sign("sha256", payload, readFileSync(key));
}

function verifyPayload(payload, sig) {
  return verify("sha256", payload, readFileSync(PUB), sig);
}

export function envelope(payload, key = KEY) {
  return {
    payload: payload.toString("base64"),
    sig: signPayload(payload, key).toString("base64"),
  };
}

async function fetchBytes(url) {
  const res = await fetch(url, {
    headers: { "User-Agent": UA },
    signal: AbortSignal.timeout(60_000),
  });
  if (!res.ok) {
    const error = new Error(`HTTP ${res.status} for ${url}`);
    error.status = res.status;
    throw error;
  }
  return Buffer.from(await res.arrayBuffer());
}

// The live payload, null when the site has none yet (404); any other failure throws,
// since without it neither the version nor the clock can be checked.
async function liveManifest() {
  let body;
  try {
    body = await fetchBytes(`${SITE}/firmware.json?t=${now()}`);
  } catch (e) {
    if (e.status === 404) {
      console.log("no firmware.json on the site yet: first publish");
      return null;
    }
    throw e;
  }
  const payload = Buffer.from(JSON.parse(body).payload, "base64");
  return JSON.parse(payload);
}

function treeDirty() {
  const git = ["-C", ROOT, "status", "--porcelain", "--", "firmware", "plugins"];
  return execFileSync("git", git, { encoding: "utf8" }).trim() !== "";
}

// null when the image is a release build, else why it is not.
function build() {
  // An empty EXTRA_COMPONENT_DIRS drops a test component a cached configure kept.
  run("idf.py", ["-C", FW, "-DDEV_BUILD=0", "-DEXTRA_COMPONENT_DIRS=", "build"]);
  const cache = readFileSync(join(FW, "build", "CMakeCache.txt"), "utf8");
  const m = cache.match(/^DEV_BUILD:[A-Z]+=(.*)$/m);
  if (m !== null && !["0", "OFF", ""].includes(m[1])) {
    return "the build directory is configured with DEV_BUILD on";
  }
  const description = readFileSync(join(FW, "build", "project_description.json"), "utf8");
  if (JSON.parse(description).build_components.includes("test_hooks")) {
    return "the build has the test_hooks component (cgdn1-dev)";
  }
  return null;
}

function parse() {
  try {
    const { values } = parseArgs({
      options: {
        notes: { type: "string" },
        host: { type: "string", default: "sasha@100.94.155.123" },
        "dry-run": { type: "boolean", default: false },
        "allow-dirty": { type: "boolean", default: false },
        out: { type: "string" },
        help: { type: "boolean", short: "h", default: false },
      },
    });
    if (!values.help && values.notes === undefined) {
      throw new Error("the following arguments are required: --notes");
    }
    return values;
  } catch (e) {
    console.error(`${USAGE}\n\npublish_fw.mjs: error: ${e.message}`);
    process.exit(2);
  }
}

async function main() {
  const args = parse();
  if (args.help) {
    console.log(USAGE);
    return 0;
  }
  if (args["allow-dirty"] && !args["dry-run"]) {
    console.error("--allow-dirty only goes with --dry-run");
    return 1;
  }
  if (!existsSync(KEY)) {
    console.error(`no signing key at ${KEY}`);
    return 1;
  }
  let problem = notesProblem(args.notes);
  if (problem) {
    console.error(problem);
    return 1;
  }
  if (treeDirty() && !args["allow-dirty"]) {
    console.error("firmware/ or plugins/ has uncommitted changes; commit them first");
    return 1;
  }

  const ver = fwVersion();
  const live = await liveManifest();
  const seq = now();
  if (live && compareVersions(ver, live.version) <= 0) {
    console.error(`FW_VERSION ${ver} is not above the live ${live.version}`);
    return 1;
  }
  if (live && seq <= live.seq) {
    console.error(`this clock is behind the live manifest (seq ${live.seq})`);
    return 1;
  }

  problem = build();
  if (problem) {
    console.error(problem);
    return 1;
  }
  const data = readFileSync(IMAGE);
  const [gotVer, project] = imageDesc(data);
  if (gotVer !== ver || project !== "cgdn1") {
    console.error(`the image says ${project} ${gotVer}, expected cgdn1 ${ver}`);
    return 1;
  }
  if (data.length > SLOT) {
    console.error(`image is ${data.length} bytes, the slot holds ${SLOT}`);
    return 1;
  }

  const [inner, payload] = payloadFor(data, ver, args.notes, seq);
  if (payload.length > MAX_PAYLOAD) {
    console.error(`payload ${payload.length} bytes, the device takes ${MAX_PAYLOAD}`);
    return 1;
  }
  const env = envelope(payload);
  if (!verifyPayload(payload, Buffer.from(env.sig, "base64"))) {
    console.error("the fresh signature does not verify with web_manifest_pub.pem");
    return 1;
  }
  const text = JSON.stringify(env);
  if (args.out) {
    writeFileSync(args.out, text + "\n");
  }
  console.log(JSON.stringify(inner, null, 2));
  if (args["dry-run"]) {
    return 0;
  }

  // The image first, then the manifest that names it: a device never reads a
  // manifest whose image is not there yet.
  const host = args.host;
  run("ssh", [host, `mkdir -p sites/cgdn1/fw/${ver}`]);
  run("rsync", ["-a", IMAGE, `${host}:sites/cgdn1/fw/${ver}/cgdn1.bin`]);
  const tmp = mkdtempSync(join(tmpdir(), "publish_fw-"));
  try {
    const manifest = join(tmp, "firmware.json");
    writeFileSync(manifest, text + "\n", { mode: 0o600 });
    run("rsync", ["-a", "--chmod=F644", manifest, `${host}:sites/cgdn1/firmware.json`]);
  } finally {
    rmSync(tmp, { recursive: true, force: true });
  }

  // Read both back through Cloudflare.
  const got = JSON.parse(await fetchBytes(`${SITE}/firmware.json?t=${now()}`));
  const gotPayload = Buffer.from(got.payload, "base64");
  if (!verifyPayload(gotPayload, Buffer.from(got.sig, "base64"))) {
    console.error("the published manifest does not verify");
    return 1;
  }
  if (JSON.parse(gotPayload).version !== ver) {
    console.error("firmware.json through the tunnel names another version");
    return 1;
  }
  const image = await fetchBytes(inner.url);
  if (createHash("sha256").update(image).digest("hex") !== inner.sha256) {
    console.error(`the image through the tunnel differs (${image.length} bytes)`);
    return 1;
  }
  console.log(`published ${ver} (seq ${seq}, ${data.length} bytes, signature verified)`);
  return 0;
}

if (import.meta.main) {
  process.exitCode = await main();
}
