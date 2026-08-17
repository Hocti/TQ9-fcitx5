#pragma once

class QWidget;

// main() switches this process to Wayland layer-shell, which by default leaves
// ordinary dialogs unable to take keyboard focus (and sometimes invisible).
// Call this on any dialog before showing it. No-op on X11.
void configureDialogForLayerShell(QWidget *dialog);
