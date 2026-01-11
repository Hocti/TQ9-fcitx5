#include "FloatingWindow.h"
#include <QApplication>
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

  // Resize handle hint (bottom right)
  painter.setPen(Qt::black);
  painter.drawLine(width() - 10, height(), width(), height() - 10);
}

void FloatingWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragPosition =
        event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

void FloatingWindow::mouseMoveEvent(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
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
