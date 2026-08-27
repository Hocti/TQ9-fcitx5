#include "SettingsDialog.h"
#include "DialogSupport.h"
#include "SttUsageLog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <iostream>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// Radio rows are used throughout instead of combo boxes: under the
// process-wide Wayland layer-shell mode a popup gets stretched to fill the
// screen.
template <typename Enum, size_t N>
QHBoxLayout *makeRadioRow(QWidget *parent, QButtonGroup *group,
                          const std::pair<const char *, Enum> (&entries)[N]) {
  auto *row = new QHBoxLayout();
  for (const auto &entry : entries) {
    auto *radio = new QRadioButton(QString::fromUtf8(entry.first), parent);
    group->addButton(radio, static_cast<int>(entry.second));
    row->addWidget(radio);
  }
  row->addStretch();
  return row;
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("九万 - 設定"));
  setModal(false);

  // Everything lives inside a scroll area so the window never grows past the
  // screen - a layer surface taller than the output cannot be reached.
  auto *page = new QWidget(this);
  auto *root = new QVBoxLayout(page);

  // ---- 選字 -------------------------------------------------------------
  auto *inputBox = new QGroupBox(QStringLiteral("選字"), page);
  auto *inputForm = new QFormLayout(inputBox);

  m_freqOrder = new QCheckBox(QStringLiteral("常用字調前"), inputBox);
  inputForm->addRow(m_freqOrder);

  auto *freqHint = new QLabel(
      QStringLiteral("打過兩次以上的字排到第二頁前面（頭九個字次序不變）；\n"
                     "連打過兩次的字組會排到「下個字」的最前面。"),
      inputBox);
  freqHint->setStyleSheet(QStringLiteral("color: gray;"));
  inputForm->addRow(freqHint);

  root->addWidget(inputBox);

  // ---- 語音輸入 ---------------------------------------------------------
  auto *form = new QFormLayout();

  m_enabled = new QCheckBox(QStringLiteral("啟用語音輸入 (STT)"), page);
  form->addRow(m_enabled);

  m_apiKey = new QLineEdit(page);
  m_apiKey->setEchoMode(QLineEdit::Password);
  m_apiKey->setPlaceholderText(QStringLiteral("AIza..."));
  auto *showKey = new QPushButton(QStringLiteral("顯示"), page);
  showKey->setCheckable(true);
  showKey->setFixedWidth(56);
  connect(showKey, &QPushButton::toggled, this, [this](bool on) {
    m_apiKey->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
  });
  auto *keyRow = new QHBoxLayout();
  keyRow->addWidget(m_apiKey);
  keyRow->addWidget(showKey);
  form->addRow(QStringLiteral("Gemini API Key"), keyRow);

  m_alertEvery = new QDoubleSpinBox(page);
  m_alertEvery->setRange(0.0, 10000.0);
  m_alertEvery->setDecimals(2);
  m_alertEvery->setSingleStep(1.0);
  m_alertEvery->setSuffix(QStringLiteral(" USD"));
  form->addRow(QStringLiteral("費用提示間隔"), m_alertEvery);

  auto *alertHint = new QLabel(
      QStringLiteral("累計費用每超過此金額提示一次，設為 0 即不提示"), page);
  alertHint->setStyleSheet(QStringLiteral("color: gray;"));
  form->addRow(QString(), alertHint);

  root->addLayout(form);

  // ---- 語言 -------------------------------------------------------------
  auto *langBox = new QGroupBox(QStringLiteral("語言"), page);
  auto *langForm = new QFormLayout(langBox);

  m_speechLanguage = new QLineEdit(langBox);
  m_speechLanguage->setPlaceholderText(QStringLiteral("例：廣東話口語"));
  langForm->addRow(QStringLiteral("聲音輸入"), m_speechLanguage);

  m_outputLanguage = new QLineEdit(langBox);
  m_outputLanguage->setPlaceholderText(
      QStringLiteral("例：繁體中文書面語（留空＝照講出來的樣子）"));
  langForm->addRow(QStringLiteral("文字輸出"), m_outputLanguage);

  auto *langHint = new QLabel(
      QStringLiteral("兩者不同時會自動改寫，例如講廣東話、出書面語，"
                     "不用開翻譯亦做得到。"),
      langBox);
  langHint->setWordWrap(true);
  langHint->setStyleSheet(QStringLiteral("color: gray;"));
  langForm->addRow(QString(), langHint);

  m_vocabulary = new QPlainTextEdit(langBox);
  m_vocabulary->setPlaceholderText(
      QStringLiteral("人名、術語、慣用寫法，一行一個或用逗號分隔"));
  m_vocabulary->setFixedHeight(72);
  langForm->addRow(QStringLiteral("常用字眼"), m_vocabulary);

  auto *vocabHint = new QLabel(
      QStringLiteral("辨識時作為參考，讀音相近就會採用這裡的寫法。"), langBox);
  vocabHint->setWordWrap(true);
  vocabHint->setStyleSheet(QStringLiteral("color: gray;"));
  langForm->addRow(QString(), vocabHint);

  root->addWidget(langBox);

  // ---- 翻譯 -------------------------------------------------------------
  auto *translateBox = new QGroupBox(QStringLiteral("翻譯"), page);
  auto *translateForm = new QFormLayout(translateBox);

  m_translateMode = new QButtonGroup(this);
  const std::pair<const char *, TranslateMode> modes[] = {
      {"關", TranslateMode::Off},
      {"開", TranslateMode::Only},
      {"同時輸出雙語", TranslateMode::Both},
  };
  translateForm->addRow(QStringLiteral("語音翻譯"),
                        makeRadioRow(translateBox, m_translateMode, modes));

  m_translateLanguage = new QLineEdit(translateBox);
  translateForm->addRow(QStringLiteral("翻譯語言"), m_translateLanguage);

  m_translateInsert = new QButtonGroup(this);
  const std::pair<const char *, TranslateInsert> inserts[] = {
      {"插在原文後", TranslateInsert::After},
      {"取代原文", TranslateInsert::Replace},
  };
  translateForm->addRow(QStringLiteral("譯文位置"),
                        makeRadioRow(translateBox, m_translateInsert, inserts));

  auto *insertHint = new QLabel(
      QStringLiteral("按 譯 鍵翻譯選取文字時，譯文放在原文之後，還是蓋過原文。"),
      translateBox);
  insertHint->setWordWrap(true);
  insertHint->setStyleSheet(QStringLiteral("color: gray;"));
  translateForm->addRow(QString(), insertHint);

  root->addWidget(translateBox);

  // ---- 錄音 -------------------------------------------------------------
  auto *recordBox = new QGroupBox(QStringLiteral("錄音"), page);
  auto *recordForm = new QFormLayout(recordBox);

  m_holdThreshold = new QDoubleSpinBox(recordBox);
  m_holdThreshold->setRange(kHoldThresholdMinMs / 1000.0,
                            kHoldThresholdMaxMs / 1000.0);
  m_holdThreshold->setDecimals(1);
  m_holdThreshold->setSingleStep(0.1);
  m_holdThreshold->setSuffix(QStringLiteral(" 秒"));
  recordForm->addRow(QStringLiteral("長按多久開始錄音"), m_holdThreshold);

  m_saveRecordings =
      new QCheckBox(QStringLiteral("保存每次錄音"), recordBox);
  auto *openFolder = new QPushButton(QStringLiteral("開啟錄音資料夾"), recordBox);
  connect(openFolder, &QPushButton::clicked, this,
          &SettingsDialog::openRecordingsFolder);
  auto *recordRow = new QHBoxLayout();
  recordRow->addWidget(m_saveRecordings);
  recordRow->addStretch();
  recordRow->addWidget(openFolder);
  recordForm->addRow(recordRow);

  auto *recordHint = new QLabel(
      QStringLiteral("以開始錄音的時間命名。就算轉文字失敗，錄音一樣留在：\n%1")
          .arg(SttPaths::recordingsDir()),
      recordBox);
  recordHint->setWordWrap(true);
  recordHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
  recordHint->setStyleSheet(QStringLiteral("color: gray;"));
  recordForm->addRow(recordHint);

  root->addWidget(recordBox);

  // ---- 模型與計價 -------------------------------------------------------
  auto *modelBox =
      new QGroupBox(QStringLiteral("模型與計價 (USD / 1M tokens)"), page);
  auto *modelLayout = new QVBoxLayout(modelBox);

  auto *modelNote = new QLabel(
      QStringLiteral(
          "每個模型連同價目一齊儲存，選中邊個就用邊個。\n"
          "Gemini API 只回傳 token 數，不回傳金額，故金額需在本機換算；"
          "回傳的輸入 token 已包含語音轉出來的部分。"),
      modelBox);
  modelNote->setWordWrap(true);
  modelNote->setStyleSheet(QStringLiteral("color: gray;"));
  modelLayout->addWidget(modelNote);

  m_modelList = new QListWidget(modelBox);
  m_modelList->setFixedHeight(96);
  connect(m_modelList, &QListWidget::currentTextChanged, this,
          [this](const QString &name) {
            if (!name.isEmpty())
              loadModelIntoWidgets(name);
          });
  modelLayout->addWidget(m_modelList);

  auto *modelForm = new QFormLayout();
  m_model = new QLineEdit(modelBox);
  m_model->setPlaceholderText(QStringLiteral("gemini-3.5-flash-lite"));
  modelForm->addRow(QStringLiteral("模型名稱"), m_model);

  auto makePrice = [modelBox](QDoubleSpinBox *&target) {
    target = new QDoubleSpinBox(modelBox);
    target->setRange(0.0, 1000.0);
    target->setDecimals(4);
    target->setSingleStep(0.01);
    return target;
  };
  modelForm->addRow(QStringLiteral("輸入"), makePrice(m_priceInput));
  modelForm->addRow(QStringLiteral("輸出"), makePrice(m_priceOutput));
  modelLayout->addLayout(modelForm);

  auto *storeModel = new QPushButton(QStringLiteral("儲存此模型"), modelBox);
  connect(storeModel, &QPushButton::clicked, this,
          &SettingsDialog::storeCurrentModel);
  auto *dropModel = new QPushButton(QStringLiteral("刪除"), modelBox);
  connect(dropModel, &QPushButton::clicked, this,
          &SettingsDialog::deleteCurrentModel);
  auto *modelButtons = new QHBoxLayout();
  modelButtons->addStretch();
  modelButtons->addWidget(storeModel);
  modelButtons->addWidget(dropModel);
  modelLayout->addLayout(modelButtons);

  root->addWidget(modelBox);

  m_promptPathLabel = new QLabel(page);
  m_promptPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_promptPathLabel->setStyleSheet(QStringLiteral("color: gray;"));
  // Four prompt files, one per job: the three 語音 modes and 譯 selection.
  m_promptPathLabel->setText(
      QStringLiteral("Prompt 檔（每次送出前重新載入）：\n%1\n"
                     "  %2　語音・不翻譯\n"
                     "  %3　語音・只出譯文\n"
                     "  %4　語音・雙語\n"
                     "  %5　翻譯選取文字")
          .arg(SttPaths::configDir(),
               QFileInfo(SttPaths::userPromptFile(TranslateMode::Off))
                   .fileName(),
               QFileInfo(SttPaths::userPromptFile(TranslateMode::Only))
                   .fileName(),
               QFileInfo(SttPaths::userPromptFile(TranslateMode::Both))
                   .fileName(),
               QFileInfo(SttPaths::translatePromptFile()).fileName()));
  root->addWidget(m_promptPathLabel);

  m_costLabel = new QLabel(page);
  m_costLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

  auto *resetButton = new QPushButton(QStringLiteral("重設計費"), page);
  connect(resetButton, &QPushButton::clicked, this,
          &SettingsDialog::resetBilling);

  auto *costRow = new QHBoxLayout();
  costRow->addWidget(m_costLabel, 1);
  costRow->addWidget(resetButton, 0, Qt::AlignBottom);
  root->addLayout(costRow);

  auto *scroll = new QScrollArea(this);
  scroll->setWidget(page);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  int maxHeight = 640;
  if (QScreen *screen = QGuiApplication::primaryScreen())
    maxHeight = std::max(320, screen->availableGeometry().height() * 3 / 4);
  scroll->setMaximumHeight(maxHeight);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    applyFromWidgets();
    SttSettingsIO::save(m_settings);
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *outer = new QVBoxLayout(this);
  outer->addWidget(scroll);
  outer->addWidget(buttons);

  setMinimumWidth(520);
  loadIntoWidgets();
  refreshCostLabels();
  configureDialogForLayerShell(this);
}

void SettingsDialog::setFrequencyOrder(bool on) {
  m_freqOrder->setChecked(on);
}

bool SettingsDialog::frequencyOrder() const {
  return m_freqOrder->isChecked();
}

void SettingsDialog::loadIntoWidgets() {
  m_settings = SttSettingsIO::load();

  m_enabled->setChecked(m_settings.enabled);
  m_apiKey->setText(m_settings.apiKey);
  m_speechLanguage->setText(m_settings.speechLanguage);
  m_outputLanguage->setText(m_settings.outputLanguage);
  m_vocabulary->setPlainText(m_settings.vocabulary);

  if (QAbstractButton *button =
          m_translateMode->button(static_cast<int>(m_settings.translateMode)))
    button->setChecked(true);
  m_translateLanguage->setText(m_settings.translateLanguage);
  if (QAbstractButton *button = m_translateInsert->button(
          static_cast<int>(m_settings.translateInsert)))
    button->setChecked(true);

  m_holdThreshold->setValue(m_settings.holdThresholdMs / 1000.0);
  m_saveRecordings->setChecked(m_settings.saveRecordings);
  m_alertEvery->setValue(m_settings.alertEveryUsd);

  // The model in use always has an entry, even on a fresh install, so the
  // list is never empty and its prices are always visible.
  if (!m_settings.models.contains(m_settings.model))
    m_settings.models.insert(m_settings.model,
                             m_settings.pricingFor(m_settings.model));
  refreshModelList(m_settings.model);
}

void SettingsDialog::refreshModelList(const QString &select) {
  const QSignalBlocker blocker(m_modelList);
  m_modelList->clear();
  for (auto it = m_settings.models.constBegin();
       it != m_settings.models.constEnd(); ++it)
    m_modelList->addItem(it.key());

  const auto matches = m_modelList->findItems(select, Qt::MatchExactly);
  if (!matches.isEmpty())
    m_modelList->setCurrentItem(matches.first());

  loadModelIntoWidgets(select);
}

void SettingsDialog::loadModelIntoWidgets(const QString &name) {
  const ModelPricing price = m_settings.pricingFor(name);
  m_model->setText(name);
  m_priceInput->setValue(price.input);
  m_priceOutput->setValue(price.output);
}

void SettingsDialog::storeCurrentModel() {
  const QString name = m_model->text().trimmed();
  if (name.isEmpty()) {
    m_model->setFocus();
    return;
  }

  ModelPricing price;
  price.input = m_priceInput->value();
  price.output = m_priceOutput->value();
  m_settings.models.insert(name, price);
  refreshModelList(name);
}

void SettingsDialog::deleteCurrentModel() {
  QListWidgetItem *item = m_modelList->currentItem();
  if (!item)
    return;

  const QString name = item->text();
  if (m_settings.models.size() <= 1) {
    // Removing the last one would leave nothing to bill against.
    return;
  }

  m_settings.models.remove(name);
  refreshModelList(m_settings.models.constBegin().key());
}

void SettingsDialog::openRecordingsFolder() {
  // main() drops QT_WAYLAND_SHELL_INTEGRATION so the file manager we spawn
  // gets a plain window instead of a layer surface; log it, since a non-empty
  // value here is the whole reason Dolphin used to come up fullscreen and
  // missing from the task bar.
  std::cerr << "[Settings] opening recordings folder, shell integration='"
            << qgetenv("QT_WAYLAND_SHELL_INTEGRATION").toStdString() << "'"
            << std::endl;
  QDesktopServices::openUrl(QUrl::fromLocalFile(SttPaths::recordingsDir()));
}

void SettingsDialog::applyFromWidgets() {
  m_settings.enabled = m_enabled->isChecked();
  m_settings.apiKey = m_apiKey->text().trimmed();
  m_settings.speechLanguage = m_speechLanguage->text();
  m_settings.outputLanguage = m_outputLanguage->text();
  m_settings.vocabulary = m_vocabulary->toPlainText();

  const int checkedId = m_translateMode->checkedId();
  if (checkedId >= 0)
    m_settings.translateMode = static_cast<TranslateMode>(checkedId);

  m_settings.translateLanguage = m_translateLanguage->text();
  const int insertId = m_translateInsert->checkedId();
  if (insertId >= 0)
    m_settings.translateInsert = static_cast<TranslateInsert>(insertId);

  m_settings.holdThresholdMs = std::clamp(
      static_cast<int>(qRound(m_holdThreshold->value() * 1000.0)),
      kHoldThresholdMinMs, kHoldThresholdMaxMs);
  m_settings.saveRecordings = m_saveRecordings->isChecked();
  m_settings.alertEveryUsd = m_alertEvery->value();

  // Whatever is in the name and price fields is the model to use, saved under
  // its own name - so editing a price needs no separate 儲存 click.
  const QString name = m_model->text().trimmed();
  if (!name.isEmpty()) {
    ModelPricing price;
    price.input = m_priceInput->value();
    price.output = m_priceOutput->value();
    m_settings.models.insert(name, price);
    m_settings.model = name;
  }
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
