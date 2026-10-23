import { AlertDialog, Button } from "@heroui/react";

// `confirm` is the action button; `trigger`, when given, is the control that opens the
// dialog.
export function ConfirmDialog({
  trigger,
  isOpen,
  onOpenChange,
  status,
  title,
  text,
  cancelLabel,
  confirm,
}) {
  return (
    <AlertDialog>
      {trigger}

      <AlertDialog.Backdrop isOpen={isOpen} onOpenChange={onOpenChange}>
        <AlertDialog.Container>
          <AlertDialog.Dialog className="sm:max-w-[400px]">
            <AlertDialog.Header>
              <AlertDialog.Icon status={status} />
              <AlertDialog.Heading>{title}</AlertDialog.Heading>
            </AlertDialog.Header>

            <AlertDialog.Body>
              <p>{text}</p>
            </AlertDialog.Body>

            <AlertDialog.Footer>
              <Button slot="close" variant="tertiary">
                {cancelLabel}
              </Button>
              {confirm}
            </AlertDialog.Footer>
          </AlertDialog.Dialog>
        </AlertDialog.Container>
      </AlertDialog.Backdrop>
    </AlertDialog>
  );
}
