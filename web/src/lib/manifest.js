// The categories a manifest may name (manifest.c); the dictionaries word each as
// "category.<key>".
export const CATEGORIES = [
  "climate",
  "time",
  "weather",
  "security",
  "finance",
  "transport",
  "news",
  "home",
  "fun",
  "tools",
  "examples",
  "other",
];

// A text in each language ({"en": "...", "uk": "..."}) in this one, else in English.
export const textIn = (text, lang) => text?.[lang] || text?.en || "";

// A plugin as GET /config gives it (its manifest, its saved settings, how it runs) in
// the shape the pages use, its texts in the page's language.
export function localize(entry, lang) {
  const m = entry.manifest ?? {};
  const values = entry.values ?? {};

  return {
    id: entry.id,
    // A standard plugin that was deleted: the store offers it again.
    installed: entry.installed !== false,
    state: entry.state,
    error: entry.error ?? "",
    active: Boolean(entry.active),
    disabled: Boolean(entry.disabled),
    name: textIn(m.name, lang) || entry.id,
    description: textIn(m.description, lang),
    // A category this page has no word for (a newer firmware's) goes under "other".
    category: CATEGORIES.includes(m.category) ? m.category : "other",
    version: m.version ?? "",
    params: (m.params ?? []).map((p) => param(p, values[p.key], lang)),
  };
}

// A setting with its saved value. A select value that is no longer one of its options (a
// newer version renamed them) reads as the default, which the device takes on save.
function param(p, saved, lang) {
  const options = (p.options ?? []).map((o) => ({
    key: o.key,
    label: textIn(o.label, lang),
  }));
  const stale = p.type === "select" && !options.some((o) => o.key === saved);

  return {
    key: p.key,
    type: p.type,
    label: textIn(p.label, lang),
    options,
    value: saved === undefined || stale ? p.def : saved,
  };
}

// The label of a select setting's option, in the page's language, or the key itself.
export function optionLabel(plugin, key, value) {
  const param = plugin?.params.find((p) => p.key === key);
  const option = param?.options.find((o) => o.key === value);
  return option?.label ?? value;
}
