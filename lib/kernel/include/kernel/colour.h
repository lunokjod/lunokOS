// **The palette of the console, and only what it needs**: the name of the prompt, what is not the answer
// (a separator, a size, a version), a warning, an error and a directory. Seven sequences and no more, because
// a terminal that is a page of text is not a place for a light show (docs/30, docs/85).
//
// The sequences are SGR and they are written only when the terminal answered the Device Attributes question
// the shell asks at start; the code that writes them is the one that knows, and this header is only the ink
// (docs/85).
#ifndef LUNOK_KERNEL_COLOUR_H
#define LUNOK_KERNEL_COLOUR_H

namespace kernel {

constexpr char kColourOff[] = "\x1b[0m";
constexpr char kColourBold[] = "\x1b[1m";
constexpr char kColourDim[] = "\x1b[90m";
constexpr char kColourError[] = "\x1b[1;31m";
constexpr char kColourWarn[] = "\x1b[33m";
constexpr char kColourGood[] = "\x1b[32m";
constexpr char kColourDir[] = "\x1b[1;34m";
constexpr char kColourName[] = "\x1b[1;32m";

}  // namespace kernel

#endif  // LUNOK_KERNEL_COLOUR_H
