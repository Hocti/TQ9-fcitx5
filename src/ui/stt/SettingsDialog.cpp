#include "SettingsDialog.h"
#include "DialogSupport.h"
#include "SttUsageLog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("九万 - 語音輸入設定"));
  setModal(false);

  auto *root = new QVBoxLayout(this);
  auto *form = new QFormLayout();

  m_enabled = new QCheckBox(QStringLiteral("啟用語音輸入 (STT)"), this);
  form->addRow(m_enabled);

  m_apiKey = new QLineEdit(this);
  m_apiKey->setEchoMode(QLineEdit::Password);
  m_apiKey->setPlaceholderText(QStringLiteral("AIza..."));
  auto *showKey = new QPushButton(QStringLiteral("顯示"), this);
  showKey->setCheckable(true);
  showKey->setFixedWidth(56);
  connect(showKey, &QPushButton::toggled, this, [this](bool on) {
    m_apiKey->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
  });
  auto *keyRow = new QHBoxLayout();
  keyRow->addWidget(m_apiKey);
  keyRow->addWidget(showKey);
  form->addRow(QStringLiteral("Gemini API Key"), keyRow);

  m_speechLanguage = new QLineEdit(this);
  form->addRow(QStringLiteral("聲音輸入語言"), m_speechLanguage);

  m_translateMode = new QButtonGroup(this);
  auto *modeRow = new QHBoxLayout();
  const struct {
    const char *label;
    TranslateMode mode;
  } modes[] = {
      {"關", TranslateMode::Off},
      {"開", TranslateMode::Only},
      {"同時輸出雙語", TranslateMode::Both},
  };
  for (const auto &entry : modes) {
    auto *radio = new QRadioButton(QString::fromUtf8(entry.label), this);
    m_translateMode->addButton(radio, static_cast<int>(entry.mode));
    modeRow->addWidget(radio);
  }
  modeRow->addStretch();

  m_translateLanguage = new QLineEdit(this);

  auto *translateColumn = new QVBoxLayout();
  translateColumn->addLayout(modeRow);
  translateColumn->addWidget(m_translateLanguage);
  form->addRow(QStringLiteral("翻譯"), translateColumn);

  m_alertEvery = new QDoubleSpinBox(this);
  m_alertEvery->setRange(0.0, 10000.0);
  m_alertEvery->setDecimals(2);
  m_alertEvery->setSingleStep(1.0);
  m_alertEvery->setSuffix(QStringLiteral(" USD"));
  form->addRow(QStringLiteral("費用提示間隔"), m_alertEvery);

  auto *alertHint = new QLabel(
      QStringLiteral("累計費用每超過此金額提示一次，設為 0 即不提示"), this);
  alertHint->setStyleSheet(QStringLiteral("color: gray;"));
  form->addRow(QString(), alertHint);

  m_model = new QLineEdit(this);
  form->addRow(QStringLiteral("模型"), m_model);

  root->addLayout(form);

  auto *priceBox = new QGroupBox(QStringLiteral("估價 (USD / 1M tokens)"), this);
  auto *priceLayout = new QVBoxLayout(priceBox);

  auto *priceNote = new QLabel(
      QStringLiteral(
          "Gemini API 只回傳 token 數，不回傳金額，故金額需在本機換算。\n"
          "以下 token 數為 API 實際回報值，價目請自行對照 Google 定價。"),
      priceBox);
  priceNote->setWordWrap(true);
  priceNote->setStyleSheet(QStringLiteral("color: gray;"));
  priceLayout->addWidget(priceNote);

  auto *priceForm = new QFormLayout();
  auto makePrice = [priceBox](QDoubleSpinBox *&target) {
    target = new QDoubleSpinBox(priceBox);
    target->setRange(0.0, 1000.0);
    target->setDecimals(4);
    target->setSingleStep(0.01);
    return target;
  };
  priceForm->addRow(QStringLiteral("文字輸入"), makePrice(m_priceTextInput));
  priceForm->addRow(QStringLiteral("音訊輸入"), makePrice(m_priceAudioInput));
  priceForm->addRow(QStringLiteral("輸出"), makePrice(m_priceOutput));
  priceLayout->addLayout(priceForm);
  root->addWidget(priceBox);

  m_promptPathLabel = new QLabel(this);
  m_promptPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_promptPathLabel->setStyleSheet(QStringLiteral("color: gray;"));
  m_promptPathLabel->setText(
      QStringLiteral("Prompt 檔（每次送出前重新載入）：\n%1")
          .arg(SttPaths::userPromptFile()));
  root->addWidget(m_promptPathLabel);

  m_costLabel = new QLabel(this);
  m_costLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

  auto *resetButton = new QPushButton(QStringLiteral("重設計費"), this);
  connect(resetButton, &QPushButton::clicked, this,
          &SettingsDialog::resetBilling);

  auto *costRow = new QHBoxLayout();
  costRow->addWidget(m_costLabel, 1);
  costRow->addWidget(resetButton, 0, Qt::AlignBottom);
  root->addLayout(costRow);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    applyFromWidgets();
    SttSettingsIO::save(m_settings);
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  setMinimumWidth(520);
  loadIntoWidgets();
  refreshCostLabels();
  configureDialogForLayerShell(this);
}

void SettingsDialog::loadIntoWidgets() {
  m_settings = SttSettingsIO::load();

  m_enabled->setChecked(m_settings.enabled);
  m_apiKey->setText(m_settings.apiKey);
  m_speechLanguage->setText(m_settings.speechLanguage);

  if (QAbstractButton *button =
          m_translateMode->button(static_cast<int>(m_settings.translateMode)))
    button->setChecked(true);
  m_translateLanguage->setText(m_settings.translateLanguage);

  m_alertEvery->setValue(m_settings.alertEveryUsd);
  m_model->setText(m_settings.model);
  m_priceTextInput->setValue(m_settings.priceTextInput);
  m_priceAudioInput->setValue(m_settings.priceAudioInput);
  m_priceOutput->setValue(m_settings.priceOutput);
}

void SettingsDialog::applyFromWidgets() {
  m_settings.enabled = m_enabled->isChecked();
  m_settings.apiKey = m_apiKey->text().trimmed();
  m_settings.speechLanguage = m_speechLanguage->text();

  const int checkedId = m_translateMode->checkedId();
  if (checkedId >= 0)
    m_settings.translateMode = static_cast<TranslateMode>(checkedId);

  m_settings.translateLanguage = m_translateLanguage->text();
  m_settings.alertEveryUsd = m_alertEvery->value();
  m_settings.model = m_model->text().trimmed();
  m_settings.priceTextInput = m_priceTextInput->value();
  m_settings.priceAudioInput = m_priceAudioInput->value();
  m_settings.priceOutput = m_priceOutput->value();
}

void SettingsDialog::refreshCostLabels() {
  const SttUsageTotals totals = SttUsageLog::totals();
  m_costLabel->setText(
      QStringLiteral("估算 API 費用\n"
                     "  今日：US$%1（%2 次）\n"
                     "  過去 30 日：US$%3（%4 次）\n"
                     "  累計：US$%5")
          .arg(totals.today, 0, 'f', 4)
          .arg(totals.todayCalls)
          .arg(totals.last30Days, 0, 'f', 4)
          .arg(totals.last30DaysCalls)
          .arg(totals.lifetime, 0, 'f', 4));
}

void SettingsDialog::resetBilling() {
  QMessageBox confirm(this);
  confirm.setWindowTitle(QStringLiteral("重設計費"));
  confirm.setIcon(QMessageBox::Question);
  confirm.setText(QStringLiteral("將所有費用記錄歸零？"));
  confirm.setInformativeText(
      QStringLiteral("現有記錄會改名備份，不會刪除。\n%1")
          .arg(SttPaths::usageLogFile()));
  confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  confirm.setDefaultButton(QMessageBox::No);
  configureDialogForLayerShell(&confirm);

  if (confirm.exec() != QMessageBox::Yes)
    return;

  const QString archive = SttUsageLog::reset();

  // Start the spend alerts over as well, otherwise the next alert would only
  // fire once the new total passed the old threshold.
  applyFromWidgets();
  m_settings.alertedUsd = 0.0;
  SttSettingsIO::save(m_settings);

  refreshCostLabels();

  QMessageBox done(this);
  done.setWindowTitle(QStringLiteral("重設計費"));
  done.setIcon(QMessageBox::Information);
  done.setText(QStringLiteral("費用記錄已歸零。"));
  if (!archive.isEmpty())
    done.setInformativeText(QStringLiteral("備份：\n%1").arg(archive));
  configureDialogForLayerShell(&done);
  done.exec();
}
