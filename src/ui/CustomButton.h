#pragma once

#include <QImage>
#include <QLabel>
#include <QWidget>

class CustomButton : public QWidget {
  Q_OBJECT

public:
  explicit CustomButton(int id, QWidget *parent = nullptr);

  void setText(const QString &text);
  void setImage(const QString &imagePath);
  void setBackgroundColor(const QColor &color);
  void setOpacity(qreal opacity);
  void setDisabledState(bool disabled); // Changes brightness/opacity
  void setSmallText(bool small);        // If true, text is smaller and in
                                 // bottom-right; if false, larger and centered

  int getId() const { return m_id; }

Q_SIGNALS:
  void clicked(int id);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;

private:
  int m_id;
  QString m_text;
  QImage m_image;
  QColor m_bgColor;
  qreal m_opacity;
  bool m_disabled;
  bool m_smallText = true; // Default: text is small and in bottom-right
};
