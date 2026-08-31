#pragma once

#include <QMap>
#include <QRect>
#include <QString>
#include <vector>

struct ButtonConfig {
	int id;
	QRect rect;
	int radius = 0;
};

// 長按 取消 掣做乜。錄音 is what it has always done; the rest are for people
// who would rather have the key do something else (or nothing) when held.
enum class CancelHold {
  Stt = 0,      // 錄音
  Relate = 1,   // 關聯字
  Shortcut = 2, // 速選
  None = 3,     // 無效
};

QString cancelHoldToString(CancelHold value);
CancelHold cancelHoldFromString(const QString &value, CancelHold fallback);

// Bounds for the keypad long-press threshold, in ms. Shorter than the 錄音
// one (which starts a recording and is deliberately hard to trigger).
constexpr int kKeyHoldMinMs = 150;
constexpr int kKeyHoldMaxMs = 1000;

// The 選字 and 長按 settings, all of them under config.json's "system".
// The engine owns the behaviour; the settings window only edits these and
// tells the engine to re-read the file.
struct InputConfig {
  // 常用字調前: let what the user types reorder the candidate lists.
  bool freq_order = true;

  // 選字時長按 1~9: the 同音 list for that candidate instead of committing it.
  // This is also what makes 1~9 act on key release rather than on press -
  // until the key is up we do not know which of the two it was.
  bool hold_homo = true;
  // 未打碼時長按 0: 開關標點 (the bracket pairs) instead of 標點.
  bool hold_openclose = true;
  // 未打碼時長按 1~9: that digit's 速選 page.
  bool hold_shortcut = true;

  int hold_ms = 350;

  CancelHold cancel_hold = CancelHold::Stt;
};

struct AppConfig {
	int windowWidth;
	int windowHeight;
	int defaultWidth = 240;
	int defaultHeight = 340;
	int minWidth = 100;
	int maxWidth = 800;

	// Storage
	int lastX = 100;
	int lastY = 100;

	// System
	bool sc_output = false;
	bool use_numpad = true;

	// 選字 / 長按 settings, read by the engine (see InputConfig).
	InputConfig input;

	struct ButtonConfig {
		int id;
		QRect rect;
		int radius = 0;
	};
	std::vector<ButtonConfig> buttons;
	QRect statusRect;

	// Key mappings (from config.json "key" section) - used with numpad
	QMap<QString, int> keys;

	// Alternative key mappings (from config.json "altkey" section) - used when
	// use_numpad=false
	QMap<QString, int> altKeys;

	QString configPath; // Store path for saving
};

class ConfigLoader {
public:
	static AppConfig load(const QString &path);
	static void save(const QString &path, const AppConfig &config);
};
