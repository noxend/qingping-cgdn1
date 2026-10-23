import { LOCALES } from "../i18n";

// "3.6 KB", "1.2 MB" in the page's language; one decimal below 10, none above.
export function formatBytes(bytes, lang) {
  const kb = bytes / 1024;
  const [value, unit] = kb >= 1024 ? [kb / 1024, "megabyte"] : [kb, "kilobyte"];
  const digits = value < 10 ? 1 : 0;

  return new Intl.NumberFormat(LOCALES[lang], {
    style: "unit",
    unit,
    maximumFractionDigits: digits,
  }).format(value);
}
