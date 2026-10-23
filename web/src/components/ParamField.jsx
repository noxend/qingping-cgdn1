import { memo, useCallback } from "react";
import { Input, Label, NumberField, TextField } from "@heroui/react";

import { SelectField } from "./SelectField";
import { SwitchField } from "./SwitchField";

// One plugin setting, built from the plugin's parameter schema. Memoised so the page's
// periodic refreshes (screen preview, live status) skip the form.
export const ParamField = memo(function ParamField({ sid, p, value, setVal }) {
  const onChange = useCallback((next) => setVal(sid, p.key, next), [sid, p.key, setVal]);

  if (p.type === "select") {
    return (
      <SelectField
        label={p.label}
        options={p.options}
        value={value || null}
        onChange={(key) => onChange(key ?? value)}
      />
    );
  }

  if (p.type === "toggle") {
    return (
      <SwitchField
        label={p.label}
        isSelected={value === "1"}
        onChange={(on) => onChange(on ? "1" : "0")}
      />
    );
  }

  if (p.type === "number") {
    const number = value === "" ? NaN : Number(value);

    return (
      <NumberField
        fullWidth
        variant="secondary"
        value={number}
        onChange={(next) => onChange(Number.isNaN(next) ? "" : String(next))}
      >
        <Label>{p.label}</Label>
        <NumberField.Group>
          <NumberField.DecrementButton />
          <NumberField.Input />
          <NumberField.IncrementButton />
        </NumberField.Group>
      </NumberField>
    );
  }

  return (
    <TextField fullWidth variant="secondary">
      <Label>{p.label}</Label>
      <Input value={value} onChange={(e) => onChange(e.target.value)} />
    </TextField>
  );
});
