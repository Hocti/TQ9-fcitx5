#pragma once

#include "../ConfigLoader.h"
#include "CustomButton.h"
#include <QWidget>
#include <memory>
#include <vector>

class FloatingWindow : public QWidget {
  Q_OBJECT

public:
  explicit FloatingWindow(QWidget *parent = nullptr);
  void initialize(const AppConfig &config);
  CustomButton *getButton(int id);
  void reset();

Q_SIGNALS:
  void buttonClicked(int id);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  AppConfig m_baseConfig;
  std::vector<CustomButton *> m_buttons;
  QPoint m_dragPosition;

  void updateLayout();
};
