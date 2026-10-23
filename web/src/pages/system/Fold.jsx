import { Button, Disclosure } from "@heroui/react";

// A button that unfolds a part of a card. Disclosure.Body pads 8px for focus rings;
// the negative margin lines its fields up with the trigger.
export function Fold({ label, variant, bodyClassName, children, ...props }) {
  return (
    <Disclosure {...props}>
      <Disclosure.Heading>
        <Button fullWidth slot="trigger" variant={variant}>
          {label}
          <Disclosure.Indicator />
        </Button>
      </Disclosure.Heading>

      <Disclosure.Content className="-mx-2">
        <Disclosure.Body className={bodyClassName}>{children}</Disclosure.Body>
      </Disclosure.Content>
    </Disclosure>
  );
}
