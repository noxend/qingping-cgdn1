import { Bars, Magnifier } from "@gravity-ui/icons";
import { Badge, Button, CloseButton, InputGroup, Kbd, TextField } from "@heroui/react";

import { useT } from "../i18n";

const HEADER_CLASS = [
  "sticky top-0 z-20 flex h-16 items-center gap-3 px-5 sm:px-8",
  "border-b border-separator bg-background/80 backdrop-blur",
].join(" ");

const MAC = /Mac|iPhone|iPad/.test(navigator.userAgent);

// HeroUI's InputGroup "With keyboard shortcut": the shortcut from SignedIn.jsx while the
// field is empty and the screen is wide enough to have a keyboard, a clear button once
// there is a query.
function SearchSuffix({ query, onClear }) {
  const t = useT();

  if (query) {
    return (
      <InputGroup.Suffix className="pe-2">
        <CloseButton aria-label={t("header.clear")} onPress={onClear} />
      </InputGroup.Suffix>
    );
  }

  return (
    <InputGroup.Suffix className="hidden pe-2 sm:flex">
      <Kbd>
        {MAC ? <Kbd.Abbr keyValue="command" /> : <Kbd.Content>Ctrl</Kbd.Content>}
        <Kbd.Content>K</Kbd.Content>
      </Kbd>
    </InputGroup.Suffix>
  );
}

export function Header({ query, onSearch, onMenu, searchRef, update }) {
  const t = useT();

  // The clear button goes away with the query, so focus goes back to the field.
  const clear = () => {
    onSearch("");
    searchRef.current?.focus();
  };

  const onKeyDown = (e) => {
    if (e.key === "Escape" && query) {
      clear();
    }
  };

  const menuLabel = update
    ? t("header.menu") + ". " + t("status.update", { version: update.version })
    : t("header.menu");

  return (
    <header className={HEADER_CLASS}>
      <Badge.Anchor className="lg:hidden">
        <Button isIconOnly variant="ghost" aria-label={menuLabel} onPress={onMenu}>
          <Bars />
        </Button>
        {update && <Badge color="danger" size="sm" />}
      </Badge.Anchor>

      <TextField
        aria-label={t("header.search")}
        className="w-full min-w-0 max-w-md"
        value={query}
        onChange={onSearch}
      >
        <InputGroup>
          <InputGroup.Prefix>
            <Magnifier className="size-4 text-muted" />
          </InputGroup.Prefix>
          <InputGroup.Input
            ref={searchRef}
            className="w-full"
            placeholder={t("header.searchPlaceholder")}
            onKeyDown={onKeyDown}
          />
          <SearchSuffix query={query} onClear={clear} />
        </InputGroup>
      </TextField>
    </header>
  );
}
