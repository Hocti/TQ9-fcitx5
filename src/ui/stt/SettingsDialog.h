#pragma once

#include "SttSettings.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

// The 設定 window opened from the top bar.
class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(QWidget *parent = nullptr);

  // Settings as saved by the user (valid after exec() returns Accepted).
  const SttSettings &settings() const { return m_settings; }

private:
  void loadIntoWidgets();
  void applyFromWidgets();
  void refreshCostLabels();
  void resetBilling();

  SttSettings m_settings;

  QCheckBox *m_enabled = nullptr;
  QLineEdit *m_apiKey = nullptr;
  QLineEdit *m_speechLanguage = nullptr;
  // Radio buttons rather than a combo box: under the process-wide Wayland
  // layer-shell mode a popup window gets stretched to fill the screen.
  QButtonGroup *m_translateMode = nullptr;
  QLineEdit *m_translateLanguage = nullptr;
  QDoubleSpinBox *m_alertEvery = nullptr;
  QLineEdit *m_model = nullptr;
  QDoubleSpinBox *m_priceTextInput = nullptr;
  QDoubleSpinBox *m_priceAudioInput = nullptr;
  QDoubleSpinBox *m_priceOutput = nullptr;
  QLabel *m_costLabel = nullptr;
  QLabel *m_promptPathLabel = nullptr;
};
