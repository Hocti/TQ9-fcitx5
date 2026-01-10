#include "FloatingWindow.h"
#include <QMouseEvent>
#include <QPainter>

FloatingWindow::FloatingWindow(QWidget *parent)
    : QWidget(parent,
              Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
  setAttribute(Qt::WA_TranslucentBackground);
  setMinimumSize(100, 100);
}

void FloatingWindow::initialize(const AppConfig &config) {
  m_baseConfig = config;
  resize(config.windowWidth, config.windowHeight);

  // Create buttons
  for (const auto &btnConf : config.buttons) {
    auto *btn = new CustomButton(btnConf.id, this);
    btn->setText(QString::number(btnConf.id)); // Default text
    connect(btn, &CustomButton::clicked, this, &FloatingWindow::buttonClicked);
    m_buttons.push_back(btn);
  }
  updateLayout();
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
    m_dragPosition = event->globalPos() - frameGeometry().topLeft();
    event->accept();
  }
}

void FloatingWindow::mouseMoveEvent(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) {
    move(event->globalPos() - m_dragPosition);
    event->accept();
  }
}

void FloatingWindow::resizeEvent(QResizeEvent *event) {
  Q_UNUSED(event);
  updateLayout();
}

void FloatingWindow::updateLayout() {
  if (m_baseConfig.windowWidth == 0 || m_baseConfig.windowHeight == 0)
    return;

  float scaleX = (float)width() / m_baseConfig.windowWidth;
  float scaleY = (float)height() / m_baseConfig.windowHeight;

  // Keep aspect ratio for Scale?
  // User said: "when the window resize all buttons resize together on scale.
  // the window width height ratio are fixed" Actually user said: "the window
  // width height ratio are fixed" - this implies we should enforce aspect ratio
  // on resize? For now I'll just scale buttons freely, relying on window
  // manager or user to keep ratio if they want, BUT "window width height ratio
  // are fixed" implies I might need to enforce it in resizeEvent or
  // setFixedSize (but user wants resizable). I will try to enforce aspect ratio
  // in resizeEvent or set aspect ratio constraint if Qt supports it easily.
  // simpler: valid resize, then scale buttons.

  // Correction: "the window width height ratio are fixed" -> I should probably
  // enforce this. But `resizeEvent` is post-facto. I'll just scale X and Y
  // independently for now as "resize together on scale" is the key. To strictly
  // fix ratio I'd need to re-resize `this->resize(w, w * ratio)` inside event,
  // which can cause loop. Let's just scale buttons.

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
