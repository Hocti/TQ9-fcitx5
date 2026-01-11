#include "CustomButton.h"
#include <QDebug>
#include <QMouseEvent>
#include <QPainter>

CustomButton::CustomButton(int id, QWidget *parent)
    : QWidget(parent), m_id(id), m_bgColor(Qt::lightGray), m_opacity(1.0),
      m_disabled(false) {}

void CustomButton::setText(const QString &text) {
  m_text = text;
  // Font size will be calculated in paintEvent based on text length
  update();
}

void CustomButton::setImage(const QString &imagePath) {
  if (imagePath.isEmpty()) {
    m_image = QImage();
    update();
    return;
  }
  if (!m_image.load(imagePath)) {
    qWarning() << "Failed to load image:" << imagePath;
  }
  update();
}

void CustomButton::setBackgroundColor(const QColor &color) {
  m_bgColor = color;
  update();
}

void CustomButton::setOpacity(qreal opacity) {
  m_opacity = opacity;
  update();
}

void CustomButton::setDisabledState(bool disabled) {
  m_disabled = disabled;
  update();
}

void CustomButton::setSmallText(bool small) {
  m_smallText = small;
  update();
}

void CustomButton::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Apply opacity
  painter.setOpacity(m_disabled ? m_opacity * 0.5 : m_opacity);

  // Draw Background
  if (m_bgColor.isValid()) {
    painter.fillRect(rect(), m_bgColor);
  }

  // Draw Image (Top-Left, scaled to 50% of size approx)
  if (!m_image.isNull()) {
    QRect imgRect(0, 0, width() * 0.6, height() * 0.6);
    painter.drawImage(imgRect, m_image);
  }

  // Draw Text
  if (!m_text.isEmpty()) {
    painter.setPen(Qt::black);
    QFont font = painter.font();

    // Calculate font size based on text length: 64 / text-length
    int textLen = m_text.length();
    int fontSize = (textLen > 0) ? (64 / textLen) : 64;
    fontSize = qMax(8, fontSize); // Minimum font size

    if (m_smallText) {
      // Small text in bottom-right corner
      font.setPixelSize(height() / 3);
      painter.setFont(font);
      QRect textRect(width() * 0.2, height() * 0.4, width() * 0.8,
                     height() * 0.6);
      painter.drawText(textRect,
                       Qt::AlignBottom | Qt::AlignRight | Qt::TextWordWrap,
                       m_text);
    } else {
      // Larger text centered in button
      font.setPixelSize(fontSize);
      painter.setFont(font);
      painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_text);
    }
  }

  // Draw Border
  painter.setPen(Qt::darkGray);
  painter.drawRect(0, 0, width() - 1, height() - 1);
}

void CustomButton::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    Q_EMIT clicked(m_id);
  }
  QWidget::mousePressEvent(event);
}
