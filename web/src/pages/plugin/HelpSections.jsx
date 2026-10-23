import { Card } from "@heroui/react";

import { SectionHeader } from "../../components/SectionHeader";
import { useT } from "../../i18n";

function TextSection({ title, text }) {
  return (
    <section>
      <SectionHeader title={title} />
      <p className="max-w-2xl leading-relaxed text-muted">{text}</p>
    </section>
  );
}

function Features({ features }) {
  const t = useT();

  return (
    <section>
      <SectionHeader title={t("plugin.features")} />
      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2">
        {features.map(([title, text]) => (
          <Card key={title} className="p-5">
            <Card.Content>
              <p className="font-medium">{title}</p>
              <p className="mt-1.5 text-sm leading-relaxed text-muted">{text}</p>
            </Card.Content>
          </Card>
        ))}
      </div>
    </section>
  );
}

// The dictionary's help for a plugin: what it is, what it does, how to use it.
export function HelpSections({ help }) {
  const t = useT();

  return (
    <>
      {help.about && <TextSection title={t("plugin.about")} text={help.about} />}
      {help.features && <Features features={help.features} />}
      {help.howto && <TextSection title={t("plugin.howto")} text={help.howto} />}
    </>
  );
}
