import { Alert } from "@heroui/react";

export function Notice({ status, title, description, className, children }) {
  return (
    <Alert status={status} className={className}>
      <Alert.Indicator />
      <Alert.Content>
        <Alert.Title>{title}</Alert.Title>
        {description && <Alert.Description>{description}</Alert.Description>}
        {children}
      </Alert.Content>
    </Alert>
  );
}
