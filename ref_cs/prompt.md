# todo

using ref_cs/Q9Form.cs and ref_cs/Q9Core.cs as reference
rewrite the cpp codein src, and remove current testing code

the original code is a c# portable inoput method for windows to input Traditional Chinese, then default using numpad to input
the Input Method is called 九万(TQ9), basicly it's clone of 九方輸入法
now I need to using C++ rewrite the C# code for fcitx5.

in C++ part, a floating window , connect to sqlite, loading images, capture keyboard input, and output string are all ready,
all you need is just clone the logic in C# code to c++.

# step

- clone the ref_cs/tq9_settings.ini data into data/config.json in json format.
- one difference is the button positions, in C# it's hard coded , including the resize feature, but in c++ it's base on the json and done already
- move the trau menu option into fcitx5 options, skip the auto open on startup part, and the How to use Message, and the icon
- then json should storage the last window position, and the window size. next time open, it should load the last position.
- clone the key input handling logic from Q9Form.cs `pressKey(int inputInt)`:
  - if `selectMode` is true:
    - key 0 → call `commandInput(q9command.next)` (next page)
    - key 1-9 → call `selectWord(inputInt)`
  - else (not in selectMode):
    - append digit to `currCode`
    - update status with `currCode`
    - if key is 0 → query database with `currCode` as int, call `processResult()`
    - if `currCode.length == 3` → query database with `currCode`, call `processResult()`
    - if `currCode.length == 1` → set button images to `setButtonImg(inputInt)`
    - if `currCode.length == 2` → set button images to `setButtonImg(10)` (semi-transparent)
- clone the command input handling from Q9Form.cs `commandInput(q9command command)`:
  - `cancel` → call `cancel()` to reset state
  - `openclose` → set `openclose=true`, query `core.keyInput(1)` for bracket pairs, enter selectMode
  - `homo` → toggle `homo` flag (next selection will find homophones)
  - `shortcut` (when not in selectMode):
    - if `currCode` is empty → query `keyInput(1000)` for shortcuts
    - if `currCode.length == 1` → query `keyInput(1000 + currCode)` for category shortcuts
  - `relate` → if `lastWord` is single char, query `core.getRelate(lastWord)`, enter selectMode
  - `prev` (in selectMode) → call `addPage(-1)` to go previous page
  - `next` (in selectMode) → call `addPage(1)` to go next page
- clone the `selectWord(int inputInt)` logic:
  - if `homo` flag is true → query homophones for selected word, stay in selectMode
  - if `openclose` flag is true → send selected bracket pair with cursor between them
  - else → send the selected word, query related words for display
  - store `lastWord` for relate feature
- clone the button display logic:
  - `setButtonImg(int type)` → load images from 0_1~0_9, 1_1~1_9, ..., 10_1~10_9
    - type 0 = base state, show strokes
    - type 1-9 = second level after first input
    - type 10 = semi-transparent, waiting for third input
  - `setButtonsText(string[] words)` → display candidates as text, gray empty slots
  - `setZeroWords(string[] words)` → show related words on buttons 1-9 with images still visible
- clone the SC (Simplified Chinese) output feature from `sendText()`:
  - if `sc_output` is true → convert Traditional to Simplified using `core.tcsc(text)` before committing
- clone the status display:
  - format: "九万 " + (homo ? "[同音] " : "") + statusPrefix + " " + statusText
  - `statusPrefix` shows current input code or context (e.g., "速選", "[字]關聯", "「」")
  - `statusText` shows page info like "1/3 頁"

# files

here is the database:
data/dataset.db

and here is the images:
data/img/0_1.png ~ data/img/10_9.png

here is the original config:
ref_cs/tq9_settings.ini

# database structure

here are the database structure:

CREATE TABLE "phase" (
"id" integer DEFAULT NULL,
"phase" TEXT DEFAULT NULL,
"freq" integer,
"part_of_speech" Varchar(32) DEFAULT NULL,
"source" integer,
PRIMARY KEY("id")
);

CREATE TABLE "word_code" (
"id" ,
"code" VARCHAR(16),
"words" LONGTEXT,
"weight" Float DEFAULT NULL,
PRIMARY KEY("id")
);

CREATE TABLE "word_freq" (
"id" INTEGER DEFAULT NULL,
"word" Varchar(8) DEFAULT NULL,
"idx" INTEGER DEFAULT NULL,
"freq" integer,
"cum_freq" INTEGER DEFAULT NULL,
"file_freq" INTEGER DEFAULT NULL,
"stroke_count" integer,
PRIMARY KEY("id")
);

CREATE TABLE "word_meta" (
"id" ,
"jyut_ping" VARCHAR(255),
"word_tc" VARCHAR(8),
"word_sc" VARCHAR(8),
"weight" INT,
PRIMARY KEY("id")
);
