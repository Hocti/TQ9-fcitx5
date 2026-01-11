#pragma once

#include "../ConfigLoader.h"
#include "CustomButton.h"
#include <QWidget>
#include <iostream>
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
  void mouseReleaseEvent(QMouseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  AppConfig m_baseConfig;
  std::vector<CustomButton *> m_buttons;
  QPoint m_dragPosition;

  // Resize handling
  enum ResizeEdge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8 };
  int m_resizeEdge = None;
  bool m_isResizing = false;
  bool m_isDragging = false;
  static const int RESIZE_MARGIN = 10;

  void updateLayout();
  void setupLayerShell();
  int getResizeEdge(const QPoint &pos) const;
  void updateCursor(int edge);
};
