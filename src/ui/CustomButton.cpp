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

void CustomButton::setRadius(int r) {
  m_radius = r;
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

void CustomButton::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Apply opacity
  painter.setOpacity(m_disabled ? m_opacity * 0.5 : m_opacity);

  // Draw Background
  if (m_bgColor.isValid()) {
    painter.setBrush(m_bgColor);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect(), m_radius, m_radius);
  }

  // Draw Image
  bool hasImage = !m_image.isNull();
  bool hasText = !m_text.isEmpty();

  if (hasImage) {
    if (hasText) {
      // Both: Image on top left
      QRect imgRect(0, 0, width() * 0.5, height() * 0.5);
      painter.drawImage(imgRect, m_image);
    } else {
      // Only image: Center
      int imgSize = qMin(width(), height()) * 0.8;
      QRect imgRect((width() - imgSize) / 2, (height() - imgSize) / 2, imgSize,
                    imgSize);
      painter.drawImage(imgRect, m_image);
    }
  }

  // Draw Text
  if (hasText) {
    painter.setPen(Qt::black);
    QFont font = painter.font();

    // Base font size: 80% of button height
    int fontSize = height() * 0.7;

    if (m_text.length() > 1) {
      // Reduce font size if more than one character
      fontSize = height() * 0.5;
      // Ensure it doesn't exceed width
      QFont tempFont = font;
      tempFont.setPixelSize(fontSize);
      QFontMetrics fm(tempFont);
      if (fm.horizontalAdvance(m_text) > width() * 0.9) {
        fontSize = fontSize * (width() * 0.9) / fm.horizontalAdvance(m_text);
      }
    }

    if (hasImage) {
      // Both: Text on bottom right
      // Use a smaller font for corner label (approx 40% of height)
      int cornerFontSize = height() * 0.4;
      font.setPixelSize(qMax(8, cornerFontSize));
      painter.setFont(font);
      painter.drawText(rect().adjusted(2, 2, -4, -4),
                       Qt::AlignBottom | Qt::AlignRight, m_text);
    } else {
      // Only text: Center
      font.setPixelSize(qMax(8, fontSize));
      painter.setFont(font);
      painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_text);
    }
  }

  // Draw Border
  painter.setBrush(Qt::NoBrush);
  painter.setPen(Qt::darkGray);
  painter.drawRoundedRect(0, 0, width() - 1, height() - 1, m_radius, m_radius);
}

void CustomButton::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    Q_EMIT clicked(m_id);
  }
  QWidget::mousePressEvent(event);
}
