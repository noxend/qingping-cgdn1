#!/usr/bin/env node
// Build the hosted web bundle, sign its manifest and publish both to cgdn1.swithy.com.
//
// The bundle goes to ~/sites/cgdn1/v/<version>/ on the Air (Caddy serves it, tunnel
// "air" publishes it, see the homelab repository), then manifest.json is replaced to
// name it. Old versions stay where they are.
//
// manifest.json is {"payload": base64(P), "sig": base64(DER ECDSA P-256 over P)}, where
// P is the JSON the device checks and keeps:
//   {"seq": <unix time>, "version": "...", "base": "https://cgdn1.swithy.com/v/<version>/",
//    "js": "assets/index-xxxx.js", "css": ["assets/style-xxxx.css"],
//    "integrity": {"assets/index-xxxx.js": "sha384-...", ...}}
// The device verifies the signature with firmware/components/net/web_manifest_pub.pem, accepts
// only a seq above the one it holds, and gives the browser the hashes. The private key
// is ~/.config/cgdn1/web_signing_key.pem, outside the checkout so nothing the build runs
// from web/ finds it there by path; without it nothing can be published.
//
// The version is web-<short git hash>. A working tree with changes under web/ is refused
// unless --allow-dirty, which adds a -dirty suffix and a timestamp: every signed manifest
// stays valid for devices below its seq, so test builds are not signed by default.

import { execFileSync, spawnSync } from "node:child_process";
import { createHash, sign, verify } from "node:crypto";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";

const ROOT = dirname(dirname(fileURLToPath(import.meta.url)));
const WEB = join(ROOT, "web");
const KEY = join(homedir(), ".config", "cgdn1", "web_signing_key.pem");
const PUB = join(ROOT, "firmware", "components", "net", "web_manifest_pub.pem");
const SITE = "https://cgdn1.swithy.com";
// Cloudflare's Browser Integrity Check answers 403 (error 1010) to a script's own
// User-Agent; a browser's passes, and only browsers load the bundle.
const UA = "Mozilla/5.0 (Macintosh) publish_web.mjs";
// The device's limits (web_manifest.c): what does not fit is refused there.
const MAX_CSS = 2;
const MAX_PAYLOAD = 1536;

const USAGE = `Usage: node tools/publish_web.mjs [--host sasha@100.94.155.123]
                                 [--dry-run] [--allow-dirty]

  --host         where the site lives (default sasha@100.94.155.123)
  --dry-run      build and sign, upload nothing
  --allow-dirty  publish a build of uncommitted web/`;

function run(cmd, args, options = {}) {
  console.log("+", cmd, ...args);
  execFileSync(cmd, args, { stdio: "inherit", ...options });
}

function now() {
  return Math.floor(Date.now() / 1000);
}

function version() {
  const sha = execFileSync("git", ["-C", ROOT, "rev-parse", "--short", "HEAD"], {
    encoding: "utf8",
  }).trim();
  const diff = spawnSync("git", ["-C", ROOT, "diff", "--quiet", "HEAD", "--", "web"], {
    stdio: "inherit",
  });
  const dirty = diff.status !== 0;
  return `web-${sha}` + (dirty ? `-dirty-${now()}` : "");
}

function sriOf(data) {
  return "sha384-" + createHash("sha384").update(data).digest("base64");
}

function signPayload(payload) {
  return sign("sha256", payload, readFileSync(KEY));
}

function verifyPayload(payload, sig) {
  return verify("sha256", payload, readFileSync(PUB), sig);
}

async function fetchWithCors(url) {
  const res = await fetch(url, {
    headers: {
      Origin: "http://cgdn1.local",
      "User-Agent": UA,
    },
    signal: AbortSignal.timeout(20_000),
  });
  if (!res.ok) {
    throw new Error(`HTTP ${res.status} for ${url}`);
  }
  const body = Buffer.from(await res.arrayBuffer());
  return [body, res.headers.get("Access-Control-Allow-Origin")];
}

function parse() {
  try {
    const { values } = parseArgs({
      options: {
        host: { type: "string", default: "sasha@100.94.155.123" },
        "dry-run": { type: "boolean", default: false },
        "allow-dirty": { type: "boolean", default: false },
        help: { type: "boolean", short: "h", default: false },
      },
    });
    return values;
  } catch (e) {
    console.error(`${USAGE}\n\npublish_web.mjs: error: ${e.message}`);
    process.exit(2);
  }
}

async function main() {
  const args = parse();
  if (args.help) {
    console.log(USAGE);
    return 0;
  }
  if (!existsSync(KEY)) {
    console.error(`no signing key at ${KEY}`);
    return 1;
  }

  const ver = version();
  if (ver.includes("-dirty-") && !args["allow-dirty"]) {
    console.error("web/ has uncommitted changes; commit them or pass --allow-dirty");
    return 1;
  }
  // A seq at or below the live one is ignored by every device: refuse it here
  // rather than print "published" for a build nobody will load.
  let liveSeq;
  try {
    const [live] = await fetchWithCors(`${SITE}/manifest.json?t=${now()}`);
    const livePayload = Buffer.from(JSON.parse(live).payload, "base64");
    liveSeq = JSON.parse(livePayload).seq;
  } catch (e) {
    // First publish, or the site is down: nothing to compare.
    console.log(`no live manifest to compare with (${e.message})`);
    liveSeq = 0;
  }
  if (now() <= liveSeq) {
    console.error(`this clock is behind the live manifest (seq ${liveSeq})`);
    return 1;
  }
  run("npm", ["run", "build", "-w", "web", "--", "--mode", "hosted"], {
    cwd: ROOT,
    env: { ...process.env, HOSTED_VERSION: ver },
  });

  const out = join(WEB, "dist-hosted");
  const vite = JSON.parse(readFileSync(join(out, ".vite", "manifest.json"), "utf8"));
  const entry = vite["index.html"];
  // With cssCodeSplit off the stylesheet is its own manifest entry, not the
  // entry's css list. A second script would load without a hash: refuse it.
  const css = [...(entry.css ?? []), ...("style.css" in vite ? [vite["style.css"].file] : [])];
  const scripts = Object.values(vite)
    .map((v) => v.file)
    .filter((file) => file.endsWith(".js"));
  if (scripts.length !== 1 || scripts[0] !== entry.file || css.length > MAX_CSS) {
    const found = `${JSON.stringify(scripts)} ${JSON.stringify(css)}`;
    console.error(`the build must be one script and at most ${MAX_CSS} stylesheets: ${found}`);
    return 1;
  }
  const files = [entry.file, ...css];
  const integrity = {};
  for (const file of files) {
    integrity[file] = sriOf(readFileSync(join(out, file)));
  }
  const inner = {
    seq: now(),
    version: ver,
    base: `${SITE}/v/${ver}/`,
    js: entry.file,
    css,
    integrity,
  };
  const payload = Buffer.from(JSON.stringify(inner));
  if (payload.length > MAX_PAYLOAD) {
    console.error(`payload ${payload.length} bytes, the device takes ${MAX_PAYLOAD}`);
    return 1;
  }
  const sig = signPayload(payload);
  if (!verifyPayload(payload, sig)) {
    console.error("the fresh signature does not verify with web_manifest_pub.pem");
    return 1;
  }
  const envelope = {
    payload: payload.toString("base64"),
    sig: sig.toString("base64"),
  };
  const text = JSON.stringify(envelope);
  console.log(JSON.stringify(inner, null, 2));
  if (args["dry-run"]) {
    return 0;
  }

  // The build first, then the manifest that points at it: a device never reads a
  // manifest naming files that are not there yet.
  const host = args.host;
  run("rsync", [
    "-a",
    "--exclude",
    ".vite",
    "--exclude",
    "index.html",
    out + "/",
    `${host}:sites/cgdn1/v/${ver}/`,
  ]);
  const tmp = join(out, "manifest.json");
  writeFileSync(tmp, text + "\n");
  run("rsync", ["-a", tmp, `${host}:sites/cgdn1/manifest.json`]);
  // The site root explains itself; the interface is opened from the device.
  const rootPage = join(ROOT, "site", "index.html");
  run("rsync", ["-a", rootPage, `${host}:sites/cgdn1/index.html`]);

  // Read it back through Cloudflare as the device and the browser will.
  const [body] = await fetchWithCors(`${SITE}/manifest.json?t=${now()}`);
  const got = JSON.parse(body);
  const gotPayload = Buffer.from(got.payload, "base64");
  if (!verifyPayload(gotPayload, Buffer.from(got.sig, "base64"))) {
    console.error("the published manifest does not verify");
    return 1;
  }
  const gotInner = JSON.parse(gotPayload);
  if (gotInner.version !== ver) {
    console.error(`manifest through the tunnel says ${gotInner.version}, expected ${ver}`);
    return 1;
  }
  for (const [file, want] of Object.entries(inner.integrity)) {
    const [data, cors] = await fetchWithCors(inner.base + file);
    const ok = sriOf(data) === want && cors === "*";
    console.log(`${ok ? "ok " : "BAD"} ${file} (${data.length} bytes, cors ${cors})`);
    if (!ok) {
      return 1;
    }
  }
  console.log(`published ${ver} (seq ${inner.seq}, signature verified)`);
  return 0;
}

if (import.meta.main) {
  process.exitCode = await main();
}
