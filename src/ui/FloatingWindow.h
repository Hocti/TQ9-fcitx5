#pragma once

#include "../ConfigLoader.h"
#include "CustomButton.h"
#include <QMargins>
#include <QWidget>
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

  // Resize handling
  enum ResizeEdge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8 };
  int m_resizeEdge = None;
  bool m_isResizing = false;
  bool m_isDragging = false;
  static const int RESIZE_MARGIN = 10;

  // Layer shell positioning (using margins from top-left anchor)
  QPoint m_windowPosition{100, 100}; // Current position as margins
  QPoint m_dragStartPos;             // Mouse position at drag start
  QPoint m_dragStartWindowPos;       // Window position at drag start

  void updateLayout();
  void setupLayerShell();
  void updateLayerShellPosition(); // Update margins for layer shell
  int getResizeEdge(const QPoint &pos) const;
  void updateCursor(int edge);
};
