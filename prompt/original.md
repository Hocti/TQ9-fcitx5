Create a input method for Linux using fcitx5 and fcitx5-chinese-addons with c++

UI:

- A floating window with 11 button. User can resize and drag the window
- The buttons position are read from a config file, with the x y width height , and the main window width and height
- when the window resize all buttons resize together on scale. the window width height ratio are fixed
- the button could be display image, text, or text with image together(top left is image bottom right is text). the button could be disabled (the image will be brighter or change opacify), the button can change the background color
- all images would be loaded are transparent PNG
- the UI should be always on top

sqlite:

the input data are from a sqlite db. at the start of the app will load it first

input key:

when click the 11 buttons or keyboard input any numpad key, the key will be capture and not typing the original character. instead, it run my logic(a function), it may be change the 11 buttons content, or it may become character output.

change the UI , the template config, and demo image for the button
the button should have multi method to set image,text,image+text,set backgound color,set brightness/alpha
load the sqlite(prepare a mock sql data with a table of emoji)
capture keyboard input, if it’s numpad, don’t output, call the logic function instead

press button will call the logic function too

the logic function will base on the input numpad key,0~9 and dot, or click 0-10 button, do some mock action:
dot will reset all
0~6 will change button image,text etc

7 will type 1 chinese word

8 will type 10 chinese word at once

9 will typeing a emoji, SELECT from the sqlite

pressing numpad “/” will type open close quotation mark at once, and place the cursor between them.

optional: if selected some textg, the open close will wrap the selected text, after that place the cursor after the close quotation mark
