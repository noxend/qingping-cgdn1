import { createContext, useContext } from "react";

import { en } from "./en";
import { uk } from "./uk";

const DICT = { uk, en };

export const LANGS = Object.keys(DICT);

// The BCP 47 tag HeroUI's I18nProvider takes for each language.
export const LOCALES = { uk: "uk-UA", en: "en-US" };

export const entry = (lang, key) => DICT[lang][key] ?? DICT.en[key];

// A key neither language has renders as the key, so a miss is visible and never blank.
export function translate(lang, key, vars) {
  const text = entry(lang, key) ?? key;
  if (!vars) {
    return text;
  }

  return text.replace(/\{(\w+)\}/g, (match, name) => (name in vars ? vars[name] : match));
}

export const LangContext = createContext(null);

export const useLang = () => useContext(LangContext);

export const useT = () => useContext(LangContext).t;
