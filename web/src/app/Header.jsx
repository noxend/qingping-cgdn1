import { ArrowUpFromLine, Bars } from "@gravity-ui/icons";
import { Button, Chip, Link, SearchField } from "@heroui/react";

import { LanguageSwitch } from "../components/LanguageSwitch";
import { useT } from "../i18n";

const HEADER_CLASS = [
  "sticky top-0 z-20 flex h-16 items-center gap-3 px-5 sm:px-8",
  "border-b border-separator bg-background/80 backdrop-blur",
].join(" ");

function UpdateChip({ version }) {
  const t = useT();

  return (
    <Link
      href="#/system"
      className="shrink-0 no-underline"
      aria-label={t("header.update", { version })}
    >
      <Chip color="accent" size="sm" variant="soft">
        <ArrowUpFromLine width={12} />
        <Chip.Label>
          <span className="hidden sm:inline">{t("header.updateShort")} </span>v{version}
        </Chip.Label>
      </Chip>
    </Link>
  );
}

export function Header({ query, onSearch, onMenu, searchRef, update }) {
  const t = useT();

  return (
    <header className={HEADER_CLASS}>
      <Button
        isIconOnly
        variant="ghost"
        className="lg:hidden"
        aria-label={t("header.menu")}
        onPress={onMenu}
      >
        <Bars />
      </Button>

      <SearchField
        aria-label={t("header.search")}
        className="w-full min-w-0 max-w-md"
        value={query}
        onChange={onSearch}
      >
        <SearchField.Group>
          <SearchField.SearchIcon />
          <SearchField.Input ref={searchRef} placeholder={t("header.searchPlaceholder")} />
          <SearchField.ClearButton />
        </SearchField.Group>
      </SearchField>

      <div className="ms-auto flex shrink-0 items-center gap-3">
        {update && <UpdateChip version={update.version} />}
        <LanguageSwitch />
      </div>
    </header>
  );
}
