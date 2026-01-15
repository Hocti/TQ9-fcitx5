#ifndef CUSTOMBUTTON_H
#define CUSTOMBUTTON_H

#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>

class CustomButton : public QWidget {
  Q_OBJECT
public:
  explicit CustomButton(int id, QWidget *parent = nullptr);

  void setText(const QString &text);
  void setImage(const QString &imagePath);
  void setBackgroundColor(const QColor &color);
  void setRadius(int r);
  void setOpacity(qreal opacity);
  void setDisabledState(bool disabled);

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
  int m_radius = 0;
  qreal m_opacity = 1.0;
  bool m_disabled = false;
};

#endif // CUSTOMBUTTON_H
