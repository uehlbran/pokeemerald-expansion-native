# The compiler flags that decide the layout and meaning of expansion's code
# and so must be the same wherever expansion's headers are compiled: for the
# game (Makefile.native) and for platform/inspect.c, which reads the game's
# structures (platform/Makefile). TESTING and the game version stay with each
# makefile: the game varies them, inspect.c is built once for the plain game.
GAME_DEFINES := -DMODERN=1 -DPLATFORM_NATIVE=1
# Plain char is unsigned on ARM; the game was written for that.
GAME_CHAR_FLAGS := -funsigned-char
