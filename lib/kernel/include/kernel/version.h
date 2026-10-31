#ifndef LUNOK_KERNEL_VERSION_H
#define LUNOK_KERNEL_VERSION_H

namespace kernel {

// Bump kVersion for any user-visible change. There is one Lua API and apps do not
// declare against it; docs/20 says what happens when it has to change.
// 0.3.0 (31/10): http.get, a page with TLS, and the radio that is switched on for
// work and switched off when the queue is empty.
// 0.4.0 (31/10): both sleeps, the wake by timer and by button, and the app that
// comes back where it was (docs/90, M6).
// 0.5.0 (31/10): power.cpu, the processor from 80 to 240 MHz.
// 0.6.0 (31/10): the light of the board and the button, with on_button and the
// three taps that close an app (docs/90, M7).
static const char kVersion[] = "0.6.0";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
