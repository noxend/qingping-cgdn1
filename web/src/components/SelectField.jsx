import { Description, Label, ListBox, Select } from "@heroui/react";

// options: [{ key, label }]
export function SelectField({ label, description, options, value, onChange, isDisabled }) {
  return (
    <Select fullWidth isDisabled={isDisabled} variant="secondary" value={value} onChange={onChange}>
      <Label>{label}</Label>
      <Select.Trigger>
        <Select.Value />
        <Select.Indicator />
      </Select.Trigger>
      {description && <Description>{description}</Description>}
      <Select.Popover>
        <ListBox>
          {options.map((option) => (
            <ListBox.Item key={option.key} id={option.key} textValue={option.label}>
              {option.label}
              <ListBox.ItemIndicator />
            </ListBox.Item>
          ))}
        </ListBox>
      </Select.Popover>
    </Select>
  );
}
