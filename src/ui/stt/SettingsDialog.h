#pragma once

#include "ConfigLoader.h"
#include "SttSettings.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

// The 設定 window opened from the top bar.
class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(QWidget *parent = nullptr);

  // Settings as saved by the user (valid after exec() returns Accepted).
  const SttSettings &settings() const { return m_settings; }

  // 選字 and 長按 are not STT settings - they live in config.json and are only
  // edited here, so they are handed in before exec() and read back after.
  void setInputConfig(const InputConfig &cfg);
  InputConfig inputConfig() const;

private:
  void loadIntoWidgets();
  void applyFromWidgets();
  void refreshCostLabels();
  void resetBilling();

  // Saved model/price sets. The list is the authority on what exists; the
  // name and price fields below it edit whichever one is selected.
  void refreshModelList(const QString &select);
  void loadModelIntoWidgets(const QString &name);
  void storeCurrentModel();
  void deleteCurrentModel();

  void openRecordingsFolder();

  SttSettings m_settings;
  // Everything the dialog does not show is carried through untouched.
  InputConfig m_input;

  QCheckBox *m_freqOrder = nullptr;
  QCheckBox *m_holdHomo = nullptr;
  QCheckBox *m_holdOpenClose = nullptr;
  QCheckBox *m_holdShortcut = nullptr;
  QDoubleSpinBox *m_holdKey = nullptr;
  QButtonGroup *m_cancelHold = nullptr;
  QCheckBox *m_enabled = nullptr;
  QLineEdit *m_apiKey = nullptr;
  QLineEdit *m_speechLanguage = nullptr;
  QLineEdit *m_outputLanguage = nullptr;
  QPlainTextEdit *m_vocabulary = nullptr;
  // Radio buttons rather than a combo box: under the process-wide Wayland
  // layer-shell mode a popup window gets stretched to fill the screen.
  QButtonGroup *m_translateMode = nullptr;
  QLineEdit *m_translateLanguage = nullptr;
  QButtonGroup *m_translateInsert = nullptr;
  QDoubleSpinBox *m_holdThreshold = nullptr;
  QCheckBox *m_saveRecordings = nullptr;
  QDoubleSpinBox *m_alertEvery = nullptr;
  QListWidget *m_modelList = nullptr;
  QLineEdit *m_model = nullptr;
  QDoubleSpinBox *m_priceInput = nullptr;
  QDoubleSpinBox *m_priceOutput = nullptr;
  QLabel *m_costLabel = nullptr;
  QLabel *m_promptPathLabel = nullptr;
};
