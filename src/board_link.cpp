// The only place where the band is named.
//
// The board and the PAL implementation are chosen by the environment in
// platformio.ini, and this file takes them through a header that has one name and
// one content per board (lib/hal_<id>/include/board/board.h). No file in the
// project writes #ifdef for a board: the include path decides.
//
// Two jobs are done at once here: the interface headers are pulled in, and so are
// the two libraries of this band. PlatformIO's dependency finder works by
// following #include directives, so without this translation unit the four
// libraries are never compiled and the link fails with "undefined reference to
// hal::platform()" — which is exactly what happened before this file existed.
//
// It is compiled in every environment, tests included (test_build_src = yes).
#include "board/impl.h"
#include "hal/hal.h"
#include "pal/pal.h"
