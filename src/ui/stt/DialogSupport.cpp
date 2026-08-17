#include "DialogSupport.h"

#include <QGuiApplication>
#include <QWidget>
#include <QWindow>

#ifdef HAVE_LAYERSHELLQT
#include <LayerShellQt/Window>
#endif

void configureDialogForLayerShell(QWidget *dialog) {
#ifdef HAVE_LAYERSHELLQT
  if (!dialog)
    return;
  if (!QGuiApplication::platformName().toLower().contains(
          QStringLiteral("wayland")))
    return;

  // Force the native window into existence so LayerShellQt can attach to it
  // before the surface is committed on show().
  dialog->winId();
  QWindow *win = dialog->windowHandle();
  if (!win)
    return;

  if (LayerShellQt::Window *layer = LayerShellQt::Window::get(win)) {
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityExclusive);
    // No anchors - the compositor centres the surface.
    layer->setAnchors({});
    layer->setExclusiveZone(0);
    // Without an explicit size a layer surface is stretched to fill the whole
    // output, which is how dialogs end up fullscreen.
    QSize wanted = dialog->sizeHint().expandedTo(dialog->minimumSize());
    if (wanted.isValid() && !wanted.isEmpty()) {
      layer->setDesiredSize(wanted);
      dialog->resize(wanted);
    }
  }
#else
  Q_UNUSED(dialog);
#endif
}
