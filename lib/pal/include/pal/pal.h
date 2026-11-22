// Platform abstraction layer.
//
// The kernel asks for a queue, a console, a monotonic clock and a way to reboot.
// It never talks to FreeRTOS, POSIX or Arduino directly. Each system implements
// this header once: pal_esp32 for the board, pal_host for the emulator.
#ifndef LUNOK_PAL_PAL_H
#define LUNOK_PAL_PAL_H

#include <stddef.h>
#include <stdint.h>

namespace pal {

// Event types travel through the queue as fixed-size PODs. Not one of them
// carries an owning pointer: the queue must never be a source of leaks.
enum EventType : uint16_t {
  kEventNone = 0,
  kEventConsoleRx = 1,
  kEventConsoleOwner = 2,
  kEventTimer = 3,
  kEventAlarmDue = 4,
  kEventPromiseDone = 5,
  kEventAppExit = 6,
  kEventPower = 7,
  kEventSystem = 8,
  // **El dit ha trucat** (0.190.119): the line of the panel fell, which is the only moment this board has news a person is waiting for.
  // No hi ha res a llegir a l'event —el marc el pren `touch::poll` del torn— i el que fa és **despertar la cua**, perquè el torn que
  // dormia els seus deu mil·lisegons (o els quaranta d'una becaina) no és un torn que arriba tard (docs/50, docs/95).
  kEventTouch = 9,
  kEventTypeCount = 10
};

struct Event {
  uint16_t type;
  uint16_t a;
  int32_t b;
};

enum LogLevel : uint8_t {
  kLogDebug = 0,
  kLogInfo = 1,
  kLogWarn = 2,
  kLogError = 3
};

class IQueue {
 public:
  virtual ~IQueue() {}
  // Never blocks for more than timeout_ms (0 = try once).
  virtual bool push(const Event& ev, uint32_t timeout_ms) = 0;
  // Blocks until an event or the timeout. timeout_ms < 0 means forever.
  virtual bool pop(Event& out, int32_t timeout_ms) = 0;
  // **Des d'una interrupció** (0.190.119): the same push without the right to block, for the one caller that cannot —the line of the
  // glass—. A platform whose queue is not an interrupt's business answers false and the world is as it was (docs/50).
  virtual bool push_from_isr(const Event& ev) {
    (void)ev;
    return false;
  }
  // **I la porta que sí que serveix per a una interrupció de debò** (0.190.206): `push_from_isr` és virtual, i una crida virtual des
  // d'una interrupció passa per una taula i per un thunk que viuen a la flash —el gestor del tàctil és a la IRAM justament perquè pot
  // arribar mentre la memòria cau de flash està apagada, i amb la cau apagada una crida a la flash executa brossa (era el
  // `Interrupt wdt timeout` de la Paper: vegeu `wake_the_kernel_from_isr`)—. Aquesta funció lliure és tota a la IRAM i no passa per
  // cap taula de mètodes virtuals.
  virtual bool wake_from_isr() { return false; }
  virtual uint16_t size() const = 0;
  virtual uint16_t capacity() const = 0;
};

class IConsole {
 public:
  virtual ~IConsole() {}
  virtual size_t write(const void* data, size_t len) = 0;
  // Returns what is available right now, 0 if nothing. Never blocks.
  virtual size_t read(void* data, size_t len) = 0;
  // **Quan la línia ha callat** (0.190.21): the boot throws away what the UART had before the system
  // existed, and a fixed quarter of a second is a wait that a quiet line does not owe. This says when
  // there have been no edges, no bytes and a high line for `quiet_ms`; `cap_ms` is the ceiling of a
  // line that does not go quiet. A console that cannot see its own line (the host's pipe) returns at
  // once and says it is quiet, which is the truth there (docs/50).
  virtual bool wait_quiet(uint32_t quiet_ms, uint32_t cap_ms) {
    (void)quiet_ms;
    (void)cap_ms;
    return true;
  }
  // **Whether this console is a session that can really end** (0.158.0): a **virtual terminal** —the console of
  // the emulator, which is a program on a PC that somebody started and can hang up— is a session, and `logout` ends
  // it. **The serial port of a board is not one**: nobody can hang it up and it is the only way into a board
  // with no glass, so `logout` says the session is closed and waits for an Enter, and the console starts again
  // (docs/30, docs/50).
  virtual bool virtual_terminal() const { return false; }
  // Waits until what was written has left the port. A serial line keeps bytes in flight
  // after write() returns, so the last thing a person reads before the board stops has to
  // be pushed out first or it is a word cut in half (docs/50).
  virtual void flush() = 0;

  // ---- el terminal que edita la línia (0.190.222) ----
  //
  // **El cable d'una placa té un terminal de debò, i el d'aquest sistema és el `console` de la IDF** (l'amo: *«Vull una migració
  // completa de la shell a aquest terminal serie. Poder moure el cursor, fer efectes, i colors dona molta vidilla»*). El que canvia
  // és **com s'escriu la línia**: el cursor, les fletxes, l'historial, el tabulador i les pistes els porta `linenoise`, i el que la
  // línia diu quan s'acaba va al mateix `shell` de sempre (docs/30, docs/50).
  //
  // **Una consola que no edita línies contesta que no i prou**: la de l'emulador i la del vidre reben bytes i el shell té el seu
  // editor, que és el que fa que una prova no depengui d'un terminal. Amb aquestes quatre portes, el nucli no ha de saber quina de les
  // dues té a sota (docs/45, docs/85).
  virtual bool has_a_line_editor() const { return false; }
  // **Si el que hi ha a l'altre cap entén els codis d'escapament** (0.190.223): un terminal de debò —el del cable, quan contesta la
  // sonda— sí; una canonada, un fitxer o el monitor d'un `pio` que no és terminal, no. **Són dues preguntes i no una**: qui edita la línia
  // és l'editor i qui entén el cursor és el terminal, i una banda pot tenir-ne un sense l'altre (el driver del USB que no arrenca deixa
  // el terminal sense editor i amb els codis). El que escriu codis ho pregunta aquí; el que llegeix tecles, a `has_a_line_editor`
  // (docs/30, docs/50, docs/85).
  virtual bool speaks_ansi() const { return false; }
  // **I quina mida té la vista, quan el terminal la diu** (0.190.234): files i columnes, preguntades a l'altre cap un sol cop a l'arrencada.
  // **False vol dir que aquesta consola no ho sap** —l'emulador, el vidre, una canonada, o un terminal que no entén la pregunta de la
  // mida—, i qui vulgui col·locar res a la pantalla ho ha de poder dir en lloc d'endevinar-ho (docs/50, docs/85).
  virtual bool the_size(int& rows, int& columns) const {
    (void)rows;
    (void)columns;
    return false;
  }
  // **El prompt és del shell i el dibuixa el terminal**: el shell diu la paraula i qui la posa a la pantalla és qui edita la línia, o
  // sigui que no s'escriu dues vegades (docs/30).
  virtual void set_the_prompt(const char* text) { (void)text; }
  // **Encén el terminal, amb els noms que el tabulador completa**, i contesta si hi és: la taula de comandes viu al nucli i el
  // terminal només la necessita per oferir-la. **False vol dir que aquesta consola no en pot tenir** —l'emulador, una placa on el
  // controlador no arrenca— i llavors el shell es queda amb el seu editor de sempre, que és el que fa que una placa que no pot tenir
  // el terminal segueixi tenint un prompt (docs/30, docs/45, docs/85).
  virtual bool start_the_terminal(const char* const* names, uint8_t count) {
    (void)names;
    (void)count;
    return false;
  }
  // **I la mateixa línia, esperant-la** (0.190.244): a shell that is a task of its own cannot spin, o sigui que espera aquí el que l'editor
  // de línia deixi, amb un sostre. False quan no n'hi ha cap dins del temps —que és el que aquesta banda contesta sempre en un PC, on la
  // shell la porta el torn (docs/30, docs/50).
  virtual bool wait_for_a_line(char* out, size_t capacity, int32_t timeout_ms) {
    (void)out;
    (void)capacity;
    (void)timeout_ms;
    return false;
  }
  // **Una línia que una persona ha acabat**, sense el salt i sense bloquejar. False quan no n'hi ha cap d'espera.
  virtual bool take_a_line(char* out, size_t capacity) {
    (void)out;
    (void)capacity;
    return false;
  }
  // **L'historial de la sessió, que pot sobreviure a un reinici** (0.190.236): el camí el decideix el nucli —el **disc preferit** de la
  // persona, o sigui la targeta quan n'hi ha— i qui escriu el fitxer és el terminal, que és qui té la llista a la memòria. False quan
  // aquesta consola no té editor de línia i, per tant, no té cap historial ni cap fitxer a tocar (docs/30, docs/50, docs/70).
  virtual bool load_the_history(const char* path) {
    (void)path;
    return false;
  }
  virtual bool save_the_history(const char* path) {
    (void)path;
    return false;
  }
};

struct HeapInfo {
  bool valid;             // false on platforms without a heap to report
  uint32_t free_bytes;
  uint32_t min_free_bytes;
  // **The biggest block that would fit right now**, which is not `free_bytes`: a heap with 200 KB free
  // in a thousand pieces cannot give a page of 100 KB, and the board says so with
  // `heap_caps_get_largest_free_block` (docs/75). Zero on a build that cannot ask.
  uint32_t largest_free_bytes;
};

class ISystem {
 public:
  virtual ~ISystem() {}
  virtual IQueue* queue() = 0;
  virtual IConsole* console() = 0;
  // Monotonic milliseconds. Never goes backwards, never jumps.
  virtual int64_t millis() const = 0;
  // Monotonic microseconds, for the meters: a call to a device is measured in microseconds or it is not
  // measured at all, and it is the same clock the milliseconds come from (docs/30).
  virtual int64_t micros() const = 0;
  virtual void reboot() = 0;
  // True once reboot() has been called on systems that cannot reboot in place.
  virtual bool reboot_requested() const = 0;
  // **A 32-bit number of this band**: on the board it comes from the chip —the true word of its generator while the
  // radio is up, and a mixer seeded from the SAR ADC's noise and the clock when it is not, docs/50— and on a PC from a
  // mixer the run seeded. It is what the interpreter starts its own generator with, and what an app that needs a seed
  // asks for (docs/20, docs/75).
  virtual uint32_t random() = 0;
  virtual HeapInfo heap() const = 0;
  // **How much of the task's own stack is left at its lowest point**, in bytes, or -1 on a build that
  // cannot ask. A page of controls, a font of outlines and the interpreter share one task, and a stack that
  // overflows does not say so: it corrupts a backtrace and reboots (measured on the Paper, 2/11).
  virtual int32_t stack_free() const = 0;
};

// Brings the layer up: console mode, queue allocation, reader tasks.
bool init();
void shutdown();

// Valid after init().
ISystem& system();

// The speed of the console, when the console is a wire whose speed matters. A real UART at 115200
// baud carries ten bits per byte (8N1), and a console that prints a screen instantly is faster than
// the board in a way a person notices (docs/85). Zero —the default, and what the tests want— is no
// wait at all: a file is not a wire. Only a host whose console is a terminal calls this.
void set_console_baud(uint32_t baud);

// **Whether the console is a session that can really end** (0.158.0), which is the `IConsole`'s policy and the one
// the shell asks before it closes anything: a **virtual terminal** —the console of the emulator, a program on a PC that
// somebody started and can hang up— is, and `exit`, `logout` and Ctrl+D end the run; a **serial port** is not, and
// there the session is not closed at all —the board says so and the Enter of the terminal opens a new one— (docs/30,
// docs/50, docs/85). False is the default and what a test wants: its console is a file or a pipe and its case models
// the board.
void set_virtual_terminal(bool on);
bool virtual_terminal();

// **What the heap of the emulator costs**, because on the board a block is not free: the allocator walks
// its free list before it hands one over, and moving a block that has to move is a copy of its bytes
// through the bus. `step_ns` is one node of that walk and `bytes_per_us` is what the extra memory
// moves in a microsecond (the PSRAM of the Paper is 20 MB/s); zero —the default, and what every test
// wants— is a heap that answers at once and copies for free (docs/85).
void set_heap_timing(uint32_t step_ns, uint32_t bytes_per_us);

// **What dispatching a deadline costs**, in microseconds: on the board a wake-up lands on a tick and the
// task that was waiting for it runs on the **next turn**, which is why a hundred awaits of one
// millisecond take 199 ms there and 115 ms on a PC (measured, docs/75). The emulator pays this after a wait
// that expired with nothing to read, so a promise of one millisecond costs what it costs on the glass.
// Zero —the default, and what every test wants— is a wait that is over when it is over (docs/85).
void set_dispatch_us(uint32_t us);

// **The longest a wait of the kernel's queue may block, in milliseconds.** On the board the chip's own tick
// wakes the system many times a second, and a wait that ends when the chip says so is the chip's business. On
// a PC the queue sleeps in one `select` until the console has a byte or the deadline arrives, and everything
// that is not the console —the window of the emulator and the finger on its glass— is looked at between those
// waits: a click that comes and goes inside one of them is drained in a batch with the next one, which is two
// taps poured into the panel at once and therefore one tap (found on the emulator, docs/85, docs/90). Zero —the
// default, and what every test wants— is a wait that lasts as long as it was told (docs/85).
void set_wait_slice_ms(uint32_t ms);

// **Keep the host alive while the kernel waits for a person** (0.190.3). The turn of the kernel is what looks at the window of
// the emulator and at the finger on its glass, and a page that waits for a person —the page of an app that has died, which waits
// for its `Dismiss` with no end (docs/30)— stops that turn: the window stops answering the window manager, the mouse stops
// reaching the panel and the button nobody can press is a run that never ends (found on the window: *«es veu l'error al log i
// després l'emulator es penja»*, docs/85). A host that has something to look at while it waits registers it here, and the second
// rule is the important one: **a wait of the queue that expires with nothing to read is a look at the window too**, so a wait
// inside a paint or a page is kept alive by the same function as a wait in a turn.
//
// The function answers **true when the person has asked the run to end** —the corner of the window of the emulator—, and the
// page lets go of a run that is ending instead of waiting for a Dismiss that is no longer on any screen. A board registers
// nothing: its glass is the panel and there is no window to pump (docs/50).
void set_pump(bool (*fn)(void*), void* ctx);
// Looks at the host once. False when there is nothing to look at, which is every board.
bool pump();

// **A seed for the numbers of the band, when the run wants to be told which ones** (0.174.8): zero —the default— is
// "seed me from this machine", which on a PC is the clock and the process and on a board is the hardware RNG; any other
// number makes `ISystem::random()` repeat the same sequence, which is what `--seed` of the emulator is for and what a
// case that draws a page at random needs to be reproducible (docs/20, docs/85). On a board it does nothing: the chip
// has one source of noise and a firmware that could freeze it would be a firmware that can be made predictable.
void set_random_seed(uint32_t seed);

// **What a home of the memory has left**, in bytes: the board's own RAM (`extra` false) or its extra
// memory (`extra` true). `counted` is false on a PAL that hands blocks out of the machine's heap and
// keeps no books, which is what a test wants and what an emulator without a model is (docs/85).
struct MemoryLeft {
  size_t free_bytes;
  size_t lowest_free_bytes;  // the least that was left since the ceilings were set
  bool counted;
};
MemoryLeft memory_left(bool extra);

// **The ceilings of the two homes, in bytes**: the board's own RAM and its extra memory, as the board
// really has them (docs/75). The blocks of `extra_memory()` are what is counted against them, which is
// what makes an app that asks for more than the glass has die here and not on the paper. Zero is a
// home with no ceiling, and a call starts the books again, so it goes once and before the system takes
// its first block. On a board it does nothing: the chip is the chip (docs/85).
void set_memory_bytes(uint32_t home_bytes, uint32_t extra_bytes);

// **Wait this many microseconds.** The kernel does not use it —its waits are events and alarms— and it
// is here for the one thing that has to spend time on purpose: an emulator that simulates a processor it is
// not running on, where the interpreter's instructions cost what they cost on the board (docs/85). On
// the board it is the chip's own busy wait and nothing calls it with a number.
void delay_us(uint32_t us);

// **Un pany per a les cues que creuen tasques** (0.190.244, l'amo: *«Aquestes cues han de ser protegides amb mutex»*): the kernel runs on the
// board in more than one task and on the PC in one, so what two of them touch needs a lock, and the PAL is the only layer that knows how to
// make one in both. A null lock is the PC's, and taking it is doing nothing (docs/50, docs/85).
using Lock = void*;
Lock make_lock();
void take_lock(Lock lock);
void give_lock(Lock lock);

// **Una tasca que el nucli demana** (0.190.244): the shell of the board is its own task and the shell of the PC is the turn itself, o sigui que
// això ho contesta la PAL i no el nucli. `start_a_task` contesta si la tasca existeix; `tasks_are_here` diu si aquest build en té (docs/50, docs/85).
bool tasks_are_here();
bool start_a_task(const char* name, uint32_t stack_bytes, uint8_t priority, void (*entry)(void*), void* argument);

// **Esperar deixant córrer els altres** (0.190.137, l'amo: *«panel bench peta la màquina... el wathdog continua desactivat? no el desacivis»*). A
// wait that gives the processor back, which is what the task watchdog of this board watches: `delay_us` is a busy wait and a command that
// spends fourteen seconds painting inside the shell never lets the idle task run, o sigui que el watchdog fa el que ha de fer i mata la placa.
// Un tick com a mínim, perquè és el que el sistema sap comptar (docs/50, docs/95).
void delay_ms(uint32_t ms);

// The extra memory of the board (PSRAM on the M5Paper) as a place to allocate from.
//
// It exists so the interpreter does not have to ask which board it is on: it asks for big blocks
// from here and small ones from the normal heap, and a board without extra memory takes all of them
// from the normal heap and says zero in `HeapInfo` (docs/75).

// **The floor of the board's own RAM, and the rule that keeps it** (0.151.20): a block goes to the chip's
// RAM only while what is left after it stays above this, and to the extra memory after that. The rule is here
// and not in a PAL so that the board and the emulator obey the same one, and so that a test can hold it.
//
// **Why it exists** (measured on the Paper, 8/11): the Lua machine of the reader asks for about 120 KB in
// small tables and strings, and with every block under 4 KB going home it took the pool the file system needs
// for a `FILE` and the card for a DMA buffer. `fopen` then answered `ENOMEM` with 1,7 MB of PSRAM free,
// `ls /littlefs` printed nothing and a read of the card failed with an `ESP_ERR_INVALID_CRC`, until the board
// was rebooted. `mem` says the board's RAM has **125 464 B free at rest**, so sixty-four thousand leaves the
// drivers room for the pieces they ask for in one block (docs/75).
constexpr size_t kHomeReserveBytes = 64 * 1024;

// True when the block should be taken from the extra memory first: the caller asked for it, or the board's own
// RAM would drop below its floor by giving it away. A block that asked for the extra memory and does not get it
// falls back home in every PAL, so a board with no PSRAM keeps working exactly as it did.
inline bool take_from_the_extra(size_t free_home, size_t bytes, bool prefer_extra) {
  if (prefer_extra) return true;
  return free_home < bytes + kHomeReserveBytes;
}

class IExtraMemory {
 public:
  virtual ~IExtraMemory() {}
  // A block of `bytes`, from the extra memory when `prefer_extra` and the board has it, and from the
  // normal heap otherwise. When the preferred home is full it tries the other one before saying no:
  // a block is a block and an app does not have to know where it lives. nullptr is a refusal.
  virtual void* allocate(size_t bytes, bool prefer_extra) = 0;
  // The same block, grown or shrunk. It is **moved** when its home changes —one that grows past the
  // caller's threshold goes to the extra memory and one that comes back goes home— and stays where
  // it is when it does not. On failure the block is untouched and nullptr is returned.
  virtual void* resize(void* block, size_t bytes, bool prefer_extra) = 0;
  virtual void release(void* block) = 0;
  // **La taula dels llibres d'aquesta plataforma** (0.190.0): nullptr until the first block has been asked for —the table is the
  // PAL's to give and it is given once— and nullptr for ever on a platform whose books are off. The books of `lib/pal` find their
  // way back here instead of keeping a pointer of their own (docs/75).
  virtual void* books() const { return nullptr; }
  // **I és la marca qui la demana primer** (0.190.0): a tag that arrives before the table exists would write its owner into nothing and
  // the first hundreds of blocks of the run —the VM of the launcher, which is the first thing that asks— would have no owner at all.
  // El constructor de `MemoryTag` crida aquesta porta i la taula hi és abans que la primera petició (docs/75).
  virtual void open_the_books() {}
};

// **Despertar el nucli des d'una interrupció, i res més** (0.190.206). El que hi ha a sota d'aquesta crida és **tot a la IRAM**: la
// plataforma és un global i no una crida, la cua es toca per un mètode no virtual, i el que queda (`xQueueSendFromISR`,
// `xTaskGetSchedulerState`) la IDF ja ho té a la IRAM. El motiu és el que es va veure a la Paper: un gestor de la IRAM que crida codi
// de la flash queda a mitges quan la cau s'apaga, i el xip se'n va a un `Interrupt wdt timeout` sense dir per què (docs/50, docs/95).
void wake_the_kernel_from_isr();

// Valid before and after init(): the object is a static of the platform, because the interpreter may
// allocate before anybody brought the layer up.
IExtraMemory& extra_memory();

// **ELS LLIBRES DE LA MEMÒRIA** (0.190.0, l'amo: *«El sistema fa un seguiment de memoria que es demana i s'allibera? podríem fer una
// llista de punter, tamany i qui l'ha demanat?»*): every block that leaves the door of `IExtraMemory` is written down —the pointer,
// the bytes and **who asked**—, and `mem blocks` lists them and adds them up per owner. La pregunta no tenia resposta: `mem` deia
// els totals de cada casa i el pic de l'app del vidre, i **cap bloc sabia de qui era** (docs/75).
//
// **The books are of the system and not of the app**: a Lua machine asks for tens of thousands of blocks in a second, and what a
// person needs to know is which module is eating the memory and not which string of a table it was (mesurat a l'emulador: 24 000
// peticions de menys de 256 B en dotze segons, 478 de mitjanes i 28 de grans).
//
// **The table is small on purpose and says what it has lost**, and the big blocks have a part of it for themselves: a block of
// four kilobytes or more can always be written down while the small ones fill the rest, perquè el que s'ha de veure primer és el
// bloc que no hi cap i no la cadena que hi sobra (docs/75).

// **Who is asking.** The word is the module —the app in front is the second half of the answer and it comes from `set_app_now`—,
// and the words live in the flash like the names of the counters of the port (docs/75, docs/95).
enum MemoryOwner : uint8_t {
  kMemoryNobody = 0,
  kMemoryKernel,
  kMemoryShell,
  kMemoryUi,
  kMemorySession,
  kMemoryBook,
  kMemorySplash,
  kMemoryLua,
  kMemoryMeter,
  kMemoryNet,
  kMemoryBoard,
  kMemoryOther,
};
const char* memory_owner_word(MemoryOwner who);

// **The mark of what is being asked now**: a stack, so a module that calls another one and the other one that asks for memory ends
// with the block under the innermost mark —`pal::MemoryTag who_asks(pal::kMemoryUi, shell::app_id());` at the top of the function
// that asks— (docs/75). An object and not a call to remember and another to forget, because the way out of a function has to be the
// way out of the mark. **L'app és la del moment de la marca** i no la de cada petició: una funció que demana vint blocs ja sap de qui
// són, i preguntar-ho a cada bloc seria un `strncpy` per bloc per a la mateixa resposta.
//
// **Zero bytes estàtics**: the mark lives in the table of the books, which is the platform's —the PAL object is already in RAM—, o
// sigui que aquesta capçalera no afegeix ni un byte al Paper, que no en té ni un de lliure (docs/75).
class MemoryTag {
 public:
  MemoryTag(MemoryOwner who, const char* app = nullptr);
  ~MemoryTag();
  MemoryTag(const MemoryTag&) = delete;
  MemoryTag& operator=(const MemoryTag&) = delete;

 private:
  void* books_;
  uint8_t was_owner_;
  char was_app_[16];
};

// **Un bloc que es torna sol** (0.190.206): the same door as `extra_memory()` with the way out of a function built in, perquè una
// funció que té nou sortides i un kilobyte a la mà no n'ha de recordar de nou. Demana amb una marca —així el bloc surt amb amo a
// `mem blocks` i no com «other»— i el torna al destructor. Un bloc que no s'ha pogut prendre és `false` i `get()` torna `nullptr`, o
// sigui que el que el demana ho ha de mirar abans de fer-lo servir.
class Scratch {
 public:
  explicit Scratch(size_t bytes, MemoryOwner who = kMemoryOther, bool prefer_extra = true);
  ~Scratch();
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;

  void* get() const { return block_; }
  explicit operator bool() const { return block_ != nullptr; }

 private:
  void* block_;
};

// **One row of the listing**: the pointer the caller has, the bytes it asked for, who asked, and the app that was in front when
// it asked. The name is copied here —a row outlives the app that asked it— with the sixteen bytes a session name takes (docs/75).
struct MemoryBlock {
  const void* block = nullptr;
  uint32_t bytes = 0;
  uint8_t owner = kMemoryNobody;
  char app[16] = {};
};

// **The live blocks, the biggest first**: `cap` is the room of the caller's array and the answer is how many blocks the books
// hold, which is not how many fitted in it (docs/75).
uint32_t memory_blocks(MemoryBlock* out, uint32_t cap);

// **What each owner holds**, the biggest first: a row per module and app, because a leak is a name and a number (docs/75).
struct MemoryBill {
  uint8_t owner = kMemoryNobody;
  char app[16] = {};
  uint32_t blocks = 0;
  uint32_t bytes = 0;
};
uint32_t memory_bills(MemoryBill* out, uint32_t cap);

// **A list that is not the whole list says so**: how many asks did not fit in the table and how many rows it has, and **how many
// blocks it is holding** —the number of rows is the room and not the answer— (docs/75).
uint32_t memory_lost();
uint32_t memory_slots();
uint32_t memory_live();

// **The doors of the books, for a PAL**: `allocate`/`resize`/`release` of `IExtraMemory` call them, and nothing else does —the
// books cannot see the heap and the heap cannot book itself—. `memory_books_begin` is the table, which the PAL gives from the
// extra memory when the board has one and from home when it does not; a table of zero bytes is a build with no books, and then
// every ask is lost and `mem` says it (docs/75, docs/85). The table stays the PAL's and `IExtraMemory::books()` is how the books
// find it again, que és el que fa que `lib/pal` no tingui ni un punter estàtic.
constexpr uint32_t kMemoryBookBytes = 8 * 1024;
void memory_books_begin(void* table, size_t bytes);
void memory_book_taken(void* block, size_t bytes);
void memory_book_moved(void* was, void* block, size_t bytes);
void memory_book_released(void* block);

// **What to give up when an allocation fails** (0.179.0, and the rule said by the owner in 0.179.6: *«Només s'ha de fer
// si falla una alocació de memoria d'una app o sistema. (s'esborra i es reintenta de forma transparent per l'app)»*): the
// kernel installs a function that frees what can be made again —the session memories of the apps (docs/30)—, and **it is
// called from the one place where an ask has already failed in every home and nowhere else**: never by a threshold of free
// bytes and never before trying. What comes after it is the same ask again, so the app that asked never learns that its
// own cache was the price.
//
// **`keep` is the block the ask is about**, when it is about one —a `resize` of a block that is already there— and the
// relief must not give it back: freeing it and asking the allocator to move it afterwards is a block used after it was
// given away. `allocate` passes nullptr; `resize` passes the block.
//
// It is here and not in a PAL so that the board and the emulator obey the same rule and a test can hold it: a session
// cache is the first thing to go and the last thing anyone misses. The function is not called from inside itself, so a
// relief that frees through `release()` cannot recurse.
void set_memory_relief(void (*relief)(void* keep, size_t bytes));

// **Els canaris de la memòria** (0.190.101, l'amo: *«millor fem les proves que volies fer amb els canaris i tota la pesca»*). Un
// bloc que surt de la porta d'`IExtraMemory` pot portar una **capçalera i una cua** amb una marca i la seva mida, i llavors qui
// escriu més enllà del que va demanar deixa un rastre que es veu **al primer `release` o `resize` d'aquell bloc** —que és
// exactament el moment en què el `tlsf` del xip cau amb un `StoreProhibited` sense dir per què—. És un build de diagnòstic i no de
// vida: `LUNOK_MEMORY_GUARDS` l'encén, i el preu són trenta-dos bytes per bloc i una còpia a cada `resize` (docs/75, docs/95).
//
// **I `debug_watch_a_block_of` és la segona meitat**: quan els canaris ja han dit de quin bloc es tracta, aquesta porta arma un
// **watchpoint de maquinari** damunt de la cua del següent bloc d'aquella mida, i el xoc deixa de ser al lloc on el mort es troba i
// passa a ser al lloc on el mata —amb el seu `PC` i la seva pila— (docs/95).
void debug_watch_a_block_of(size_t bytes);
void debug_watch_off();
uint32_t debug_guards_on();
// La integritat dels dos munts de memòria, dita per qui els administra: la camina i diu **on** és el forat (docs/95).
bool debug_heap_check();

}  // namespace pal

#endif  // LUNOK_PAL_PAL_H
