// The plugin.json a module carries in its custom section "cgdn1.plugin"
// (plugins/sdk/embed_manifest.py), read in the page before the module is uploaded, so
// the user sees what it is and which hosts it will reach. The device reads the same
// section and checks it again.

const SECTION = "cgdn1.plugin";
const HEADER = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];

// An unsigned LEB128 number at bytes[at]: [value, next offset], or null.
function leb(bytes, at) {
  let value = 0;
  for (let shift = 0; shift < 35 && at < bytes.length; shift += 7) {
    const b = bytes[at++];
    value += (b & 0x7f) * 2 ** shift;
    if (!(b & 0x80)) {
      return [value, at];
    }
  }
  return null;
}

// The bytes of the "cgdn1.plugin" custom section after its name, or null.
function customSection(bytes) {
  if (HEADER.some((b, i) => bytes[i] !== b)) {
    return null;
  }

  const name = new TextEncoder().encode(SECTION);
  let at = HEADER.length;

  while (at < bytes.length) {
    const id = bytes[at];
    const size = leb(bytes, at + 1);
    if (!size || size[1] + size[0] > bytes.length) {
      return null;
    }

    const [length, body] = size;
    at = body + length;
    if (id !== 0) {
      continue;
    }

    const nameLength = leb(bytes, body);
    if (!nameLength || nameLength[1] + nameLength[0] > at) {
      continue;
    }

    const [n, nameAt] = nameLength;
    const same = n === name.length && name.every((b, i) => bytes[nameAt + i] === b);
    if (same) {
      return bytes.subarray(nameAt + n, at);
    }
  }

  return null;
}

const isText = (v) => typeof v === "string" && v.length > 0;

// A text in each language, English required (plugin.json format 2).
const isTexts = (v) => v !== null && typeof v === "object" && isText(v.en);

// The module's plugin.json, or null when the file is not a module with one, or
// {refused: "old_sdk"} for one built for an older firmware (plugin.json format 1). Its texts
// stay per language for the caller to pick.
export function readModuleManifest(buffer) {
  const section = customSection(new Uint8Array(buffer));
  if (!section) {
    return null;
  }

  let meta;
  try {
    meta = JSON.parse(new TextDecoder().decode(section));
  } catch {
    return null;
  }

  if (typeof meta?.sdk === "number" && meta.sdk < 2) {
    return { refused: "old_sdk" };
  }

  // The fields the device requires (manifest.c), so a file it would refuse is refused
  // here, before the user is asked to confirm it; the device checks the rest.
  const hosts = Array.isArray(meta?.hosts) ? meta.hosts : null;
  const ok =
    ["id", "category", "version"].every((k) => isText(meta?.[k])) &&
    isTexts(meta?.name) &&
    isTexts(meta?.description) &&
    hosts !== null &&
    hosts.every(isText);
  if (!ok) {
    return null;
  }

  return {
    id: meta.id,
    name: meta.name,
    description: meta.description,
    version: meta.version,
    hosts,
  };
}
