#include "FloatingWindow.h"
#include <QApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWindow>

// LayerShellQt for Wayland always-on-top
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>

FloatingWindow::FloatingWindow(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setFocusPolicy(Qt::NoFocus);
  setMouseTracking(true); // Enable mouse tracking for hover cursor updates

  setMinimumSize(100, 100);
  // Position at top-right corner initially
  move(100, 100);
}

void FloatingWindow::setupLayerShell() {
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
      layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
      // Don't reserve any exclusive zone
      layerWindow->setExclusiveZone(0);
      // No keyboard interaction to avoid stealing focus
      layerWindow->setKeyboardInteractivity(
          LayerShellQt::Window::KeyboardInteractivityNone);
      // No anchors - floating window
      layerWindow->setAnchors(LayerShellQt::Window::AnchorNone);

      std::cerr << "[FloatingWindow] LayerShellQt configured for overlay layer"
                << std::endl;
    } else {
      std::cerr
          << "[FloatingWindow] Warning: Could not get LayerShellQt::Window"
          << std::endl;
    }
  }
}

void FloatingWindow::initialize(const AppConfig &config) {
  m_baseConfig = config;
  resize(config.windowWidth, config.windowHeight);

  // Create buttons
  for (const auto &btnConf : config.buttons) {
    auto *btn = new CustomButton(btnConf.id, this);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setText(QString::number(btnConf.id)); // Default text
    connect(btn, &CustomButton::clicked, this, &FloatingWindow::buttonClicked);
    m_buttons.push_back(btn);
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
  for (auto *btn : m_buttons) {
    btn->setText(QString::number(btn->getId()));
    btn->setBackgroundColor(Qt::lightGray);
    btn->setImage("");
    btn->setDisabledState(false);
  }
  update();
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
      m_isResizing = false;
      m_isDragging = true;
      m_dragPosition =
          event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    event->accept();
  }
}

void FloatingWindow::mouseMoveEvent(QMouseEvent *event) {
  if (!(event->buttons() & Qt::LeftButton)) {
    // Just hovering - update cursor based on position
    int edge = getResizeEdge(event->pos());
    updateCursor(edge);
    return;
  }

  if (m_isResizing) {
    // Fixed ratio resize based on config aspect ratio
    QPoint globalPos = event->globalPosition().toPoint();
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
    event->accept();
  } else {
    // Drag the window
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
  }
}

void FloatingWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isResizing = false;
    m_isDragging = false;
    m_resizeEdge = None;
    updateCursor(getResizeEdge(event->pos()));
  }
  QWidget::mouseReleaseEvent(event);
}

int FloatingWindow::getResizeEdge(const QPoint &pos) const {
  int edge = None;

  if (pos.x() <= RESIZE_MARGIN) {
    edge |= Left;
  } else if (pos.x() >= width() - RESIZE_MARGIN) {
    edge |= Right;
  }

  if (pos.y() <= RESIZE_MARGIN) {
    edge |= Top;
  } else if (pos.y() >= height() - RESIZE_MARGIN) {
    edge |= Bottom;
  }

  return edge;
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
  raise();
}

void FloatingWindow::hideEvent(QHideEvent *event) { QWidget::hideEvent(event); }

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
    }
  }
}
