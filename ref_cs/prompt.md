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
-

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
