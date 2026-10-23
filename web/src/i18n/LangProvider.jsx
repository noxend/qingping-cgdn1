import { useCallback, useEffect, useMemo, useState } from "react";
import { I18nProvider } from "@heroui/react";

import { entry, LangContext, LANGS, LOCALES, translate } from "./index";

const STORAGE_KEY = "cgdn1-lang";

function initialLang() {
  try {
    const stored = localStorage.getItem(STORAGE_KEY);
    if (LANGS.includes(stored)) {
      return stored;
    }
  } catch {
    // Storage blocked: English.
  }

  return "en";
}

// HeroUI's own labels (the search clear button, number steppers) follow the language
// chosen on the page, not the browser's.
export function LangProvider({ children }) {
  const [lang, setStoredLang] = useState(initialLang);

  const setLang = useCallback((next) => {
    try {
      localStorage.setItem(STORAGE_KEY, next);
    } catch {
      // Storage blocked: the choice lasts for this page.
    }
    setStoredLang(next);
  }, []);

  useEffect(() => {
    document.documentElement.lang = lang;
  }, [lang]);

  const value = useMemo(
    () => ({
      lang,
      setLang,
      t: (key, vars) => translate(lang, key, vars),
      entry: (key) => entry(lang, key),
    }),
    [lang, setLang],
  );

  return (
    <LangContext.Provider value={value}>
      <I18nProvider locale={LOCALES[lang]}>{children}</I18nProvider>
    </LangContext.Provider>
  );
}
