import { Description, Label, Switch } from "@heroui/react";

export function SwitchField({ label, description, ...props }) {
  return (
    <Switch {...props}>
      <Switch.Content>
        <Switch.Control>
          <Switch.Thumb />
        </Switch.Control>
        <Label>{label}</Label>
      </Switch.Content>
      {description && <Description>{description}</Description>}
    </Switch>
  );
}
