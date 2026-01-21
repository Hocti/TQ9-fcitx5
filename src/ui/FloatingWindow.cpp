#include "FloatingWindow.h"
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWindow>

// LayerShellQt for Wayland always-on-top (optional dependency)
#ifdef HAVE_LAYERSHELLQT
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>
#endif
#include <iostream>

// Helper function to detect if running on Wayland
static bool isWayland() {
  static bool checked = false;
  static bool wayland = false;
  if (!checked) {
    QString platform = QGuiApplication::platformName();
    wayland = platform.toLower().contains("wayland");
    checked = true;
    std::cerr << "[FloatingWindow] Platform: " << platform.toStdString()
              << " (Wayland=" << (wayland ? "true" : "false") << ")"
              << std::endl;
  }
  return wayland;
}

FloatingWindow::FloatingWindow(QWidget *parent)
    : QWidget(parent,
              Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setFocusPolicy(Qt::NoFocus);
  setMouseTracking(true); // Enable mouse tracking for hover cursor updates

  // On X11, we need additional flags for proper overlay behavior
  if (!isWayland()) {
    // X11: ensure the window stays on top and is not affected by window manager
    setAttribute(Qt::WA_X11NetWmWindowTypeUtility, true);
  }

  setMinimumSize(100, 100);
  // Position at top-right corner initially
  move(100, 100);
}

void FloatingWindow::setupLayerShell() {
  // Skip LayerShell setup on X11 - it's Wayland only
  if (!isWayland()) {
    std::cerr << "[FloatingWindow] X11 detected, skipping LayerShell setup"
              << std::endl;
    // On X11, just ensure the window is positioned correctly
    move(m_windowPosition);
    return;
  }

#ifdef HAVE_LAYERSHELLQT
  // Get the QWindow and configure LayerShellQt
  QWindow *win = windowHandle();
  if (!win) {
    // Create the native window handle first
    create();
    win = windowHandle();
  }

  if (win) {
    LayerShellQt::Window *layerWindow = LayerShellQt::Window::get(win);
    if (layerWindow) {
      // Set the window to the overlay layer (always on top)
      layerWindow->setLayer(LayerShellQt::Window::LayerTop);
      // Don't reserve any exclusive zone
      layerWindow->setExclusiveZone(0);
      // No keyboard interaction to avoid stealing focus
      layerWindow->setKeyboardInteractivity(
          LayerShellQt::Window::KeyboardInteractivityNone);
      // Anchor to top-left corner - we use margins to position
      layerWindow->setAnchors(LayerShellQt::Window::Anchors(
          LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
      // Initial margins will be set by updateLayerShellPosition()

      std::cerr << "[FloatingWindow] LayerShellQt configured for overlay layer"
                << std::endl;
    } else {
      std::cerr
          << "[FloatingWindow] Warning: Could not get LayerShellQt::Window"
          << std::endl;
    }
  }
#else
  std::cerr << "[FloatingWindow] LayerShellQt not available, using standard "
               "window mode"
            << std::endl;
  move(m_windowPosition);
#endif
}

void FloatingWindow::updateLayerShellPosition() {
  // On X11 or when LayerShellQt is not available, use standard Qt move()
  if (!isWayland()) {
    std::cerr << "[FloatingWindow] X11: move to: " << m_windowPosition.x()
              << "," << m_windowPosition.y() << std::endl;
    move(m_windowPosition);
    return;
  }

#ifdef HAVE_LAYERSHELLQT
  // Wayland path: use LayerShell margins for positioning
  QWindow *win = windowHandle();
  if (!win) {
    std::cerr << "[FloatingWindow] updateLayerShellPosition: no windowHandle"
              << std::endl;
    return;
  }

  LayerShellQt::Window *layerWindow = LayerShellQt::Window::get(win);
  if (layerWindow) {
    std::cerr << "[FloatingWindow] Setting LayerShell margins: left="
              << m_windowPosition.x() << " top=" << m_windowPosition.y()
              << std::endl;
    layerWindow->setMargins(
        QMargins(m_windowPosition.x(), m_windowPosition.y(), 0, 0));
    win->requestUpdate();
  } else {
    std::cerr << "[FloatingWindow] Warning: Could not get LayerShellQt::Window"
              << std::endl;
  }
#else
  // LayerShellQt not available, use standard move()
  move(m_windowPosition);
#endif
}

void FloatingWindow::initialize(const AppConfig &config) {
  m_baseConfig = config;
  m_windowPosition = QPoint(config.lastX, config.lastY);
  resize(config.windowWidth, config.windowHeight);

  // Create buttons (content will be set by pipe commands from engine)
  for (const auto &btnConf : config.buttons) {
    auto *btn = new CustomButton(btnConf.id, this);
    btn->setFocusPolicy(Qt::NoFocus);
    // Don't set any default text - content controlled by main.cpp's
    // initializeButtons()
    btn->setText("");
    btn->setBackgroundColor(Qt::white);
    btn->setRadius(btnConf.radius);
    connect(btn, &CustomButton::clicked, this, &FloatingWindow::buttonClicked);
    m_buttons.push_back(btn);
  }

  if (!m_statusLabel) {
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: black; background-color: rgba(255, "
                                 "255, 255, 150); border-radius: 5px;");
    m_statusLabel->setText("九万");
  }

  updateLayout();

  // Setup LayerShell after initialization
  setupLayerShell();
}

CustomButton *FloatingWindow::getButton(int id) {
  for (auto *btn : m_buttons) {
    if (btn->getId() == id)
      return btn;
  }
  return nullptr;
}

void FloatingWindow::reset() {
  // reset() is called when the engine sends RESET command
  // The actual button content reset is handled in main.cpp via
  // initializeButtons() Here we just ensure the window is in a clean state for
  // redraw
  update();
}

void FloatingWindow::showWindow() {
  // On Wayland with LayerShell, after hide() the native window surface becomes
  // invalid. We need to destroy and recreate it for the window to show
  // properly.
  if (m_wasHidden && isWayland()) {
    std::cerr
        << "[FloatingWindow] Wayland: Recreating window surface after hide"
        << std::endl;

    // Destroy the old window handle - this invalidates the Wayland surface
    if (windowHandle()) {
      windowHandle()->destroy();
    }

    // Create a new native window handle
    create();

    // Reconfigure LayerShell on the new surface
    setupLayerShell();
    updateLayerShellPosition();

    m_wasHidden = false;
  } else if (m_wasHidden && !isWayland()) {
    // On X11, just reset the flag - no need to recreate window
    std::cerr << "[FloatingWindow] X11: Showing window after hide" << std::endl;
    m_wasHidden = false;
  }

  // Now show and raise the window
  show();
  raise();

  // On X11, ensure window stays on top by raising it again after show
  if (!isWayland()) {
    // Use a timer to raise again after the window manager has processed the
    // show
    QTimer::singleShot(50, this, [this]() {
      raise();
      activateWindow();
    });
  }
}

void FloatingWindow::saveConfig() {
  if (m_baseConfig.configPath.isEmpty())
    return;

  // Update config with current window state
  // Position
  m_baseConfig.lastX = m_windowPosition.x();
  m_baseConfig.lastY = m_windowPosition.y();
  m_baseConfig.windowWidth = width();
  m_baseConfig.windowHeight = height();

  ConfigLoader::save(m_baseConfig.configPath, m_baseConfig);
  std::cerr << "[FloatingWindow] Configuration saved to "
            << m_baseConfig.configPath.toStdString() << std::endl;
}

void FloatingWindow::setStatusText(const QString &text) {
  if (m_statusLabel) {
    m_statusLabel->setText(text);
  }
}

void FloatingWindow::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Draw semi-transparent background for the window itself
  painter.setBrush(QColor(240, 240, 240, 200));
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(rect(), 10, 10);

  // Draw resize handles on corners and edges
  painter.setPen(QPen(Qt::darkGray, 2));
  int handleSize = 8;

  // Bottom-right corner (main resize handle)
  painter.drawLine(width() - handleSize, height(), width(),
                   height() - handleSize);
  painter.drawLine(width() - handleSize - 4, height(), width(),
                   height() - handleSize - 4);

  // You can add more visual hints for other edges if desired
}

void FloatingWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_resizeEdge = getResizeEdge(event->pos());
    if (m_resizeEdge != None) {
      m_isResizing = true;
      m_isDragging = false;
    } else {
      // For layer-shell, we track drag manually and update margins
      m_isResizing = false;
      m_isDragging = true;
      m_dragStartPos = event->globalPosition().toPoint();
      std::cerr << "[FloatingWindow] Drag started at: " << m_dragStartPos.x()
                << "," << m_dragStartPos.y() << std::endl;
      m_dragStartWindowPos = m_windowPosition;
    }
    event->accept();
  }
}

void FloatingWindow::mouseMoveEvent(QMouseEvent *event) {
  // Only update cursor based on hover position
  int edge = getResizeEdge(event->pos());
  updateCursor(edge);
}

void FloatingWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    QPoint globalPos = event->globalPosition().toPoint();

    if (m_isResizing) {
      // Fixed ratio resize based on config aspect ratio
      QRect geom = frameGeometry();

      // Calculate aspect ratio from config
      float aspectRatio =
          (float)m_baseConfig.windowWidth / m_baseConfig.windowHeight;

      int minW = m_baseConfig.minWidth;
      int maxW = m_baseConfig.maxWidth;
      int minH = (int)(minW / aspectRatio);
      int maxH = (int)(maxW / aspectRatio);

      int newWidth = geom.width();
      int newHeight = geom.height();
      int newLeft = geom.left();
      int newTop = geom.top();

      // Calculate new size based on which edge is being dragged
      if (m_resizeEdge & Right) {
        newWidth = globalPos.x() - geom.left();
      } else if (m_resizeEdge & Left) {
        newWidth = geom.right() - globalPos.x() + 1;
        newLeft = globalPos.x();
      }

      if (m_resizeEdge & Bottom) {
        newHeight = globalPos.y() - geom.top();
      } else if (m_resizeEdge & Top) {
        newHeight = geom.bottom() - globalPos.y() + 1;
        newTop = globalPos.y();
      }

      // Determine which dimension to use for maintaining aspect ratio
      // Use the larger change to drive the resize
      int targetWidth, targetHeight;
      if (m_resizeEdge & (Left | Right)) {
        // Width-driven resize
        targetWidth = qBound(minW, newWidth, maxW);
        targetHeight = (int)(targetWidth / aspectRatio);
      } else if (m_resizeEdge & (Top | Bottom)) {
        // Height-driven resize
        targetHeight = qBound(minH, newHeight, maxH);
        targetWidth = (int)(targetHeight * aspectRatio);
      } else {
        // Corner resize - use width
        targetWidth = qBound(minW, newWidth, maxW);
        targetHeight = (int)(targetWidth / aspectRatio);
      }

      // Adjust position for left/top edge resize
      if (m_resizeEdge & Left) {
        newLeft = geom.right() - targetWidth + 1;
      }
      if (m_resizeEdge & Top) {
        newTop = geom.bottom() - targetHeight + 1;
      }

      setGeometry(newLeft, newTop, targetWidth, targetHeight);
    } else if (m_isDragging) {
      // Calculate final position based on drag
      std::cerr << "[FloatingWindow] Drag ended at: " << globalPos.x() << ","
                << globalPos.y() << std::endl;

      // Update position with boundary checks
      ensureWithinScreen(m_dragStartWindowPos + (globalPos - m_dragStartPos));

      // Update layer shell margins
      updateLayerShellPosition();
    }

    // Reset state
    m_isResizing = false;
    m_isDragging = false;
    m_resizeEdge = None;
    updateCursor(getResizeEdge(event->pos()));
  }
  QWidget::mouseReleaseEvent(event);
}

int FloatingWindow::getResizeEdge(const QPoint &pos) const {
  // Only allow resize from bottom-right corner
  if (pos.x() >= width() - RESIZE_MARGIN &&
      pos.y() >= height() - RESIZE_MARGIN) {
    return Bottom | Right;
  }

  return None;
}

void FloatingWindow::updateCursor(int edge) {
  if (edge == None) {
    setCursor(Qt::ArrowCursor);
  } else if (edge == (Top | Left) || edge == (Bottom | Right)) {
    setCursor(Qt::SizeFDiagCursor);
  } else if (edge == (Top | Right) || edge == (Bottom | Left)) {
    setCursor(Qt::SizeBDiagCursor);
  } else if (edge & (Left | Right)) {
    setCursor(Qt::SizeHorCursor);
  } else if (edge & (Top | Bottom)) {
    setCursor(Qt::SizeVerCursor);
  }
}

void FloatingWindow::resizeEvent(QResizeEvent *event) {
  Q_UNUSED(event);
  updateLayout();
}

void FloatingWindow::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  if (isWayland()) {
    // Check bounds before configuring
    ensureWithinScreen();
    // Re-apply LayerShell configuration each time the window is shown
    // On Wayland, the surface may be destroyed when hidden, so we need to
    // reconfigure LayerShell properties
    setupLayerShell();
    updateLayerShellPosition();
    std::cerr << "[FloatingWindow] showEvent: LayerShell reconfigured"
              << std::endl;
  } else {
    // On X11, ensure position is correct and window stays on top
    ensureWithinScreen();
    move(m_windowPosition);
    std::cerr << "[FloatingWindow] showEvent: X11 window shown at "
              << m_windowPosition.x() << "," << m_windowPosition.y()
              << std::endl;
  }

  raise();
}

void FloatingWindow::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  // Mark that we've been hidden - need to recreate surface on next show
  m_wasHidden = true;
}

void FloatingWindow::ensureWithinScreen(std::optional<QPoint> point) {
  int w = width();
  int h = height();
  QScreen *screen = nullptr;
  QPoint center = point.value_or(m_windowPosition);
  int x = center.x();
  int y = center.y();

  screen = QGuiApplication::screenAt(center);
  if (!screen) {
    screen = QGuiApplication::screenAt(QPoint(x + w / 2, y + h / 2));
  }
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }

  if (screen) {
    std::cerr << "[FloatingWindow] center: " << center.x() << "," << center.y()
              << " screen pos: " << screen->geometry().x() << ","
              << screen->geometry().y()
              << " width: " << screen->geometry().width() << ","
              << " m_isDragging: " << m_isDragging << " QCursor "
              << QCursor::pos().x() << "," << QCursor::pos().y()
              << " QCursor+center " << (QCursor::pos() + center).x() << ","
              << (QCursor::pos() + center).y() << std::endl;

    QRect screenRect = screen->availableGeometry();
    // Ensure window is within screen bounds
    if (x + w > screenRect.x() + screenRect.width()) {
      x = screenRect.x() + screenRect.width() - w;
    }
    if (x < screenRect.x()) {
      x = screenRect.x();
    }
    if (y + h > screenRect.y() + screenRect.height()) {
      y = screenRect.y() + screenRect.height() - h;
    }
    if (y < screenRect.y()) {
      y = screenRect.y();
    }
    std::cerr << "[FloatingWindow] set new xy: " << x << "," << y
              << " old xy: " << m_windowPosition.x() << ","
              << m_windowPosition.y() << std::endl;

    m_windowPosition.setX(x);
    m_windowPosition.setY(y);
  }
}

void FloatingWindow::updateLayout() {
  if (m_baseConfig.windowWidth == 0 || m_baseConfig.windowHeight == 0)
    return;

  float scaleX = (float)width() / m_baseConfig.windowWidth;
  float scaleY = (float)height() / m_baseConfig.windowHeight;

  for (size_t i = 0; i < m_buttons.size(); ++i) {
    if (i < m_baseConfig.buttons.size()) {
      const auto &conf = m_baseConfig.buttons[i].rect;
      int x = conf.x() * scaleX;
      int y = conf.y() * scaleY;
      int w = conf.width() * scaleX;
      int h = conf.height() * scaleY;
      m_buttons[i]->setGeometry(x, y, w, h);
      m_buttons[i]->setRadius(m_baseConfig.buttons[i].radius *
                              (scaleX + scaleY) / 2.0);
    }
  }

  if (m_statusLabel) {
    int x = m_baseConfig.statusRect.x() * scaleX;
    int y = m_baseConfig.statusRect.y() * scaleY;
    int w = m_baseConfig.statusRect.width() * scaleX;
    int h = m_baseConfig.statusRect.height() * scaleY;
    m_statusLabel->setGeometry(x, y, w, h);

    QFont font = m_statusLabel->font();
    int pixelSize = 14 * (scaleX + scaleY) / 2.0;
    if (pixelSize < 8)
      pixelSize = 8;
    font.setPixelSize(pixelSize);
    m_statusLabel->setFont(font);
  }
}
