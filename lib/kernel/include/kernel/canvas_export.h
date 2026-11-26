// **Guardar un canvas com a imatge, fet pel sistema** (0.192.51): el que l'app no ha de fer amb mig milió de voltes de Lua.
#ifndef LUNOK_KERNEL_CANVAS_EXPORT_H
#define LUNOK_KERNEL_CANVAS_EXPORT_H

#include <stdint.h>

namespace kernel {

// The pixels a canvas already holds, written as a PNG of eight bits of grey at `path`. The two words are the reason when it is
// false —an `E_*` and a sentence a page can show—, and a path that cannot be written leaves no half picture behind (docs/30, docs/70).
bool export_a_png(int16_t slot, const char* path, const char*& code, const char*& reason);

}  // namespace kernel

#endif  // LUNOK_KERNEL_CANVAS_EXPORT_H
