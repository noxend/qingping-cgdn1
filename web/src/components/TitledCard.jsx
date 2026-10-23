import { Card } from "@heroui/react";

export function TitledCard({ title, description, footer, className, children }) {
  return (
    <Card className={className}>
      <Card.Header>
        <Card.Title>{title}</Card.Title>
        {description && <Card.Description>{description}</Card.Description>}
      </Card.Header>

      <Card.Content>{children}</Card.Content>

      {footer && <Card.Footer>{footer}</Card.Footer>}
    </Card>
  );
}
