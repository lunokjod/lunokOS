#pragma once

#include <cstddef>
#include <cstdint>

// **Les comandes que la shell no executa ella** (0.190.245, l'amo: *«l'app va per el seu fil d'execució i la shell terminal al port serie va en un
// altre. Per això tota execució amb "run ...." o "comanda" ha d'executar-se al fil de sistema»*).
//
// The shell of the board reads keys in its own task, echoes them and **executes nothing**: a finished line goes into this queue and the turn
// runs it at the beginning of its own turn, where the glass, the interpreter and the app live. Two tasks touch the queue, so every door takes
// the lock the PAL knows how to make, and a queue that is full refuses the line —a person who is told «no» can type it again, and one who is
// told «yes» and loses the command cannot.
namespace kernel::orders {

// **Qui demana la línia, i això no és un detall** (0.190.251): a line somebody typed in a closed session **opens the session** and is not
// executed; an order the system gives itself —the page of the start, at a boot— has to run, and the first one used to be eaten by the login.
// És el defecte que va fer que al T5S3 la pàgina de casa no sortís mai i a l'emulador sí (docs/30, docs/50).
enum Who : uint8_t {
  kPerson = 0,
  kTheSystem = 1,
};

// The shell's own line: `linenoise` cuts at a hundred and twenty-seven bytes and the editor of `feed` copies into a `char[128]`, o sigui que
// aquest és el número que la línia ja té (docs/30).
constexpr size_t kLineBytes = 128;

// **Quatre és prou**: a person types one line at a time and the turn drains the queue on every turn. A paste of more lines than this loses the
// last ones **with a line that says so**, and the number is small because the static memory of a Paper is not free (docs/75).
constexpr size_t kWaiting = 4;

// The queue and its lock, asked for once by the boot. A line posted before it is refused instead of written nowhere.
void make_the_queue();

// **Una línia acabada que el torn ha d'executar.** False when there is no room, and then nothing was written.
bool post_a_line(const char* text, Who who = kPerson);

// The turn's door: the oldest line and who asked for it, or false when there is nothing waiting. The text is copied into `out` and always ends.
bool take_a_line(char* out, size_t capacity, Who& who);

// How many lines are waiting, for the prompt and the log (docs/50).
size_t waiting();

}  // namespace kernel::orders
