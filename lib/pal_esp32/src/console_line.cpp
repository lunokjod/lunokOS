// **L'editor de línia del cable** (0.190.222, l'amo: *«Vull una migració completa de la shell a aquest terminal serie. Poder moure el
// cursor, fer efectes, i colors dona molta vidilla»*). Aquest fitxer és l'única part del sistema que sap que el terminal és el `console`
// de la IDF: `linenoise` per editar la línia, `esp_console` per saber quins noms completa el tabulador, i una cua de FreeRTOS per portar
// les línies acabades al torn del nucli, que és qui les executa (docs/30, docs/50).
//
// **I per què una tasca i no una crida**: `linenoise()` no torna fins que una persona prem Enter, i aquest nucli és un torn cooperatiu que
// també ha de pintar el vidre, moure el cursor d'una app i atendre el tàctil. O sigui que qui espera la línia és una tasca i el que
// espera el torn és la cua (docs/30, docs/50).
#include "esp32/console_line.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_console.h"
#include "esp_err.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"

#include "hal/log.h"
#include "pal/pal.h"

// **La consola és el USB o la UART0 segons la placa** (docs/50): el T5 E-Paper S3 Pro no té pont sèrie i parla pel USB-Serial-JTAG, i la
// resta de bandes per la UART0. El terminal és el mateix als dos costats; el que canvia és qui proveeix els bytes, i això ho diu
// `pal_esp32.cpp` amb aquesta mateixa macro des de la 0.190.90.
#if defined(LUNOK_CONSOLE_USB) && LUNOK_CONSOLE_USB
#include "driver/usb_serial_jtag.h"
#include "esp_vfs_usb_serial_jtag.h"
#define LUNOK_LINE_CONSOLE_USB 1
#else
#include "driver/uart.h"
#define LUNOK_LINE_CONSOLE_USB 0
#endif

namespace pal {
namespace {

// **La línia més llarga que el terminal accepta**, i la mateixa que el prompt d'aquest sistema té des de sempre: el bloc de la línia
// del shell són 128 bytes i una línia que no hi cap és una línia que es diria a mitges (`line_editor.h`, docs/30, docs/75).
const size_t kLineBytes = 128;
// **Quantes línies poden esperar el torn**: quatre és més del que una persona escriu sense que el nucli passi (el torn és de
// mil·lisegons), i una cua més llarga només seria memòria que ningú no llegeix (docs/75).
const uint8_t kPendingLines = 4;
// **L'historial del terminal**: setze comandes és el que es camina amb els dits sense mirar, i la memòria és un bloc de setze punters
// —`linenoise` la reserva a l'heap— (docs/30, docs/75).
const uint32_t kHistory = 16;
// **La pila de la tasca que espera, i això és el que va petar** (0.190.225): aquesta tasca no és només `linenoise`. A la mateixa pila hi viu
// l'estat de l'editor (`struct linenoiseState`, que és on són el prompt, el buffer i **l'amplada del terminal**), els buffers de refresc, el
// `snprintf` de newlib que copia la línia a la cua, i les crides del `console` que el tabulador i les pistes fan. Amb quatre kilobytes
// —el que el REPL d'Espressif demana, i que aquí no n'hi havia prou— la pila s'omplia i el que es veia era `pal_line` clavat amb l'`IDLE` del
// segon nucli sense menjar: l'estat de l'editor trepitjat vol dir una amplada de zero, i `refreshMultiLine` divideix per ella i
// `refreshSingleLine` hi dona voltes amb un `size_t` que baixa (l'amo ho va veure a la placa: *«S'ha quedat pillat i no podia moure el
// cursor»*). Vuit kilobytes són el doble, i la marca d'aigua que es diu al registre després de la primera línia diu quant se n'ha gastat de
// debò (docs/50, docs/75).
const uint32_t kTaskStack = 8192;
const uint8_t kTaskPriority = 1;
// **Quant s'espera el prompt del shell abans de dibuixar-lo igualment** (0.190.222): el prompt ha de sortir **després** del que ha escrit la
// comanda —així és com es veu un terminal—, i qui el canvia és el shell quan ha acabat. Però una comanda llarga (un `mesh find`, una xarxa
// que no contesta) no pot deixar la línia sense editor, i el `log follow` s'atura **escrivint una línia**: o sigui que l'espera té un sostre
// curt, i complert es dibuixa el prompt que hi hagi i la persona pot teclejar (docs/30, docs/50).
const uint32_t kPromptGraceMs = 250;
// **El color de les pistes**: el gris del mig del vidre, que és el que aquesta casa fa servir per a «això no és el que escrius»
// (`panel.cpp`, docs/30, docs/92).
const int kHintColour = 90;
// **L'amplada més petita que es creu**: un terminal de debò en té vuitanta i una finestra estreta pot baixar a quaranta; vint és el mínim que
// encara és una línia, i per sota d'això el que hi ha a l'altre cap no és un terminal o no ha contestat bé, i val més no deixar-li la línia a
// l'editor (0.190.225, docs/50, docs/85).
const int kFewestColumns = 20;
// **Quants caràcters fa cada pregunta** (0.190.231): `ESC [ 5 n` (l'estat del terminal) i `ESC [ 6 n` (on és el cursor) són quatre bytes cada
// una, i el rentat de la pantalla en posa tants com s'han escrit (docs/50, docs/85).
const size_t kProbeBytes = 4;
const size_t kWidthBytes = 4;

bool g_on = false;
// **Ja no és un pany sinó una pregunta que es pot repetir** (0.190.230): qui la fa és el nucli a l'arrencada i, després, la comanda
// `terminal`, que és l'única manera d'encendre l'editor quan el monitor apareix més tard que la placa (docs/50, docs/85).

// **El que la sonda va contestar sobre el terminal de l'altre cap** (0.190.223): si contesta la pregunta dels codis d'escapament vol dir que
// entén el cursor, el color i l'esborrat de pantalla, i això no és el mateix que «hi ha editor» —l'editor també pot no arrencar, si el driver
// del USB no s'instal·la—. Es desa un cop, es pregunta amb `the_terminal_speaks_ansi`, i el nucli ho diu al registre en encendre's
// (docs/30, docs/50).
//
// **Neix `false` i no `true`** (0.190.228): el que no ha contestat ningú és un terminal que no ho és, i un camí que no arriba a preguntar-ho
// —la cua que no s'ha pogut fer, la tasca que no ha arrencat— no pot deixar aquesta casa creient que parla amb un terminal. Amb `true` de
// sèrie, un `clear` hi enviava codis a un monitor que no els entén (docs/50, docs/85).
bool g_ansi = false;
bool g_said_the_stack = false;
// **L'amplada que ha contestat el terminal** (0.190.225): es pregunta un cop i es diu al registre; el que no es fa és deixar-la decidir res més
// que si hi ha terminal, perquè qui la fa servir és `linenoise` i una amplada de zero el penja (docs/50, docs/85).
int g_columns = 0;
// **El prompt el diu el shell i abans que el digui no n'hi ha** (0.190.222): al cable, la sessió arrenca tancada i es demana un Enter, i un
// prompt que hi és sense sessió és un prompt que demana una cosa que no es pot fer. El shell el posa al primer `prompt()` (docs/30).
//
// **I són dos blocs i no un** (0.190.222): `linenoise` es queda amb el punter del prompt mentre edita la línia i el rellegeix a cada
// redibuix, o sigui que canviar el text a sota seu li faria dibuixar meitat d'una paraula i meitat de l'altra. El shell escriu al bloc que
// ningú no està llegint i el canvia de torn (`g_prompt_now`) (docs/30, docs/50).
char g_prompt[2][kLineBytes] = {"", ""};
uint8_t g_prompt_now = 0;
QueueHandle_t g_lines = nullptr;
// **El senyal que diu «el shell ja ha dit quin és el prompt»**: el dona `set_the_line_prompt` i el pren la tasca de l'editor abans de demanar
// la línia següent, que és el que fa que el prompt surti a sota de la resposta i amb la paraula que toca (docs/30, docs/50).
SemaphoreHandle_t g_ready = nullptr;
TaskHandle_t g_task = nullptr;

// **El camí dels bytes mentre el terminal edita**: el shell escriu pels mateixos `stdin`/`stdout` que l'editor, no per la FIFO del
// perifèric, perquè dues plomes al mateix paper es trepitgen. El que escriu el nucli —el registre, una taula, un avís— i el que dibuixa
// el cursor de la línia han de sortir en ordre (docs/30, docs/50).
void post_a_line(const char* text) {
  if (g_lines == nullptr) return;
  char block[kLineBytes];
  snprintf(block, sizeof block, "%s", text);
  // **Si la cua és plena es perd la línia més vella**, i no la nova: una persona que escriu de pressa prefereix que la comanda que acaba
  // de prémer Enter sigui la que es fa (docs/30).
  if (xQueueSend(g_lines, block, 0) != pdPASS) {
    char thrown[kLineBytes];
    (void)xQueueReceive(g_lines, thrown, 0);
    (void)xQueueSend(g_lines, block, 0);
  }
  // **I el torn es desperta**: la línia no ha d'esperar el pròxim tic del nucli per fer-se. L'event és el mateix que el de la consola,
  // perquè per al nucli és el mateix fet: hi ha alguna cosa a llegir (docs/50).
  Event ev = {};
  ev.type = kEventConsoleRx;
  IQueue* queue = system().queue();
  if (queue != nullptr) (void)queue->push(ev, 0);
}

// **La pregunta de la sonda es renta de la pantalla** (0.190.224, l'amo: *«I el suport per ansi ? no veig color enlloc»*). A un terminal de
// debò la sonda no es veu —és una pregunta, no una lletra—, però en un monitor que no ho és queda escrita com quatre caràcters i és el
// primer que llegeix una persona en arrencar la placa. Un retorn de carro, vuit espais i un altre retorn la tapen amb el que hi hagués a
// sota, i en un terminal de veritat no es veu res de res (docs/50, docs/85).
// **I es renta exactament el que s'ha escrit** (0.190.231, l'amo: *«8 espais?»*): vuit era el pitjor cas —les dues preguntes, `ESC [ 5 n` i
// `ESC [ 6 n`, quatre caràcters cada una— i quan només se n'havia fet una, els quatre espais de més quedaven a la pantalla com una línia en
// blanc. Cada camí diu quants caràcters ha escrit i el rentat en posa tants com cal (docs/50, docs/85).
void wash_the_probe(size_t chars) {
  char wash[16];
  if (chars + 2 > sizeof wash) chars = sizeof wash - 2;
  wash[0] = '\r';
  for (size_t i = 0; i < chars; ++i) wash[1 + i] = ' ';
  wash[1 + chars] = '\r';
  (void)::write(STDOUT_FILENO, wash, chars + 2);
}

// **La segona pregunta: quantes columnes té** (0.190.225). `linenoise` mesura l'amplada amb `␛ [ 6 n` i, si la resposta no li arriba bé, es
// queda amb una amplada que pot ser **zero** — i llavors `refreshMultiLine` **divideix per zero** (i en aquest xip, que no té divisió al
// maquinari, la rutina de la llibreria dona voltes per sempre) i `refreshSingleLine` fa baixar un `size_t` fins que dona la volta. Les dues
// coses són el mateix que va veure l'amo: `pal_line` clavat i l'`IDLE` del segon nucli sense menjar, o sigui la placa morta. O sigui que es
// pregunta **aquí primer**, es llegeix la resposta amb la seva forma (`␛ [ files ; columnes R`) i, si no és l'amplada d'un terminal de debò,
// aquesta banda diu que no i el shell es queda amb el seu editor de text pla (docs/50, docs/85).
int ask_the_width() {
  const int fd = fileno(stdin);
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0) return 0;
  (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  const char query[] = "\x1b[6n";
  (void)::write(STDOUT_FILENO, query, sizeof query - 1);
  char answer[24] = {};
  size_t used = 0;
  // **Cent cinquanta mil·lisegons i prou**: el mateix temps que es dona a la sonda, en passos de cinc, i el terminal que contesta ho fa en
  // microsegons (docs/50).
  for (int waited_ms = 0; waited_ms < 150; ++waited_ms) {
    char c = 0;
    const int got = read(fd, &c, 1);
    if (got == 1) {
      if (used + 1 < sizeof answer) {
        answer[used++] = c;
        answer[used] = '\0';
      }
      if (c == 'R') break;
      continue;
    }
    // **Qualsevol altra cosa és «encara no»**: el VFS del cable contesta zero quan no hi ha res i el de la IDF pot contestar -1; totes dues
    // volen dir el mateix i el que cal és esperar, no plegar (docs/50).
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  (void)fcntl(fd, F_SETFL, flags);
  if (used < 5 || answer[0] != '\x1b' || answer[1] != '[') return 0;
  int rows = 0;
  int cols = 0;
  if (sscanf(answer + 2, "%d;%d", &rows, &cols) != 2) return 0;
  if (rows < 1 || cols < 1) return 0;
  return cols;
}

// **La tasca de l'editor**: espera una línia, la guarda i torna a esperar. El que no s'hi fa és executar res: això és del torn.
void line_loop(void*) {
  for (;;) {
    char* line = linenoise(g_prompt[g_prompt_now]);
    if (line == nullptr) {
      // **Una línia que no s'ha pogut llegir no és una tasca morta**: la memòria s'ha cansat un moment i es torna a provar; una tasca
      // que surt del bucle deixaria el prompt sense editor per sempre i ningú no ho sabria (docs/50).
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    // **L'historial el porta el terminal i no el `line_editor` del shell**: una línia buida no hi entra, com no hi entrava abans
    // (docs/30).
    // **La primera línia diu quanta pila s'ha gastat** (0.190.225): és l'única mesura que diu si els vuit kilobytes són els que calen o si
    // encara balla, i es pren quan l'editor ja ha passat pel punt més fondo que sap passar-hi (docs/50, docs/75).
    if (!g_said_the_stack) {
      g_said_the_stack = true;
      const uint32_t left = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) * sizeof(StackType_t);
      hal::logf(pal::kLogInfo, "CONSOLE", "the line editor has %u byte(s) of stack left of %u", static_cast<unsigned>(left),
                static_cast<unsigned>(kTaskStack));
    }
    if (line[0] != '\0') (void)linenoiseHistoryAdd(line);
    post_a_line(line);
    linenoiseFree(line);
    // **I el prompt es dibuixa quan el shell ha acabat** (0.190.222): el dona `set_the_line_prompt`, i amb el sostre del davant una comanda
    // que triga no deixa la línia òrfena (docs/30, docs/50).
    if (g_ready != nullptr) (void)xSemaphoreTake(g_ready, pdMS_TO_TICKS(kPromptGraceMs));
  }
}

void line_trampoline(void* self) { line_loop(self); }

// **La consola a punt perquè algú l'esperi**: el driver darrere de `stdin`, el salt de línia d'un terminal i el `stdin` sense memòria
// intermèdia. El registre continua essent text pla: el color de les línies de la IDF és `CONFIG_LOG_COLORS` i no es toca (docs/50).
bool set_up_the_console_side() {
  // **La sonda** (0.190.222): `linenoise` mesura el prompt i l'amplada preguntant-ho al terminal (`␛ [ 6 n`) i **llegeix la resposta de
  // `stdin` esperant-se-la**; en un conducte que no és un terminal —el monitor de `pio`, `cat`, una canonada— no hi ha resposta, i el que
  // passaria és que el primer caràcter de cada línia es menjaria la sonda i sortiria escombraria. `linenoiseProbe()` pregunta el mateix amb
  // un temps límit (mig segon, sense bloquejar) i contesta si hi ha terminal a l'altre cap; si no n'hi ha, aquesta banda diu que no i el
  // shell es queda amb el seu editor, que és millor que un editor que es menja tecles (docs/30, docs/50, docs/85).
  //
  // **I quan es pregunta no és el mateix als dos costats, i és el detall que fa que això sigui portable** (0.190.222): al USB la sonda va
  // **abans** d'instal·lar el driver, perquè el driver li prendria el perifèric al camí de bytes i una sonda fallida deixaria la placa amb un
  // driver que ningú no llegiria per sobre de la seva lectura de sempre. A la UART va **després** d'enganxar el driver al VFS, perquè el
  // `read(0)` sense driver llegeix la FIFO del perifèric i el driver ja n'ha tret els bytes cap a la seva anella: la resposta hi seria i la
  // sonda no la veuria mai (docs/50).
#if LUNOK_LINE_CONSOLE_USB
  // **I la resposta de la sonda no es llença**: que l'altre cap contesti vol dir que entén els codis, i això es diu (0.190.223).
  g_ansi = (linenoiseProbe() == 0);
  if (!g_ansi) {
    wash_the_probe(kProbeBytes);
    return false;
  }
  // **I l'amplada, que és la que pot penjar l'editor** (0.190.225): sense una amplada de debò, el que hi ha a l'altre cap no és un terminal i
  // `linenoise` s'hi quedaria clavat (docs/50, docs/85).
  g_columns = ask_the_width();
  if (g_columns < kFewestColumns) {
    wash_the_probe(kProbeBytes + kWidthBytes);
    g_ansi = false;
    return false;
  }
  // **El driver del USB-Serial-JTAG, que és el que `linenoise` necessita llegir** (0.190.222): aquesta banda no en tenia cap des de la
  // 0.190.91 —llegeix la FIFO amb les crides de baix nivell— i el terminal en demana un, perquè `read(0)` ha de poder esperar. Si no
  // arrenca, el terminal no s'encén i el shell es queda amb el seu editor (docs/50).
  // **Camp a camp i no amb el macro de la IDF**: `USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT` és un inicialitzador amb designadors, que és C i
  // que aquest build —`gnu++11`— només accepta com a extensió. Tres línies i cap discussió amb el compilador (docs/45).
  usb_serial_jtag_driver_config_t config = {};
  config.tx_buffer_size = 256;
  config.rx_buffer_size = 256;
  if (usb_serial_jtag_driver_install(&config) != ESP_OK) return false;
  esp_vfs_usb_serial_jtag_use_driver();
  // **El salt de línia del terminal** (0.190.222): el d'un terminal és CR, i el que aquest sistema escriu és CRLF escrit a mà. La
  // conversió de baixada és CR→LF, que és el que `linenoise` espera, i la de pujada no es toca, o cada línia en tindria dos (docs/30).
  esp_vfs_dev_usb_serial_jtag_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
  esp_vfs_dev_usb_serial_jtag_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
#else
  // **I la UART, amb el driver que el PAL ja va instal·lar per la consola** (0.190.190): `use_driver` el posa darrere de `read(0)`, que
  // és el que el terminal demana, i el lector del PAL deixa de llegir mentre l'editor hi sigui (docs/50).
  esp_vfs_dev_uart_use_driver(UART_NUM_0);
  esp_vfs_dev_uart_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
  esp_vfs_dev_uart_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_LF);
  g_ansi = (linenoiseProbe() == 0);
  if (!g_ansi) {
    wash_the_probe(kProbeBytes);
    return false;
  }
  // **I l'amplada, que és la que pot penjar l'editor** (0.190.225): sense una amplada de debò, el que hi ha a l'altre cap no és un terminal i
  // `linenoise` s'hi quedaria clavat (docs/50, docs/85).
  g_columns = ask_the_width();
  if (g_columns < kFewestColumns) {
    wash_the_probe(kProbeBytes + kWidthBytes);
    g_ansi = false;
    return false;
  }
#endif
  // **El `stdin` sense memòria intermèdia**: amb la de newlib, un `read(0)` d'un byte esperaria una línia sencera i el cursor de la
  // línia no es mouria fins que la persona prem Enter, que és exactament el que el terminal ha de poder fer (docs/50).
  setvbuf(stdin, nullptr, _IONBF, 0);
  (void)fcntl(fileno(stdin), F_SETFL, 0);
  return true;
}

// **La taula que el tabulador completa**: els noms del shell, sense ajuda ni pistes. El que aquesta casa té de cada comanda —la frase
// de `help` i els seus arguments— viu al nucli i no es copia aquí; el terminal només ha de saber quins noms existeixen per oferir-los
// quan es prem el tabulador (docs/30, docs/45).
void register_the_names(const char* const* names, uint8_t count) {
  // **Igual que el driver del USB**: el macro d'Espressif és un inicialitzador amb designadors i això és `gnu++11`. Els quatre camps són
  // aquests i el que s'hi posa és el que aquest sistema necessita: la línia de cent vint-i-vuit bytes, setze arguments, i les pistes amb
  // el gris de la casa (docs/30, docs/45).
  esp_console_config_t config = {};
  config.max_cmdline_length = kLineBytes;
  // **Vuit arguments**: una línia d'aquest sistema, partida, no arriba més enllà; és un número que només limita el tabulador i que no
  // talla res del que es pot escriure (docs/30).
  config.max_cmdline_args = 16;
  config.hint_color = kHintColour;
  config.hint_bold = 0;
  if (esp_console_init(&config) != ESP_OK) return;
  for (uint8_t i = 0; i < count; ++i) {
    if (names == nullptr || names[i] == nullptr || names[i][0] == '\0') continue;
    esp_console_cmd_t cmd = {};
    cmd.command = names[i];
    // **`help` i `hint` buits i `func` nul**: el terminal no executa res —això és del shell— i el seu `help` no és el d'aquesta casa, que
    // és més llarg i més seu. El que se'n fa servir és la llista, que és el que `esp_console_get_completion` camina (docs/30, docs/45).
    cmd.help = nullptr;
    cmd.hint = nullptr;
    cmd.func = nullptr;
    (void)esp_console_cmd_register(&cmd);
  }
  // **El que fa que això sigui un terminal i no un `fgets`**: el tabulador completa amb la llista de dalt, i l'historial guarda el que
  // s'ha escrit. Les pistes queden enceses amb el mateix camí que la documentació mana (`linenoiseSetHintsCallback` vol el retorn
  // sense `const` i per això el cast), que avui no diu res perquè la taula no porta textos i demà dirà els arguments de cada comanda
  // (docs/30, docs/45).
  linenoiseSetCompletionCallback(&esp_console_get_completion);
  linenoiseSetHintsCallback(reinterpret_cast<linenoiseHintsCallback*>(&esp_console_get_hint));
  // **Les línies llargues es dobleguen en lloc de desplaçar-se dins d'una de sola** (0.190.222): una comanda d'aquest sistema pot tenir cent
  // vint-i-set caràcters i amb una sola línia el que s'escriu se surt de la pantalla per l'esquerra; el mode de diverses files és el que fa
  // que el cursor es mogui de fila quan toca (docs/30, docs/50).
  linenoiseSetMultiLine(1);
  // **I una línia buida és una línia** (0.190.222): l'Enter sense res obre la sessió del cable i atura un `log follow`, o sigui que el
  // terminal no pot tornar «no hi ha línia»; el valor d'Espressif ja és aquest, i es diu igualment perquè no depèn d'un defecte (docs/30).
  linenoiseAllowEmpty(true);
  (void)linenoiseHistorySetMaxLen(static_cast<int>(kHistory));
  (void)linenoiseSetMaxLineLen(kLineBytes - 1);
}

}  // namespace

bool start_the_line_editor(const char* const* names, uint8_t count) {
  // **Un terminal que va fallar es pot tornar a provar** (0.190.230, l'amo amb el registre a la mà: *«the other end of the cable does not
  // answer escape codes (no terminal)»*, i el monitor obert dins del VS Code): **la sonda es pregunta una vegada, al segon d'arrencar la
  // placa**, i si en aquell moment el monitor encara no hi era —o el seu terminal no havia contestat— la sessió sencera es quedava sense
  // editor. O sigui que això no és un pany: si l'editor ja hi és, sí; si la tasca no s'ha creat mai, es torna a preguntar (docs/30, docs/50).
  if (g_on) return true;
  if (g_task != nullptr) return false;
  // **La cua de les línies, abans de res**: una tasca que comenci sense cua no té on deixar el que llegeixi (docs/50).
  g_lines = xQueueCreate(kPendingLines, kLineBytes);
  if (g_lines == nullptr) return false;
  g_ready = xSemaphoreCreateBinary();
  if (g_ready == nullptr) {
    vQueueDelete(g_lines);
    g_lines = nullptr;
    return false;
  }
  // **I si la consola no es pot posar darrere del VFS, no hi ha terminal**: `linenoise` llegeix del descriptor zero, i un descriptor
  // que no bloqueja el tornaria amb una línia buida a cada volta —un bucle que es menja el torn i no espera ningú— o sigui que val més
  // dir que no i quedar-se amb l'editor del shell (docs/50).
  if (!set_up_the_console_side()) {
    vSemaphoreDelete(g_ready);
    g_ready = nullptr;
    vQueueDelete(g_lines);
    g_lines = nullptr;
    // **I es diu per què** (0.190.223): un terminal que no hi és i una consola que no pot esperar es veuen igual des del prompt, i són dues
    // coses diferents per a qui llegeixi el registre amb la placa a la mà (docs/30, docs/50, docs/85).
    if (g_ansi) {
      hal::logf(pal::kLogWarn, "CONSOLE", "the console cannot go through the VFS; the shell keeps its own line editor");
    } else {
      hal::logf(pal::kLogWarn, "CONSOLE",
                "the other end of the cable does not answer escape codes (no terminal): the shell keeps its own line editor; "
                "a terminal of truth answers -- tio, screen, minicom, or `pio device monitor --filter direct` -- and `terminal` at the "
                "prompt asks it again");
    }
    return false;
  }
  register_the_names(names, count);
  if (xTaskCreate(&line_trampoline, "pal_line", kTaskStack, nullptr, kTaskPriority, &g_task) != pdPASS) {
    vSemaphoreDelete(g_ready);
    g_ready = nullptr;
    vQueueDelete(g_lines);
    g_lines = nullptr;
    return false;
  }
  g_on = true;
  // **I el terminal es presenta**: que contesti els codis és la diferència entre un prompt amb cursor i colors i un que només té lletres,
  // i és una cosa que qui mira el registre ha de poder saber sense endevinar-la (0.190.223, docs/30, docs/50).
  hal::logf(pal::kLogInfo, "CONSOLE",
            "the terminal is up and speaks ANSI (cursor, colour, history) on %d column(s): the line is edited by the console component "
            "(%u command(s) to complete)",
            g_columns, static_cast<unsigned>(count));
  return true;
}

bool the_line_editor_is_on() { return g_on; }

bool the_terminal_speaks_ansi() { return g_ansi; }

bool the_line_editor_owns_the_console() { return g_on; }

void set_the_line_prompt(const char* text) {
  if (text == nullptr) return;
  const uint8_t spare = static_cast<uint8_t>(g_prompt_now ^ 1);
  snprintf(g_prompt[spare], kLineBytes, "%s", text);
  g_prompt_now = spare;
  // **I el shell ha parlat**: la línia següent ja es pot dibuixar amb la paraula que toca (docs/30, docs/50).
  if (g_ready != nullptr) (void)xSemaphoreGive(g_ready);
}

bool take_a_finished_line(char* out, size_t capacity) {
  if (g_lines == nullptr || out == nullptr || capacity == 0) return false;
  char block[kLineBytes];
  if (xQueueReceive(g_lines, block, 0) != pdPASS) return false;
  snprintf(out, capacity, "%s", block);
  return true;
}

}  // namespace pal
