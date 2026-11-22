// The serial shell. At M0 it is the only user interface there is, because the
// board has no screen (docs/10 and docs/50).
#ifndef LUNOK_KERNEL_SHELL_H
#define LUNOK_KERNEL_SHELL_H

#include <stddef.h>
#include <stdint.h>

#include "alarms/alarms.h"

namespace kernel {
namespace shell {

void start();

// **La shell de la placa és una tasca seva** (0.190.244, l'amo: *«El terminal serie ha de ser multitasca. El terminal s'ha d'inicialitzar i
// quedar-se a l'espera. Anirà a una tasca del sistema per poder seguir atenent al usuari encara que la pantalla estigui robant molt temps
// els torns»*). `serve` is what that task runs: it waits for a line —or for bytes when the console has no editor—, echoes it and **leaves it
// to the turn**, que és qui l'executa (0.190.245). On the PC there is no such task and the turn calls `serve_once(0)`, which is the same code
// without the wait (docs/30, docs/50, docs/85).
void serve();
void serve_once(int32_t wait_ms);
// **I la tasca on viu**, que la demana l'arrencada: contesta si aquesta banda en té (docs/50).
bool start_the_task();
// **Les comandes que la shell ha llegit, fetes pel torn** (0.190.245): la shell només llegeix i fa l'eco, o sigui que cada línia acabada espera
// a `kernel/orders` i aquesta és la porta per la qual el torn les executa —el vidre, l'intèrpret i l'app són seus—, al començament de cada
// volta i amb el mateix `take_a_line` d'una línia del terminal (docs/30, docs/50).
void carry_out_the_orders();

// **La pàgina de casa la demana la shell, no l'arrencada** (0.190.247, l'amo: *«Mira que és fàcil tu!, shell fa "run home_eink &" només
// entrar i santes pàsques!»*): escriu `run <LUNOK_HOME_APP> &` a la cua de comandes i contesta si hi ha pogut entrar. El torn l'executa al
// primer torn, o sigui que la pàgina del sistema arriba **després** de l'arrencada i amb el cable ja viu (docs/30, docs/50).
bool ask_for_the_home_app();

void prompt();
// **La sessió del cable tanca sola als cinc minuts** (0.190.184): el torn la mira a cada volta, i si la persona no ha tocat res es tanca i el
// xip torna a poder fer becaines (docs/30, docs/60).
void poll_idle();

// **L'historial de la sessió que sobreviu a un reinici** (0.190.236, l'amo: *«pero usant el disc prefrit per l'usuari, si hi ha SD instal·lada,
// es l'SD per evitar danyar la flash»*). El camí no és una preferència nova: és el **disc preferit** que ja decideix `/cfg` i `/data`
// (`cfg auto|sd|littlefs`), o sigui que amb una targeta el fitxer hi viu i el flaix no s'escriu per una comanda. `load_the_history` es
// crida quan el terminal s'encén i `keep_the_history` quan la sessió es tanca; una consola sense editor de línia contesta que no a totes
// dues i no es toca cap fitxer (docs/30, docs/50, docs/70).
void load_the_history();
void keep_the_history();

// **Les sis tintes d'una resposta** (0.190.238): el cian clar del prompt per als títols, el gris del mig del vidre per al que no és el que una
// persona ha vingut a llegir —unitats, guions, avisos—, i el verd i el vermell que el registre ja fa servir per a un sí i un no. `stats` les
// necessita perquè la seva taula no passa pel shell: l'escriu `meter.cpp` directament a la consola (docs/30, docs/50).
extern const char* const kStyleTitle;
extern const char* const kStyleHead;
extern const char* const kStyleKey;
extern const char* const kStyleQuiet;
extern const char* const kStyleYes;
extern const char* const kStyleNo;
extern const char* const kStyleWarn;
extern const char* const kStyleOn;

// Bytes as they arrive from the console. Handles echo, backspace and newline.
void feed(const char* data, size_t len);

// **Una línia sencera que el terminal ha editat** (0.190.222, l'amo: *«Vull una migració completa de la shell a aquest terminal serie»*):
// el cursor, les fletxes, l'historial i el tabulador els porta l'editor, i el que en surt quan es prem Enter és aquesta línia. **No es
// torna a escriure a la pantalla** —l'editor ja ho ha fet— i el que se'n fa és el mateix que el camí de bytes fa amb una línia: el
// shell mana (docs/30, docs/50).
void take_a_line(const char* text);

// **El filtre del color, i la mesura d'una seqüència** (0.190.226): el codi porta els codis d'escapament sempre i qui decideix si arriben a
// la línia és `cfg::ansi_on()` —el commutador de `ansi on|off`, que l'arrencada posa a `off` quan el terminal de l'altre cap no contesta—.
// `filter_ansi` deixa el text tal com és amb el color encès i treu cada seqüència sencera quan és apagat; `skip_the_escape` diu on s'acaba
// la que comença en una posició, i és la que fa que el filtre i el camí que escriu a trossos comptin igual (docs/30, docs/50, docs/85).
size_t filter_ansi(const char* text, size_t len, char* out, size_t cap);
size_t skip_the_escape(const char* text, size_t len, size_t at);

// **Si aquesta sessió parla codis** (0.190.229): és el que el filtre consulta i el que el registre pregunta abans de pintar el nivell d'una
// línia. `ansi on|off` el mou i l'arrencada el deixa com el terminal de l'altre cap demana (docs/30, docs/50).
bool ansi_on();

// **Els noms de les comandes**, perquè el terminal sàpiga què completa el tabulador. La taula és d'aquest fitxer i no es copia enlloc
// (docs/30, docs/45).
const char* const* command_names(uint8_t& count);
// Runs one command line (already null-terminated). Returns false on error.
bool execute(char* line);

// **The restart, asked for by the prompt or by an app** (0.171.0): the picture of the moment goes on the glass, the ink
// is waited on and the board restarts, which is what `reboot` does — it is one function because a restart is a moment a
// person sees and three copies of it would be three moments (docs/30, docs/50). `reason` is written in the log with the
// app's name in front when an app asked; the prompt passes nothing, because a person at the prompt is the reason.
bool reboot(const char* reason);

// ---- a session of the shell that is not the console (0.159.0, docs/30) ----
//
// **The shell is the system's and not the wire's**: `open_session` gives a handle and the caller talks to a shell
// through three pipes —`session_write` is the lines in, `session_read` the answers and `session_read_error` the
// complaints— with no console in the middle, no prompt and no echo. The handle is one based, and `open_session`
// answers 0 with the reason in `why` when it cannot give one.
// **What a session costs**: 1 024 B of answers and 256 B of complaints asked of the heap, given back by
// `close_session`; a byte that does not fit the ring is dropped and counted (`session_dropped`), because an app that
// stops reading must not stop the shell. One session at a time, which is the number `kMaxSessions` says
// (docs/30, docs/75).
uint8_t open_session(char* why, size_t why_cap);
void close_session(uint8_t id);
bool session_open(uint8_t id);
bool session_write(uint8_t id, const char* data, size_t len);
size_t session_read(uint8_t id, char* out, size_t cap);
size_t session_read_error(uint8_t id, char* out, size_t cap);
uint32_t session_dropped(uint8_t id);

// **The six calls, into the interpreter** (0.159.0): `luabind` never includes the kernel, so the boot hands it the
// table and `sys.shell()` is the Lua side of the same session (docs/20, docs/30).
void install_shell_ops();

// True while an app has been started and has not finished. An app may be waiting
// for a promise between turns of the kernel (docs/30).
bool app_running();

// True while a command that waits for the radio is running: the shell shows no
// prompt while it is, because its answer is not in yet (docs/30).
bool native_running();

// One turn for the app. Returns true while it is alive; when it finishes, the
// prompt comes back and the interpreter is stopped.
bool poll_app();

// Milliseconds until the app needs attention, or -1 when there is no app or the
// app waits for something without a deadline.
int32_t app_wait_ms();

// The id of the app that is alive, or "" when there is none.
const char* app_id();

// Closes the app that is alive, if there is one. What `raise` does to whatever
// was in front before the alarm takes over.
//
// **`say` és la línia del prompt** (0.189.175): `app closed` is the answer to a page that is gone for good, and a page that
// steps aside for an alarm window and comes back has not been closed by anybody —the caller that parks it says its own line
// and leaves this one out— (docs/30, docs/60).
void close_app(bool say = true);

// **Going back one view, which is what every way out asks for first** (0.163.1): the app gives the page on top back
// and hears `on_back(depth)`; true means the app is still in front and the caller does not close it. It is here and
// not in the three places that ask for it —the pinch from big to small, the three taps of the middle key and the
// three taps on the glass— because «go back» has one meaning and a page that stops listening is what the ways out
// exist for (docs/30).
bool go_back_one_view();

// **Whether the page in front is one that may be left** (0.179.25): the panel of the system asks it to know whether its cross is a
// button at all —a launcher is the one page that is not left, and no page at all is nothing to leave— and the answer is the one
// `leave_one_page` gives, asked before the fact instead of after it (docs/30, docs/45).
bool the_page_in_front_can_be_left();

// **What going out of a page ended in** (0.167.0), so the three gestures can say it in their own words and a caller does
// not have to ask the same questions twice (docs/30).
enum class LeaveOutcome {
  kNothing,     // no app is in front
  kLauncher,    // a launcher is the one page that is not left
  kBack,        // a page was given back and the app is still in front
  kClosed,      // the only page went and the prompt is what there is
  kClosedHome,  // the only page went and the page that asked for it took the glass back
};

// **The one road out of a page, which every way out asks for** (0.167.0): a page with another one on top gives the top
// one back and stays —the app hears `on_back` and the page below it is refreshed by the system—, and a page that is the
// only one goes with the app, **and the page it was started from takes the glass back**, which is the launcher when a
// launcher asked for it. The pinch, the three presses of the middle key and the three taps on the glass all come here,
// because «leave this page» is one meaning and three gestures that mean it differently are three behaviours (docs/30).
LeaveOutcome leave_one_page();

// **Tancar l'app de debò, i apagar** (0.185.0, the card of the panel asks for both). `leave_one_page` gives **one page** back
// and leaves the app in front, which is what a pinch means; the cross of the card says «close» and closes **the app**, whatever
// pages it has on top, and the page that asked for it takes the glass back —the same return the last page has when it goes by
// itself— (the owner: *«"Close" ha de tancar l'app, no anar enrere, per aixó ja està "Back"»*). A launcher is refused, and
// `power_off` is the sequence the `poweroff` line of the prompt does: the picture of the moment, its wait, and the supply
// (docs/30, docs/50).
LeaveOutcome close_the_app();
bool power_off(const char* reason);

// **El son que deixa la placa encesa i una tecla per tornar** (0.189.25): the command `deepsleep` and the key of the card of the
// bar ask for the same sequence, so it is one function and not two —the picture, the line out, the disk and the store, and the
// chip asleep with the supply still held—. `reason` is what goes in the log (docs/30, docs/50).
bool deep_sleep(const char* reason);

// **I el que l'ha fet pujar li és dit quan una alarma la fa pujar** (0.190.7, l'amo: *«les alarmes de les apps es multipliquen»* va
// destapar que un `raise` no deia res a l'app): the entry the alarm carried reaches the app's `on_alarm` through `AStart::alarm`, exactly as it
// does in a window, perquè el ganxo és l'única porta que té una alarma (docs/30, docs/60).
// **An app asks for another one** (0.153.0): what `app.run` does from Lua, and the switch happens after the turn —the app
// in front goes away with its goodbye hooks and the one asked for comes up in its place—, because the app that asked is
// the one the interpreter belongs to and closing it inside the call would destroy the state the call is running in.
// False with a reason when there is nothing to ask right now; the name is taken at most once per turn (docs/30, docs/90).
// `back` is what a launcher wants: the app that asked is started again when the one it opened ends, which is the only
// way back to a page on a board whose only way out of an app is a key (docs/30, docs/90).
// **`flags` are the switches the wish carries** (0.189.161): `app.run(id, back, '-d battery')` and `run appinfo -d battery` both
// leave a string here that the next start gives to the app as `app.args.flags`, which is the door the manifest's `opensArgs`
// already uses. Null or empty is no switches, and the manifest's own are what the app gets then (docs/30, docs/40).
bool ask_run(const char* id, bool back, const char* flags, char* why, size_t why_cap);

// **La porta única d'una pàgina que arrenca, amb cada cosa pel seu nom** (0.190.251, l'amo: *«reorganitza aquell caos de funcions que criden
// funcions que comproven que criden funcions»*). Fins avui hi havia `run_app(id, path, restore, splash, alarm, late_ms)` —sis arguments
// posicionals— i tres embolcalls (`start_app`, `restore_app`, `raise_app`) que eren combinacions recordades d'aquells sis. Cada porta nova
// havia d'encertar-los tots i, si se n'oblidava d'un, el defecte no era a la porta sinó a la pàgina que es veia: la meva comanda `home` va
// ensenyar la targeta que l'arrencada li treia des de la 0.179.33. Amb els camps nomenats això no pot tornar a passar sense dir-ho.
struct AStart {
  const char* id = nullptr;              // which app; empty is refused
  const char* path = nullptr;            // a file it opens with, when the page opens with a file (docs/30, docs/45)
  bool card = true;                      // the card of its manifest, when the glass is somebody else's picture
  bool come_back = false;                // it comes back from a sleep: reads its `state` and does not wash the page (docs/90)
  const alarms::Entry* alarm = nullptr;  // it is a `raise`: the payload the window would have given it (docs/60)
  int64_t late_ms = 0;
};

// **I el perquè de `card`** (0.179.33): a start that shows the picture of the manifest and one that does not are two different moments of the
// same app, and which one it is belongs to the caller. **The boot does not** —the board has already painted the picture of its own boot and a
// second one is a wash of the glass for something the person has just seen—; **la tornada a una pàgina sí**, perquè la persona ha de veure
// quina pàgina arriba (docs/30, docs/40).
bool start_a_page(const AStart& start);

// The window of an alarm: the app is loaded, turned once and its on_alarm is
// called, and then the VM goes away. Never runs on top of another app.
//
// **I qui la demana aparta la pàgina del davant** (0.189.175): `deliver_alarms` closes the page that is in front (silently,
// with its goodbye hooks and its session) and brings it back with `restore_app` when the windows are done, perquè amb un sol
// intèrpret no hi ha cap altra manera d'avisar una app quan toca. When it refuses, it says why in the log (docs/30, docs/60).
bool run_window(const char* id, const alarms::Entry& entry, int64_t late_ms);

}  // namespace shell
}  // namespace kernel

#endif  // LUNOK_KERNEL_SHELL_H
