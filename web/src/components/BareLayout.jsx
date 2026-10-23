import { LanguageSwitch } from "./LanguageSwitch";

// The page before sign-in: the language and one card, without the menu.
export function BareLayout({ children }) {
  return (
    <div className="flex min-h-screen flex-col">
      <header className="flex h-16 items-center justify-end px-5 sm:px-8">
        <LanguageSwitch />
      </header>

      <main className="mx-auto w-full max-w-md px-5 pb-10 pt-8 sm:pt-16">{children}</main>
    </div>
  );
}
