// PAL for ESP32 + Arduino.
//
// Two rules live here and nowhere else: how to queue events (FreeRTOS) and how
// to talk to the serial port (Arduino Serial). The kernel sees neither.
// **Dues consoles i una sola casa** (0.190.90): amb el nucli d'Arduino la consola és `Serial`; sense el nucli és el perifèric que la
// placa hi té connectat —el USB-Serial-JTAG a la T5 E-Paper S3 Pro, la UART0 a la resta— i el que aquest fitxer li demana són cinc
// respostes: escriure, buidar, quants bytes hi ha, llegir-ne un i començar. La resta del fitxer no sap quina de les dues té a sota
// (docs/30, docs/50).
#include <driver/uart.h>
// **El USB-Serial-JTAG només existeix al xip que en té** (0.190.166): a l'ESP32 de tota la vida la consola és la UART0 i la capçalera
// d'aquell perifèric no hi és, o sigui que s'inclou només quan l'entorn diu que la consola és el USB (docs/50).
#if defined(LUNOK_CONSOLE_USB) && LUNOK_CONSOLE_USB
#include <hal/usb_serial_jtag_ll.h>
#endif
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <unistd.h>

#include <bootloader_random.h>
#include <driver/gpio.h>
#include <esp_cpu.h>
#include <esp_idf_version.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <stdarg.h>
#include <esp_rom_sys.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// **I el registre, que és de la HAL i no d'aquesta casa**: el fil del filferro diu si arrenca o no, i aquesta banda ja en parla des del
// terminal (0.190.243, and `console_line.cpp` in this same library does the same) (docs/50).
#include "hal/log.h"
#include <hal/uart_ll.h>
// **`sdkconfig.h` explícit**: en una llibreria del projecte no arriba sol, i el que hi ha aquí a sota és un `#if` sobre un símbol seu
// (docs/45). **El de la consola**, que diu si aquesta banda parla per la UART o pel USB-Serial-JTAG (docs/50).
#include "sdkconfig.h"

#include "esp32/console_line.h"
#include "pal/pal.h"

// **El color de la IDF i el color d'aquest sistema són dues coses, i el guardià d'abans ja no hi és** (0.190.227). Fins ahir hi havia un
// `#error` aquí que aturava la compilació si `CONFIG_LOG_COLORS` era encesa (l'amo: *«et vaig demanar desactivar l'ANSI de la configuració
// d'esp_idf… pots trobar-la i treure-ho?»*), perquè una línia de la IDF amb codis arribava al port sense que ningú els pogués treure. **Des
// de la 0.190.226 el color d'aquest sistema passa per un filtre** (`shell::filter_ansi`, `ansi on|off`) i l'amo ha encès `CONFIG_LOG_COLORS`
// a totes les bandes amb un `-D` a `platformio.ini`: el que queda del guardià és aquesta nota, perquè **el que la IDF escriu ella mateixa no
// passa pel filtre** —són les seves línies, no les del registre d'aquesta casa— i qui encén aquest símbol en una sessió sense terminal veurà
// els codis escrits. El `# CONFIG_LOG_COLORS is not set` dels `sdkconfig.defaults` s'hi queda: és el defecte de la placa, i el `-D` és la
// tria de la sessió (docs/50, docs/80, docs/85).

// **La consola de la placa quan no hi ha el nucli d'Arduino** (0.190.90): les cinc respostes que aquest fitxer demana, sobre el
// perifèric que la placa té. Els bytes que arriben es guarden en un anell petit perquè `available()` i `read()` —que és com aquest
// fitxer llegeix— preguntin sense perdre res (docs/30, docs/50).
// La consola és el USB quan l'entorn ho demana (`LUNOK_CONSOLE_USB`) i és la UART0 en tots els altres casos (docs/50).
// **I des de la 0.190.176 no hi ha cap altra veu**: `ARDUINO_USB_CDC_ON_BOOT` era la del nucli d'Arduino que aquest projecte ja no
// compila (docs/50).
#if defined(LUNOK_CONSOLE_USB) && LUNOK_CONSOLE_USB
#define LUNOK_HAVE_USB_CONSOLE 1
#else
#define LUNOK_HAVE_USB_CONSOLE 0
#endif

// **La cua d'esdeveniments del UART** (0.190.190): la ISR del driver hi deixa un event per ràfega de bytes, i el fil lector del PAL
// s'hi bloqueja. És d'aquí i no d'`EspSystem` perquè qui la crea és `LunokSerial::begin` —`uart_driver_install` vol la cua en mà— i qui
// l'espera és el fil, o sigui que ha de viure entre tots dos (docs/50).
// **Quants events del UART es poden acumular**: the reader empties it as fast as it can and the bytes stay in the driver's ring either
// way, o sigui que vuit és molt més del que cal i prou perquè una ràfega no en perdi cap (docs/50).
const int kUartEvents = 8;
static QueueHandle_t g_uart_events = nullptr;

// **Els dos rellotges d'aquest fitxer, abans de qui els fa servir** (0.190.243): the queue of the wire waits with a ceiling and needs the
// millisecond clock and a delay, i una funció que es declara després del seu lector no existeix per a ell (docs/45).
inline void lunok_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms > 0 ? ms : 1)); }
inline uint32_t lunok_now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

class LunokSerial {
 public:
  // **La ploma del filferro, feta amb l'objecte** (0.190.247): four tasks write here and asking for it later would be a lock that two of
  // them could make at once. Recursive, perquè qui té la ploma la torna a demanar quan baixa a `put_on_the_wire` (docs/50).
  LunokSerial() { pen_ = xSemaphoreCreateRecursiveMutex(); }

  // **Al USB, ni el driver de la IDF ni una interrupció pròpia: la FIFO es buida quan es pregunta** (0.190.91, el primer flaix que no
  // deixava teclejar). Amb el driver d'`esp_driver_usb_serial_jtag` la sortida hi era i l'entrada no —la placa parlava i no sentia—, i una
  // interrupció pròpia xoca amb l'enllaçador de l'xtensa quan el nostre `lib/` la posa a IRAM (`l32r: literal placed after use`). El que
  // sí que encaixa amb aquesta casa és el que fa: el lector del PAL pregunta cada **mil·lisegon** (0.190.190), i a 115 200 bauds això són
  // onze bytes en una FIFO de seixanta-quatre. Es buida sencera a l'anell i el torn llegeix d'allà (docs/30, docs/50).
  //
  // **I a l'UART, la cua del driver** (0.190.190): la ISR del perifèric hi deixa un event i el fil lector s'hi bloqueja, o sigui que
  // aquesta banda no pregunta res i la tecla no espera cap sondeig (docs/50).
  bool begin(uint32_t baud) {
#if LUNOK_HAVE_USB_CONSOLE
    (void)baud;
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
    uint8_t stale[64];
    while (usb_serial_jtag_ll_rxfifo_data_available() > 0) {
      if (usb_serial_jtag_ll_read_rxfifo(stale, sizeof(stale)) <= 0) break;
    }
    usb_serial_jtag_ll_txfifo_flush();
    (void)start_the_wire();
    return true;
#else
    uart_config_t config = {};
    config.baud_rate = static_cast<int>(baud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    // **El baud no pot sortir de l'APB** (0.190.177, la brossa al port de l'ATOM el 21/11): the UART takes its baud from its source
    // clock, and `UART_SCLK_DEFAULT` is the **APB** — so the moment the chip's power management moved the APB the console printed
    // rubbish until it moved back (`cpu 80 to 240 MHz` and the rest of the run garbled, seen on the board). **REF_TICK** is one
    // megahertz derived from the APB and held there when the APB changes, which is exactly what a console wants, and the S3 has the
    // crystal itself. The divisor of 115 200 on 1 MHz is 8,68 and the peripheral takes it with the fractional part (docs/50, docs/60).
#if defined(CONFIG_IDF_TARGET_ESP32)
    config.source_clk = UART_SCLK_REF_TICK;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    config.source_clk = UART_SCLK_XTAL;
#else
    config.source_clk = UART_SCLK_DEFAULT;
#endif
    // **La cua d'esdeveniments del driver és la interrupció** (0.190.190): with a queue, the UART's ISR posts `UART_DATA` and the
    // reader thread waits on it instead of asking `available()` every few milliseconds; els bytes no es toquen, perquè l'event només diu
    // que n'hi ha i qui els llegeix és el torn (docs/50).
    if (uart_driver_install(UART_NUM_0, 1024, 0, kUartEvents, &g_uart_events, 0) != ESP_OK) return false;
    if (uart_param_config(UART_NUM_0, &config) != ESP_OK) return false;
    // **I el fil del filferro, aquí** (0.190.243): the terminal has to be up and waiting before the first line of the system, and a
    // serial that did not come up writes from the caller as it always did (docs/50).
    (void)start_the_wire();
    return true;
#endif
  }

  void setDebugOutput(bool) {}

  // **El filferro té el seu propi fil** (0.190.243, l'amo: *«el terminal serie ha de ser multitasca. El terminal s'ha d'inicialitzar i
  // quedar-se a l'espera. Anirà a una tasca del sistema per poder seguir atenent al usuari encara que la pantalla estigui robant molt
  // temps de torns»*).
  //
  // **Què passava sense això**: `write()` posava els bytes al UART *des del torn*, i el driver d'aquesta casa no té anella de sortida
  // —`uart_driver_install(…, 1024, 0, …)`—, o sigui que una línia de registre, una taula de `help` o el dibuix de l'entrada **esperaven
  // el filferro dins del torn**: mil cent bytes a 115 200 bauds són noranta mil·lisegons que el nucli no pot mirar ni el tàctil ni el
  // panell. Amb el fil del filferro, el torn només **deixa els bytes a la cua** i torna de seguida; qui espera la línia és una tasca.
  //
  // **I és una cua i no una escriptura directa quan hi ha editor**: el dibuix del prompt i l'eco de `linenoise` passen per `::write(1)`
  // —són del component i no es poden interceptar sense posar-hi un driver de VFS propi—, o sigui que abans de tocar el filferro per
  // aquesta banda es buida la cua (`flush`), i el torn no pot deixar res entremig d'un redibuix (docs/30, docs/50).
  //
  // **La mida és de dos kilobytes i surt del munt**: una cua fixa d'aquestes és DRAM que la Paper no té (docs/75) i un `malloc` d'aquest
  // munt acaba a la PSRAM quan n'hi ha. Si el munt no en dona, `write` segueix essent el de sempre i ho diu al registre: una placa sense
  // fil del filferro ha d'escriure igualment.
  static const size_t kWireBytes = 2048;
  static const size_t kFullMostMs = 200;   // what a caller waits for room in the queue, and not a tick more

  bool start_the_wire() {
    if (wire_on_) return true;
    if (wire_ == nullptr) wire_ = static_cast<uint8_t*>(malloc(kWireBytes));
    if (wire_lock_ == nullptr) wire_lock_ = xSemaphoreCreateMutex();
    if (wire_wake_ == nullptr) wire_wake_ = xSemaphoreCreateBinary();
    if (wire_ == nullptr || wire_lock_ == nullptr || wire_wake_ == nullptr) {
      hal::logf(pal::kLogWarn, "CONSOLE", "no memory for the queue of the wire: the console writes from the caller as it always did");
      return false;
    }
    if (xTaskCreate(&LunokSerial::wire_trampoline, "pal_tx", kWireStack, this, kWirePriority, nullptr) != pdPASS) {
      hal::logf(pal::kLogWarn, "CONSOLE", "the task of the wire did not start: the console writes from the caller as it always did");
      return false;
    }
    wire_on_ = true;
    hal::logf(pal::kLogInfo, "CONSOLE", "the wire is its own task (%u bytes of queue)", static_cast<unsigned>(kWireBytes));
    return true;
  }

  size_t write(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) return 0;
    if (!wire_on_) return put_on_the_wire(data, len);

    // **El torn deixa els bytes i se'n va**: what makes this a queue and not a wait is that only a full queue makes the caller wait,
    // and then with a ceiling. A line of the log is forty bytes and a screen is a couple of thousand, so the ceiling is the wire and
    // not the queue.
    //
    // **I cada escriptura és sencera o no és** (0.190.247): el tros que aquesta crida deixa a la cua es copia amb la ploma a la mà, o sigui
    // que dues escriptures de tasques diferents mai no es barregen dins de la cua. **La ploma no s'espera amb la cua plena**: qui la buida és
    // `pal_tx`, i si qui espera tingués la ploma, el filferro no avançaria mai — el que es fa és deixar-la anar, esperar un mil·lisegon i
    // tornar-ho a provar, que és exactament el que aquesta cua ha fet sempre (docs/50).
    size_t done = 0;
    const uint32_t started = lunok_now_ms();
    while (done < len) {
      const size_t piece = put_in_the_queue(data + done, len - done);
      done += piece;
      if (done < len) {
        if (lunok_now_ms() - started >= kFullMostMs) break;
        lunok_delay_ms(1);
      }
    }
    return done;
  }

  // **El tros que es deixa a la cua, amb la ploma a la mà** (0.190.247): what fits goes in whole and nothing else can be written between its
  // first and its last byte. When the whole write does not fit, what fits goes in anyway —una escriptura gran s'escriu a trossos i el que no
  // pot passar és que s'hi colin *dins*— i el que queda espera que el filferro buidi (docs/50).
  size_t put_in_the_queue(const uint8_t* data, size_t len) {
    Pen pen(pen_);
    size_t piece = 0;
    (void)xSemaphoreTake(wire_lock_, portMAX_DELAY);
    const size_t room = kWireBytes - wire_used_;
    if (room > 0) {
      piece = (len < room) ? len : room;
      for (size_t i = 0; i < piece; ++i) {
        wire_[(wire_head_ + i) % kWireBytes] = data[i];
      }
      wire_head_ = (wire_head_ + piece) % kWireBytes;
      wire_used_ = wire_used_ + piece;
    }
    (void)xSemaphoreGive(wire_lock_);
    if (piece > 0) (void)xSemaphoreGive(wire_wake_);
    return piece;
  }

  // **I el buidatge espera la cua abans que el registre del perifèric**: what a caller wants from `flush` is that the words are on the
  // wire, and they are not while the queue still holds them (docs/50, docs/60). **La ploma no es té mentre s'espera la cua** (0.190.247):
  // qui la buida és `pal_tx` i un pany a la mà de qui espera seria un pany mort; es pren després, per esperar el perifèric, que és l'últim
  // tros del camí.
  void flush() {
    if (wire_on_) {
      const uint32_t started = lunok_now_ms();
      while (wire_used_ > 0) {
        if (lunok_now_ms() - started >= kFlushMostMs) break;
        lunok_delay_ms(1);
      }
    }
    Pen pen(pen_);
    wait_the_wire();
  }
  void flush(bool) { flush(); }

 private:
  // **Una sola ploma al filferro** (0.190.247, l'amo: *«escriptura a port serie ha de ser exclusiva (shell, vs tota la resta del codi)»*):
  // the guard takes the lock at the door and gives it back on every way out, o sigui que el que no es barreja és l'escriptura sencera i no
  // el tros. És recursiu perquè `write` la té i baixa a `put_on_the_wire` (docs/30, docs/50).
  struct Pen {
    explicit Pen(SemaphoreHandle_t lock) : pen(lock) {
      if (pen != nullptr) (void)xSemaphoreTakeRecursive(pen, portMAX_DELAY);
    }
    ~Pen() {
      if (pen != nullptr) (void)xSemaphoreGiveRecursive(pen);
    }
    Pen(const Pen&) = delete;
    Pen& operator=(const Pen&) = delete;
    SemaphoreHandle_t pen;
  };

  SemaphoreHandle_t pen_ = nullptr;
  static const size_t kWireStack = 3072;
  static const uint8_t kWirePriority = 2;   // above the turn and the reader, below the radios
  static const uint32_t kFlushMostMs = 100;

  static void wire_trampoline(void* self) {
    static_cast<LunokSerial*>(self)->wire_loop();
  }

  // **La tasca del filferro**: waits for the turn to leave something and puts it on the wire, chunk by chunk. What it takes out of the
  // queue is contiguous —a ring gives a run up to its end— and a write of a run is one `put_on_the_wire`, o sigui que dues línies de
  // registre mai no es barregen encara que vinguin de tasques diferents.
  void wire_loop() {
    static const size_t kChunk = 512;   // a run at a time: the stack of this task is three kilobytes and not eight
    uint8_t chunk[kChunk];
    for (;;) {
      (void)xSemaphoreTake(wire_wake_, portMAX_DELAY);
      while (true) {
        size_t took = 0;
        (void)xSemaphoreTake(wire_lock_, portMAX_DELAY);
        if (wire_used_ > 0) {
          const size_t tail = (wire_head_ + kWireBytes - wire_used_) % kWireBytes;
          took = (kWireBytes - tail < wire_used_) ? (kWireBytes - tail) : wire_used_;
          if (took > kChunk) took = kChunk;
          for (size_t i = 0; i < took; ++i) chunk[i] = wire_[tail + i];
          // **Fora del `-=`** (0.190.248): `wire_used_` és `volatile` perquè `flush` el llegeix sense el pany, i una assignació composta sobre un
          // `volatile` està desaconsellada pel compilador —i amb `-Werror` d'aquesta casa, que és un error— (docs/50).
          wire_used_ = wire_used_ - took;
        }
        (void)xSemaphoreGive(wire_lock_);
        if (took == 0) break;
        (void)put_on_the_wire(chunk, took);
      }
    }
  }

  // **El que posa els bytes al perifèric**, i el de sempre: la ploma única del terminal, la FIFO del USB o l'`uart_write_bytes`.
  size_t put_on_the_wire(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) return 0;
    Pen pen(pen_);
    // **Una sola ploma** (0.190.222): amb el terminal enlaire, el que escriu el nucli i el que dibuixa el cursor de la línia han de sortir
    // pel mateix camí —el que el driver dona a `stdout`—, o dues escriptures al mateix filferro es trepitjarien i una línia del registre
    // cauria al mig d'una comanda que s'està editant (docs/30, docs/50).
    if (pal::the_line_editor_owns_the_console()) {
      size_t done = 0;
      while (done < len) {
        const int wrote = static_cast<int>(::write(STDOUT_FILENO, data + done, len - done));
        if (wrote <= 0) break;
        done += static_cast<size_t>(wrote);
      }
      return done;
    }
#if LUNOK_HAVE_USB_CONSOLE
    // La FIFO del perifèric és de seixanta-quatre bytes i el xip la buida sol; s'escriu el que hi cap i s'espera el torn. **El sostre de
    // voltes és el mateix criteri que el del buidatge**: una línia perduda és pitjor que res, però la placa no es pot quedar aquí (docs/50).
    size_t done = 0;
    while (done < len) {
      int spins = 0;
      while (!usb_serial_jtag_ll_txfifo_writable()) {
        if (++spins > 200000) return done;
      }
      const uint32_t room = (len - done > 64) ? 64u : static_cast<uint32_t>(len - done);
      const int sent = usb_serial_jtag_ll_write_txfifo(data + done, room);
      if (sent <= 0) break;
      done += static_cast<size_t>(sent);
      usb_serial_jtag_ll_txfifo_flush();
    }
    return done;
#else
    const int sent = uart_write_bytes(UART_NUM_0, data, len);
    return (sent > 0) ? static_cast<size_t>(sent) : 0;
#endif
  }

  // **El sostre de cent mil·lisegons és el de sempre** (0.190.69): una placa que ha de tallar el corrent no pot esperar un fil que no
  // s'acaba; el que es perd si el fil no s'acaba és mitja línia del registre i el que es perdia sense el sostre era la placa. **I des de
  // la 0.190.243 això és el segon pas del buidatge**: primer la cua del fil i després el registre del perifèric, o el que es quedés a la
  // cua no arribaria mai (docs/50, docs/60).
  void wait_the_wire() {
#if LUNOK_HAVE_USB_CONSOLE
    // **Amb el terminal, la FIFO no és nostra** (0.190.222): el driver del USB-Serial-JTAG té el perifèric i el seu anella, i empènyer
    // la FIFO amb les crides de baix nivell seria ficar-hi la mà a mitges —el que s'ha escrit pel VFS ja surt sol—. Es deixa passar el
    // mateix mil·lisegon de sempre i prou (docs/50).
    if (pal::the_line_editor_owns_the_console()) {
      vTaskDelay(pdMS_TO_TICKS(1));
      return;
    }
    // **El que hi ha a la FIFO surt ara** (0.190.91): el perifèric no té registre de desplaçament que esperar, i el que queda per
    // enviar el porta la línia USB; això empeny el que ja és dins (docs/50).
    usb_serial_jtag_ll_txfifo_flush();
    vTaskDelay(pdMS_TO_TICKS(1));
#else
    (void)uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
#endif
  }

 public:

  int available() {
#if LUNOK_HAVE_USB_CONSOLE
    take_what_the_fifo_has();
    return static_cast<int>(count_);
#else
    size_t waiting = 0;
    (void)uart_get_buffered_data_len(UART_NUM_0, &waiting);
    return static_cast<int>(waiting);
#endif
  }

  int read() {
#if LUNOK_HAVE_USB_CONSOLE
    if (count_ == 0) return -1;
    const int byte = ring_[head_];
    head_ = static_cast<uint16_t>((head_ + 1) % sizeof(ring_));
    --count_;
    return byte;
#else
    uint8_t byte = 0;
    return (uart_read_bytes(UART_NUM_0, &byte, 1, 0) == 1) ? byte : -1;
#endif
  }

 private:
  // **La cua del filferro i la seva tasca** (0.190.243): the bytes the turn has handed over and the task that puts them on the wire. The
  // ring is a `malloc` of two kilobytes —DRAM is what the Paper does not have— and the two semaphores are the lock of the ring and the
  // shout that says there is work.
  uint8_t* wire_ = nullptr;
  size_t wire_head_ = 0;            // where the next byte of the queue goes
  volatile size_t wire_used_ = 0;   // how many are waiting, and read by `flush` without the lock
  SemaphoreHandle_t wire_lock_ = nullptr;
  SemaphoreHandle_t wire_wake_ = nullptr;
  volatile bool wire_on_ = false;

#if LUNOK_HAVE_USB_CONSOLE
  // **El que el perifèric té, cap a l'anell** (0.190.91): la FIFO és de seixanta-quatre bytes i l'anell de dos-cents cinquanta-sis, o
  // sigui que una ràfega curta hi cap sencera i la llegeix el torn del nucli. L'anell ple deixa caure el byte, que és el mateix que fa
  // la FIFO del xip quan ningú no la buida (docs/50).
  void take_what_the_fifo_has() {
    uint8_t chunk[64];
    while (count_ < sizeof(ring_) && usb_serial_jtag_ll_rxfifo_data_available() > 0) {
      const int got = usb_serial_jtag_ll_read_rxfifo(chunk, sizeof(chunk));
      if (got <= 0) return;
      for (int i = 0; i < got; ++i) {
        ring_[tail_] = chunk[i];
        tail_ = static_cast<uint16_t>((tail_ + 1) % sizeof(ring_));
        ++count_;
      }
    }
  }

  uint8_t ring_[256];
  uint16_t head_ = 0;
  uint16_t tail_ = 0;
  uint16_t count_ = 0;
#endif
};

LunokSerial g_lunok_serial;
#define LUNOK_SERIAL g_lunok_serial
inline void lunok_reboot() { esp_restart(); }
inline bool lunok_serial_begin() { return g_lunok_serial.begin(115200); }

namespace pal {
namespace {

const uint16_t kQueueLength = 16;
// **El que queda de sondeig** (0.190.190): la banda del UART ja no en té (la cua del driver la desperta), i la del USB-Serial-JTAG
// —que es llegeix a mà— mira la FIFO d'un en un. Cinc mil·lisegons eren un quart de dècima de segon de latència per tecla que no calia.
const uint32_t kReaderPeriodMs = 1;

// **The noise of this boot, taken once** (0.174.8). The chip's hardware RNG is a true one only while the radio is on
// —on this family the number is read out of the Wi-Fi peripheral's register, `WDEV_RND_REG`, and the entropy is the RF
// noise (docs/50)— and this system keeps the radio off until something asks for it, so at boot a `esp_random()` can hand
// back the same word every time. The chip's own answer for that case is `bootloader_random_enable()`: it mixes the SAR
// ADC's internal noise into the generator, is documented for use from the app early in the run, and **has to be let go
// before anything else touches the ADC** (the battery gauge of the Paper is an ADC pin, docs/50). So this is the whole
// window: enable, take eight bytes, disable — before the first widget of the system exists.
//
// **And what is mixed with it** is the microsecond count since the reset, because two boots of the same board are two
// different moments even when the amplifier of the ADC says the same thing twice.
//
// **Whether the radio of the chip is up** (0.178.6), which is the one question that decides whether the chip's generator
// is a true one: `esp_random()` is documented as true random **while Wi-Fi or Bluetooth is enabled**, because the entropy
// is the RF noise of the peripheral (docs/50). The driver answers `ESP_ERR_WIFI_NOT_INIT` when nobody has brought it up,
// and the mode is null while it is not started, so this is "the radio is on" and not "somebody asked for Wi-Fi once".
bool radio_is_up() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  return esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_NULL;
}

uint32_t mix32(uint32_t x) {
  x += 0x9E3779B9u;
  x = (x ^ (x >> 16)) * 0x21F0AAADu;
  x = (x ^ (x >> 15)) * 0x735A2D97u;
  return x ^ (x >> 15);
}

// **The last word this board drew, kept where a reset does not reach it** (0.178.8). The RTC memory of the chip is the
// one place that survives a reset, a panic and a deep sleep without touching the flash —which wears— and it is where
// the hour of `/rtc` lives too (docs/50, docs/70). **It does not survive the power going away**, and that is said in
// the docs: a board that has been unplugged starts from the noise of its own boot, which is exactly the case the ADC
// window exists for. With the last word mixed into the seed, **two runs of the same board cannot share a sequence**
// even if the entropy source hands back the same bytes twice, which was the fault this is here to close (docs/20).
//
// `RTC_NOINIT_ATTR` and not `RTC_DATA_ATTR`: an initialised variable is written on every boot and would erase what the
// boot before it left. The block is read as garbage the first time the power comes on, so it carries a magic and a
// check and a block that does not pass them is not mixed —it is not trusted, and it does not stop the boot either—.
struct Kept {
  uint32_t magic;
  uint32_t state;
  uint32_t serial;
  uint32_t check;
};
RTC_NOINIT_ATTR Kept g_kept;
const uint32_t kKeptMagic = 0x524E4721u;  // "RNG!"

uint32_t kept_check(uint32_t state, uint32_t serial) { return mix32(state ^ mix32(serial ^ kKeptMagic)); }

bool kept_is_sound() { return g_kept.magic == kKeptMagic && g_kept.check == kept_check(g_kept.state, g_kept.serial); }

void kept_write(uint32_t state, uint32_t serial) {
  g_kept.state = state;
  g_kept.serial = serial;
  g_kept.check = kept_check(state, serial);
  g_kept.magic = kKeptMagic;
}

struct Random {
  bool seeded = false;
  uint32_t state = 0;

  void seed() {
    if (seeded) return;
    seeded = true;
    uint32_t noise[2] = {0, 0};
    bootloader_random_enable();
    esp_fill_random(noise, sizeof(noise));
    bootloader_random_disable();
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time());
    // **And the word the run before left in the RTC memory**, if it left one: a reset is not a new world, and a board
    // whose entropy source says the same thing twice still gets two different sequences because the second boot knows
    // what the first one drew (docs/50).
    const uint32_t before = kept_is_sound() ? g_kept.state : 0;
    const uint32_t serial = kept_is_sound() ? (g_kept.serial + 1u) : 1u;
    // A zero state is a mixer that answers zero for ever, and `mix32` of anything is not zero.
    state = mix32(noise[0] ^ mix32(noise[1] ^ us ^ mix32(before ^ serial))) | 1u;
    kept_write(state, serial);
  }

  // **With the radio up the number is the chip's** (0.178.6): while the radio is on, `esp_random()` is a true random
  // word —the noise of the RF stage, docs/50— and that is what an app gets, so a session with Wi-Fi is not a session of
  // xorshift32. **And each of those words stirs the state of the mixer**, so that the sequence that answers when the
  // radio goes back down is not the one seeded at the boot any more. With the radio off this is the xorshift32 of the
  // boot, as it always was, because asking the chip then is asking a register that does not refresh (docs/20, docs/50).
  uint32_t next() {
    seed();
    if (radio_is_up()) {
      const uint32_t word = esp_random();
      state ^= mix32(word);
      if (state == 0) state = 1u;
      // **The word of the chip is the one the next boot should know**: it is the last thing this board said, and it is
      // written where a reset does not reach (a write to the RTC memory, not to the flash) (docs/50).
      kept_write(word, g_kept.serial);
      return word;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    kept_write(state, g_kept.serial);
    return state;
  }
};
Random g_random;

class EspSystem : public ISystem, public IQueue, public IConsole {
 public:
  bool bring_up() {
    q_ = xQueueCreate(kQueueLength, sizeof(Event));
    if (q_ == nullptr) return false;
    // The reader only looks at how many bytes are waiting and posts an event.
    // It never reads them: only the kernel reads the console.
    if (xTaskCreate(&EspSystem::reader_trampoline, "pal_rx", 2048, this, 1, nullptr) != pdPASS) {
      return false;
    }
    return true;
  }

  // ISystem -----------------------------------------------------------------

  IQueue* queue() override { return this; }
  IConsole* console() override { return this; }

  int64_t millis() const override {
    return static_cast<int64_t>(esp_timer_get_time() / 1000);
  }

  int64_t micros() const override { return esp_timer_get_time(); }

  void reboot() override { lunok_reboot(); }
  bool reboot_requested() const override { return false; }

  uint32_t random() override { return g_random.next(); }

  HeapInfo heap() const override {
    HeapInfo info;
    info.valid = true;
    // Asked by capability and not with `ESP.getFreeHeap()`, which is one number for a board that now
    // has two kinds of memory: this is the board's own RAM, which is what the system and the drivers
    // need. What the PSRAM has left is the board's answer, in `IPlatform::extra_memory` (docs/75).
    info.free_bytes =
        static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    info.min_free_bytes =
        static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    // **What a page could still take**: the pieces do not add up, and the board has the number of the
    // biggest one (docs/75).
    info.largest_free_bytes =
        static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return info;
  }

  // **What is left of the stack of this task, at its lowest point**: FreeRTOS counts the words nobody has
  // touched since the task started, and a task whose stack overflows does not report it — it corrupts a
  // backtrace and reboots (measured on the Paper with `run buttons`, 2/11, docs/75).
  int32_t stack_free() const override {
    const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
    return static_cast<int32_t>(words * sizeof(StackType_t));
  }

  // IQueue ------------------------------------------------------------------

  bool push(const Event& ev, uint32_t timeout_ms) override {
    if (q_ == nullptr) return false;
    return xQueueSend(q_, &ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  }

  // **L'empenta d'una interrupció, i aquesta sí que és una interrupció de debò** (0.190.206): all of it in IRAM —the object is reached
  // through the global and not through a virtual table, and what is left (`xQueueSendFromISR`, `xTaskGetSchedulerState`) is already
  // there in this IDF. **No virtual calls and no flash**: el gestor del tàctil és a la IRAM perquè pot arribar mentre la memòria cau
  // de flash està apagada, i una crida a la flash en aquell moment no espera — executa brossa, i el xip acaba en un
  // `Interrupt wdt timeout` amb la traça dins d'aquest mateix camí (docs/50, docs/95).
  IRAM_ATTR bool wake_from_isr() override { return wake_an_isr_here(); }

  // Non-virtual on purpose: the door below calls this one, so no table and no thunk can be reached from an interrupt (0.190.206).
  IRAM_ATTR bool wake_an_isr_here() {
    if (q_ == nullptr) return false;
    Event ev = {};
    ev.type = kEventTouch;
    BaseType_t woken = pdFALSE;
    const bool ok = xQueueSendFromISR(q_, &ev, &woken) == pdTRUE;
    // **I el relleu només si el planificador el pot prendre**: una escriptura a la flash suspèn el planificador i tot seguit tanca les
    // interrupcions que no són a la IRAM, i demanar-hi un canvi de context és demanar-lo dins d'una secció crítica. No es perd res: la
    // tasca ja és a punt dins la cua i `xTaskResumeAll` fa el canvi que toca.
    if (woken == pdTRUE && xTaskGetSchedulerState() != taskSCHEDULER_SUSPENDED) portYIELD_FROM_ISR();
    return ok;
  }

  // **L'antiga porta, que és la de les proves i la de qui no és una interrupció**: does the same and goes through the table, which is
  // exactly what an interrupt must not do (docs/50).
  bool push_from_isr(const Event& ev) override {
    (void)ev;
    return wake_from_isr();
  }

  bool pop(Event& out, int32_t timeout_ms) override {
    if (q_ == nullptr) return false;
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(q_, &out, ticks) == pdTRUE;
  }

  uint16_t size() const override {
    if (q_ == nullptr) return 0;
    return static_cast<uint16_t>(uxQueueMessagesWaiting(q_));
  }

  uint16_t capacity() const override { return kQueueLength; }

  // IConsole ----------------------------------------------------------------

  size_t write(const void* data, size_t len) override {
    return LUNOK_SERIAL.write(static_cast<const uint8_t*>(data), len);
  }

  // The UART keeps a FIFO and a shift register after write() returns, and both stop when
  // the chip sleeps: without this wait the last line before a sleep arrives cut in half
  // (docs/50, docs/60).
  //
  // **I amb un sostre, perquè una placa que s'ha d'adormir no es pot quedar esperant un fil** (0.190.69): `Serial.flush()` espera
  // que l'FSM del UART estigui idle en un bucle sense temps límit, i la nit del 20/11 la placa es va quedar penjada exactament aquí
  // —sense consola, sense tàctil i sense anar-se'n a dormir, perquè el camí de la baixada no torna mai i el torn no avança—. El que
  // es perd si el fil no s'acaba és mitja línia del registre; el que es perdia sense el sostre era la placa (docs/50, docs/60).
  void flush() override {
#if LUNOK_HAVE_USB_CONSOLE
    // **Amb la consola al USB no hi ha cap UART del xip a esperar** (0.190.30): the bytes go out through the USB peripheral and its
    // own stack, so the bound below is about a wire this board does not have —and UART0 here is the GPS of that board and the
    // interrupt line of its touch (docs/50).
    LUNOK_SERIAL.flush();
#else
    // **Cent mil·lisegons és el sostre d'aquesta espera**: a 115200 bauds una línia del registre són nou mil·lisegons, i una placa que
    // ha de tallar el corrent no pot esperar més que això per una línia de text. És un sostre i no una espera: un UART sa el compleix
    // en microsegons (docs/50, docs/60).
    const uint32_t kFlushMostMs = 100;
    const uint32_t started = lunok_now_ms();
    while (!uart_ll_is_tx_idle(UART_LL_GET_HW(0))) {
      if (lunok_now_ms() - started >= kFlushMostMs) return;
      lunok_delay_ms(1);
    }
#endif
  }

  // **El terminal del cable** (0.190.222): mentre edita la línia, aquesta consola no llegeix ni escriu pels camins de sempre —l'editor
  // té el `stdin`/`stdout` i el que arriba és una línia sencera, no bytes—, i el nucli ho pregunta abans de tocar res (docs/30, docs/50).
  bool has_a_line_editor() const override { return pal::the_line_editor_is_on(); }

  // **Un cable pot portar codis encara que el monitor no contesti** (0.190.231): són dues coses diferents i fins avui aquesta porta les
  // barrejava. *Contestar* (la sonda) és el que necessita l'editor de línia per mesurar el prompt i l'amplada; *pintar* el que arriba és una
  // altra cosa, i el monitor de `pio` pinta i no contesta mai perquè es menja les respostes del terminal. O sigui que per a aquesta banda
  // la resposta és **sí**, i qui decideix si els codis surten és el commutador `ansi on|off` (docs/30, docs/50, docs/85).
  bool speaks_ansi() const override { return true; }

  // **I la mida de la vista** (0.190.234): la pregunta la fa el mòdul del terminal un sol cop, quan s'encén, i aquí només es diu el que ha
  // contestat. Un terminal que no ho ha dit contesta que no, i qui dibuixa aleshores no pot alinear res a la dreta (docs/50, docs/85).
  bool the_size(int& rows, int& columns) const override { return pal::the_terminal_size(rows, columns); }

  bool start_the_terminal(const char* const* names, uint8_t count) override {
    return pal::start_the_line_editor(names, count);
  }

  void set_the_prompt(const char* text) override { pal::set_the_line_prompt(text); }

  bool take_a_line(char* out, size_t capacity) override { return pal::take_a_finished_line(out, capacity); }
  bool wait_for_a_line(char* out, size_t capacity, int32_t timeout_ms) override {
    return pal::take_a_finished_line(out, capacity, timeout_ms);
  }

  // **I l'historial, que pot sobreviure al reinici** (0.190.236): `linenoise` el té a la memòria i el desa en un fitxer del VFS; quin disc
  // és el que mana ho ha decidit el nucli abans de cridar aquí (docs/30, docs/50, docs/70).
  bool load_the_history(const char* path) override { return pal::load_the_line_history(path); }
  bool save_the_history(const char* path) override { return pal::keep_the_line_history(path); }

  size_t read(void* data, size_t len) override {
    // **Amb l'editor a sobre, qui llegeix és ell**: un segon lector es menjaria les tecles que la persona escriu (docs/50).
    if (pal::the_line_editor_owns_the_console()) return 0;
    uint8_t* out = static_cast<uint8_t*>(data);
    size_t n = 0;
    while (n < len && LUNOK_SERIAL.available() > 0) {
      const int c = LUNOK_SERIAL.read();
      if (c < 0) break;
      out[n++] = static_cast<uint8_t>(c);
    }
    return n;
  }

  // **La línia diu quan ha callat** (0.190.21): the UART counts the edges of its RX pin, shows the level of
  // the line and holds the bytes that have not been read; a byte in flight is an edge, a break is a low line,
  // and a byte in the FIFO or in the driver's ring is a byte. The chip's RX timeout is this same fact
  // (`UART_RXFIFO_TOUT_INT`, at most 126 symbols, ~12 ms at 115200); reading the counter here costs no event
  // task and no second consumer of UART0 (docs/50).
  bool wait_quiet(uint32_t quiet_ms, uint32_t cap_ms) override {
    // **Amb la consola al USB no hi ha cap línia a vigilar** (0.190.30): a board whose chip puts its console on the
    // USB CDC —the T5 E-Paper S3 Pro does, it has no bridge chip— has no UART to watch, and reading UART0 anyway is
    // doubly wrong there: its RX pin is somebody else's (the GPS of that board) and the pull-up this used to put on
    // GPIO3 is the interrupt line of its touch controller. The bytes of a CDC arrive when the host writes them and
    // there is no line to settle, o sigui que la resposta honesta és que ja ha callat (docs/50).
#if LUNOK_HAVE_USB_CONSOLE
    (void)quiet_ms;
    (void)cap_ms;
    return true;
#else
    uart_dev_t* hw = UART_LL_GET_HW(0);
    // On a battery boot the bridge at the other end has no power and the pin floats; the pull-up is what makes
    // "high and still" mean "nobody is talking" instead of "the wire is picking up the room" (docs/50).
    gpio_pullup_en(GPIO_NUM_3);
    // Whatever arrived while the system was not looking is dropped, as the boot did with its first bytes.
    LUNOK_SERIAL.flush(false);
    if (quiet_ms == 0) quiet_ms = 1;

    const uint32_t started = lunok_now_ms();
    uint32_t edges = uart_ll_get_rxd_edge_cnt(hw);
    uint32_t still_since = started;
    for (;;) {
      while (LUNOK_SERIAL.available() > 0) LUNOK_SERIAL.read();
      const uint32_t now = lunok_now_ms();
      const uint32_t seen = uart_ll_get_rxd_edge_cnt(hw);
      const bool high = hw->status.rxd != 0;
      const bool bytes = uart_ll_get_rxfifo_len(hw) > 0;
      if (!high || bytes || seen != edges) {
        edges = seen;
        still_since = now;
      } else if ((now - still_since) >= quiet_ms) {
        return true;
      }
      if ((now - started) >= cap_ms) return false;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
#endif  // LUNOK_HAVE_USB_CONSOLE
  }

 private:
  static void reader_trampoline(void* self) {
    static_cast<EspSystem*>(self)->reader_loop();
  }

  // **El fil que avisa que hi ha una tecla** (0.190.190): it never reads a byte —the turn does—, it only says that there is something
  // waiting. On the UART that means blocking on the driver's queue, which an interrupt fills; a banda del USB, que no en té, mira la
  // FIFO cada mil·lisegon (docs/50).
  void reader_loop() {
    for (;;) {
      // **El sondeig, que és el de sempre** (0.190.225, l'amo: *«recordes que em vas dir que tornesim al pooling? … va doncs, provem-ho (no
      // esborris el codi actual de la interrupció, comenta'l)»*): aquest fil mira el port cada mil·lisegon i diu al torn què hi ha. És el
      // camí que aquesta casa tenia abans de la 0.190.190, quan la cua del driver va substituir el sondeig.
      //
      // **I la cua del driver es buida a cada volta, tota**: amb el sondeig ningú més no la llegeix, i una cua plena fa que la ISR de la
      // UART cridi des de la interrupció (`event queue full`), que és una avaria al registre que no és cap avaria i que es paga amb temps
      // de filferro dins d'una ISR (docs/50).
      drain_the_events();
      vTaskDelay(pdMS_TO_TICKS(kReaderPeriodMs));
      // **Amb el terminal enlaire aquest fil no té més feina**: qui espera les tecles és l'editor, i despertar el torn per dir-li que hi ha
      // bytes que no llegirà seria gastar el torn dues vegades (docs/30, docs/50).
      if (pal::the_line_editor_owns_the_console()) continue;
      const int waiting = LUNOK_SERIAL.available();
      if (waiting <= 0) continue;
      Event ev;
      ev.type = kEventConsoleRx;
      ev.a = 0;
      ev.b = static_cast<int32_t>(waiting);
      push(ev, 0);
    }
  }

#if 0  // **La interrupció, apagada per la 0.190.225 — no esborrada**: això és exactament el que aquest fil feia des de la 0.190.190 i fins a la
       // 0.190.224, quan la cua del driver despertava el fil en lloc del sondeig. **Per tornar-hi**: es lleva el `#if 0` d'aquí i el bloc de
       // `drain_the_events()` + `vTaskDelay` del bucle de dalt, que és el que fa la feina mentre això està apagat. **I la diferència que
       // compta**: amb la interrupció hi ha *un sol* consumidor de la cua —el fil que s'hi adorm, per cada ràfega— i amb el sondeig la cua
       // es buida sencera a cada volta, perquè la ISR no ha de cridar mai (docs/50).
  void reader_loop_by_interrupt() {
    for (;;) {
      if (pal::the_line_editor_owns_the_console()) {
#if LUNOK_HAVE_USB_CONSOLE
        vTaskDelay(pdMS_TO_TICKS(kReaderPeriodMs));
#else
        uart_event_t stale = {};
        (void)xQueueReceive(g_uart_events, &stale, pdMS_TO_TICKS(kReaderPeriodMs));
#endif
        continue;
      }
#if LUNOK_HAVE_USB_CONSOLE
      vTaskDelay(pdMS_TO_TICKS(kReaderPeriodMs));
#else
      uart_event_t event = {};
      if (xQueueReceive(g_uart_events, &event, portMAX_DELAY) != pdTRUE) continue;
#endif
      const int waiting = LUNOK_SERIAL.available();
      if (waiting <= 0) continue;
      Event ev;
      ev.type = kEventConsoleRx;
      ev.a = 0;
      ev.b = static_cast<int32_t>(waiting);
      push(ev, 0);
    }
  }
#endif  // la interrupció

  // **Buidar la cua d'esdeveniments del driver** (0.190.225): la ISR hi deixa un event per ràfega de bytes i, si ningú no la llegeix, la cua
  // s'omple i el driver crida des de la interrupció. No es mira què diu cap event: el sondeig ja pregunta si hi ha bytes, i el que la cua
  // diu només era útil quan despertava aquest fil (docs/50).
  void drain_the_events() {
#if !LUNOK_HAVE_USB_CONSOLE
    if (g_uart_events == nullptr) return;
    uart_event_t stale = {};
    while (xQueueReceive(g_uart_events, &stale, 0) == pdTRUE) {
    }
#endif
  }

  QueueHandle_t q_ = nullptr;
};

EspSystem g_system;

// The two homes a block can have, as the chip names them. INTERNAL is the board's own RAM, the one
// the system and the drivers need, and SPIRAM is the PSRAM the M5Paper carries (docs/50, docs/75).
constexpr uint32_t kExtraCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr uint32_t kHomeCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

// **Where a block goes is `pal::take_from_the_extra`** (0.151.20): the floor of the board's own RAM and the rule
// that keeps it live in `pal/pal.h` so the emulator obeys the same one and a test can hold it. What is here is only
// the reading of the chip's free bytes.
// **The relief of a heap that is full** (0.179.0): the kernel's session memories go first, and the ask is tried again
// once. A pointer and not a call into the kernel, because this layer is below it (docs/30, docs/75).
void (*g_relief)(void* keep, size_t bytes) = nullptr;
bool g_relieving = false;

// **`keep` is the block the ask is about**, when it is about one: a block that is being moved must not be given back by
// the relief and then moved (0.179.6, docs/75).
void relieve(void* keep, size_t bytes) {
  if (g_relief == nullptr || g_relieving) return;
  g_relieving = true;
  g_relief(keep, bytes);
  g_relieving = false;
}

// **Els canaris** (0.190.101, l'amo: *«millor fem les proves que volies fer amb els canaris i tota la pesca»*): every block that leaves
// this door carries a head and a tail with a magic and the size that was asked for, o sigui que una escriptura més enllà del que es va
// demanar deixa un rastre que es llegeix **al primer `release` o `resize` d'aquell bloc** —que és exactament el moment en què el `tlsf`
// del xip cau amb un `StoreProhibited` i no diu res més—. La capçalera no toca la memòria del qui la demana: el punter que torna és
// `raw + 32`, que segueix alineat a trenta-dos.
#if LUNOK_MEMORY_GUARDS
constexpr size_t kGuardHead = 32;
constexpr size_t kGuardTail = 16;
constexpr uint32_t kHeadWord = 0x474E4B4CU;   // "LKNG"
constexpr uint32_t kHeadSizeWord = 0x455A4953U;  // "SIZE"
constexpr uint32_t kTailWord = 0x44524155U;   // "UARD"
constexpr uint32_t kTailSizeWord = 0x454E4F44U;  // "DONE"

struct GuardHead {
  uint32_t magic;
  uint32_t marker;
  uint32_t bytes;
  uint32_t pad;
};

// **Quin bloc es vol veure morir** (0.190.101): zero vol dir cap. El watchpoint va damunt de la primera paraula de la cua del bloc
// d'aquesta mida, i només se n'arma un.
size_t g_watch_bytes = 0;
uint32_t g_guard_breaks = 0;

char* raw_of(void* block) { return static_cast<char*>(block) - kGuardHead; }
void* user_of(char* raw) { return raw + kGuardHead; }
// **La cua va alineada, o el xip cau per una altra cosa** (0.190.101): Xtensa no llegeix una paraula a mitges sense una excepció, i els
// blocs de Lua són de mides senars (una cadena de set caràcters), o sigui que el farciment no és un luxe: és el que fa que aquests
// canaris no canviïn el xoc d'adreça (docs/95).
size_t padded(size_t bytes) { return (bytes + 3u) & ~static_cast<size_t>(3u); }

void write_guards(char* raw, size_t bytes) {
  GuardHead* head = reinterpret_cast<GuardHead*>(raw);
  head->magic = kHeadWord;
  head->marker = kHeadSizeWord;
  head->bytes = static_cast<uint32_t>(bytes);
  head->pad = static_cast<uint32_t>(bytes) ^ kHeadWord;
  uint32_t* tail = reinterpret_cast<uint32_t*>(raw + kGuardHead + padded(bytes));
  tail[0] = kTailWord;
  tail[1] = kTailSizeWord;
  tail[2] = static_cast<uint32_t>(bytes) ^ kTailWord;
  tail[3] = ~static_cast<uint32_t>(bytes);
}

// **La cua, mirada i dita** (0.190.101): returns true when the four words are the ones that were written, and says by how much and
// where the first one that is not is, perquè «s'ha corromput» no és una resposta que serveixi per trobar qui ho ha fet.
bool the_tail_is_whole(void* block, const char* when) {
  GuardHead* head = reinterpret_cast<GuardHead*>(raw_of(block));
  if (head->magic != kHeadWord || head->marker != kHeadSizeWord) {
    ++g_guard_breaks;
    printf("\n[MEMORY] guard: a block given to %s has no head any more (what is there is 0x%08lx) — it was already given back, or "
           "the pointer was never ours\n", when, static_cast<unsigned long>(head->magic));
    return false;
  }
  const uint32_t bytes = head->bytes;
  const uint32_t* tail = reinterpret_cast<const uint32_t*>(static_cast<char*>(block) + padded(bytes));
  const uint32_t want[4] = {kTailWord, kTailSizeWord, bytes ^ kTailWord, ~bytes};
  for (int i = 0; i < 4; ++i) {
    if (tail[i] != want[i]) {
      ++g_guard_breaks;
      printf("\n[MEMORY] guard: the tail of a %lu B block at %p is broken at byte +%d (0x%08lx, wanted 0x%08lx) on %s\n",
             static_cast<unsigned long>(bytes), block, static_cast<int>(bytes + i * 4), static_cast<unsigned long>(tail[i]),
             static_cast<unsigned long>(want[i]), when);
      return false;
    }
  }
  return true;
}

// **El nom del watchpoint canvia de la IDF 4.4 a la 5** (0.190.101): la 5 en fa un `enum` (`ESP_CPU_WATCHPOINT_STORE`) i la 4.4 un
// `#define` (`ESP_WATCHPOINT_STORE`), i el nucli d'Arduino porta la 4.4. És una línia i val per als dos entorns (docs/50).
esp_err_t watch_a_store(void* where) {
#if ESP_IDF_VERSION_MAJOR >= 5
  return esp_cpu_set_watchpoint(0, where, 4, ESP_CPU_WATCHPOINT_STORE);
#else
  return esp_cpu_set_watchpoint(0, where, 4, ESP_WATCHPOINT_STORE);
#endif
}

void arm_the_watchpoint(void* block, size_t bytes) {
  if (g_watch_bytes == 0 || g_watch_bytes != bytes) return;
  g_watch_bytes = 0;  // one block, one watchpoint
  void* where = static_cast<char*>(block) + padded(bytes);
  const bool ok = watch_a_store(where) == ESP_OK;
  printf("\n[MEMORY] guard: watching the tail of the %lu B block at %p (the watchpoint is on %p, %s)\n",
         static_cast<unsigned long>(bytes), block, where, ok ? "armed" : "refused");
}
#endif

class EspExtraMemory : public IExtraMemory {
 public:
  // **Els llibres de la memòria** (0.190.0): the table is a block of the **extra memory** and not a static of the binary —the Paper
  // has no sixteen bytes of its own RAM to give—, i si la placa no en té, de la RAM que li queda; una placa que no pot donar vuit
  // kilobytes es queda sense llibres i `mem` ho diu en comptes de mentir (docs/75).
  void begin_the_books_once() {
    if (books_ != nullptr) return;
    void* table = heap_caps_malloc(pal::kMemoryBookBytes, kExtraCaps);
    if (table == nullptr) table = heap_caps_malloc(pal::kMemoryBookBytes, kHomeCaps);
    if (table == nullptr) return;
    pal::memory_books_begin(table, pal::kMemoryBookBytes);
    books_ = table;
  }

  void* books() const override { return books_; }

  void open_the_books() override { begin_the_books_once(); }

  void* allocate(size_t bytes, bool prefer_extra) override {
    void* block = take_guarded(bytes, prefer_extra);
    if (block != nullptr) pal::memory_book_taken(block, bytes);
    return block;
  }

  void* resize(void* block, size_t bytes, bool prefer_extra) override {
    void* moved = resize_guarded(block, bytes, prefer_extra);
    if (moved != nullptr) {
      pal::memory_book_moved(block, moved, bytes);
    } else if (block != nullptr && bytes == 0) {
      pal::memory_book_released(block);
    }
    return moved;
  }

  void release(void* block) override {
    if (block == nullptr) return;
    give_guarded(block);
    pal::memory_book_released(block);
  }

 private:
  // **Les tres portes amb canaris** (0.190.101): a build without `LUNOK_MEMORY_GUARDS` les tres són les de sempre, sense ni una crida
  // de més (docs/75, docs/95).
  void* take_guarded(size_t bytes, bool prefer_extra) {
#if LUNOK_MEMORY_GUARDS
    if (bytes == 0) return nullptr;
    char* raw = static_cast<char*>(allocate_raw(kGuardHead + padded(bytes) + kGuardTail, prefer_extra));
    if (raw == nullptr) return nullptr;
    write_guards(raw, bytes);
    void* block = user_of(raw);
    arm_the_watchpoint(block, bytes);
    return block;
#else
    return allocate_raw(bytes, prefer_extra);
#endif
  }

  void* resize_guarded(void* block, size_t bytes, bool prefer_extra) {
#if LUNOK_MEMORY_GUARDS
    if (block == nullptr) return take_guarded(bytes, prefer_extra);
    if (bytes == 0) {
      give_guarded(block);
      return nullptr;
    }
    // **Un bloc amb la cua trencada no es mou i no es torna** (0.190.101): moving it would die inside the `tlsf` with no explanation,
    // and giving it back would poison the heap; es deixa anar (el build de diagnòstic es pot permetre el forat) i el registre diu
    // quina mida i on, que és el que es venia a saber (docs/95).
    if (!the_tail_is_whole(block, "a resize")) return nullptr;
    const uint32_t was = reinterpret_cast<GuardHead*>(raw_of(block))->bytes;
    void* moved = take_guarded(bytes, prefer_extra);
    if (moved == nullptr) return nullptr;  // the old block is untouched and still the caller's
    memcpy(moved, block, (was < bytes) ? was : bytes);
    give_guarded(block);
    return moved;
#else
    return resize_raw(block, bytes, prefer_extra);
#endif
  }

  void give_guarded(void* block) {
#if LUNOK_MEMORY_GUARDS
    if (!the_tail_is_whole(block, "a release")) return;  // leaked on purpose: the log has the size (docs/95)
    GuardHead* head = reinterpret_cast<GuardHead*>(raw_of(block));
    head->magic = 0;
    release_raw(head);
#else
    release_raw(block);
#endif
  }

  void* allocate_raw(size_t bytes, bool prefer_extra) {
    if (bytes == 0) return nullptr;
    // A block that asked for the extra memory goes there first and the board's RAM is the fallback; a
    // small one goes home only while the home keeps its floor, and to the PSRAM when it would lose it.
    if (take_from_the_extra(room_at_home(), bytes, prefer_extra)) {
      if (void* block = heap_caps_malloc(bytes, kExtraCaps)) return block;
    }
    if (void* block = heap_caps_malloc(bytes, kHomeCaps)) return block;
    // A board with no PSRAM has nowhere else to look and this is the answer it gives (docs/75)...
    if (void* block = heap_caps_malloc(bytes, kExtraCaps)) return block;
    // ...**unless the session memories of the apps are what is in the way** (0.179.0): they go, once, and the two
    // homes are asked again. It is a *failed* ask and not a heap that looks full: a block that fits is never paid for with
    // somebody's cache (0.179.6, docs/75).
    relieve(nullptr, bytes);
    if (take_from_the_extra(room_at_home(), bytes, prefer_extra)) {
      if (void* block = heap_caps_malloc(bytes, kExtraCaps)) return block;
    }
    if (void* block = heap_caps_malloc(bytes, kHomeCaps)) return block;
    return heap_caps_malloc(bytes, kExtraCaps);
  }

  void* resize_raw(void* block, size_t bytes, bool prefer_extra) {
    if (block == nullptr) return allocate_raw(bytes, prefer_extra);
    if (bytes == 0) {
      release_raw(block);
      return nullptr;
    }
    const bool to_extra = take_from_the_extra(room_at_home(), bytes, prefer_extra);
    const uint32_t home = to_extra ? kExtraCaps : kHomeCaps;
    if (void* moved = heap_caps_realloc(block, bytes, home)) return moved;
    // The same block in the other home: `heap_caps_realloc` allocates, copies and frees, which is
    // exactly what moving a block to the other memory means. On failure the block is still the one
    // the caller gave and it is not touched (docs/75).
    if (void* moved = heap_caps_realloc(block, bytes, to_extra ? kHomeCaps : kExtraCaps)) return moved;
    // **And the session memories go before the refusal here too** (0.179.0), with the block untouched and the same
    // two homes tried again. **The block that is being moved is not given back**: it is the one the ask is about, and a
    // `realloc` of what the relief has just freed is a block used after it was given away (0.179.6, docs/75).
    relieve(block, bytes);
    if (void* moved = heap_caps_realloc(block, bytes, home)) return moved;
    return heap_caps_realloc(block, bytes, to_extra ? kHomeCaps : kExtraCaps);
  }

  void release_raw(void* block) { heap_caps_free(block); }

 private:
  // **The free bytes of the board's RAM are read every so many asks and not at every one**: the walk
  // of the heap for each of the thousands of blocks a Lua machine takes would cost more than the block
  // itself, and a floor that is read two hundred and fifty-six blocks late is still a floor.
  size_t room_at_home() {
    if (++asked_ == 0u) home_free_ = heap_caps_get_free_size(kHomeCaps);
    return home_free_;
  }

  // **Un comptador que només mira el byte de sota, i prou** (0.190.0): it reads the home heap once every 256 asks, so a byte is all
  // it needs —and those four bytes are the four of the pointer below, or sigui que els llibres no fan créixer l'objecte d'aquesta
  // plataforma (docs/75).
  uint8_t asked_ = 0;
  // Nothing has been read yet: the first blocks go to the extra memory, which is the safe answer when
  // the board has one and the only answer when it does not.
  size_t home_free_ = 0;
  // **La taula dels llibres, que és d'aquesta plataforma** (0.190.0): given once, on the first block, and it lives as long as the
  // board is up (docs/75).
  void* books_ = nullptr;
};

EspExtraMemory g_extra;

}  // namespace

namespace {
// **Les línies de la IDF passen a ser d'aquesta casa** (0.190.247, l'amo: *«Les repintades de l'epdiy es veuen al canal sèrie!! no havíem
// quedat que tot anava al log circular del kernel?»*). The components of the chip and of the libraries —`epdiy` between them— write with
// `ESP_LOGx`, and that goes by `vprintf` straight to the descriptor of the console: neither the ring of the kernel nor the rule that says the
// wire only carries errors applied to them, o sigui que una repintada del panell sortia pel port al mig del que una persona escrivia. With this
// hook every one of those lines comes through `hal::say_a_line`, which is the same door as ours: el registre circular, el mirall de la consola
// i la cua del filferro amb la seva ploma (docs/30, docs/50, docs/85).
int the_line_of_the_idf(const char* fmt, va_list args) {
  char line[192];
  const int written = vsnprintf(line, sizeof(line), fmt, args);
  if (written <= 0) return written;
  size_t len = static_cast<size_t>(written);
  if (len >= sizeof(line)) len = sizeof(line) - 1;
  // The IDF puts the newline at the end and a line of this house carries its own: es treu i es torna a posar, perquè una línia del registre
  // acabi en un salt de línia i en un de sol.
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) --len;
  if (len == 0) return written;
  line[len++] = '\n';
  hal::say_a_line(line, len);
  return written;
}
}  // namespace

bool init() {
  // **I el ganxo, abans de la primera línia de ningú** (0.190.247).
  esp_log_set_vprintf(&the_line_of_the_idf);
  // **The noise of this boot, taken before anything else of the board exists**: the window of `Random::seed` leaves the
  // entropy source off, and the documents of the chip are clear that it has to be off before the ADC is initialised
  // —the battery gauge of the Paper is an ADC pin— and before the radio (docs/50). Doing it here and not at the first
  // `random()` is what makes that a fact of the boot instead of a fact of whoever asks first (docs/20).
  g_random.seed();
  if (!lunok_serial_begin()) return false;
  // Right after a reset the line carries junk for a few milliseconds. The line
  // says when it has gone quiet; the twenty milliseconds are the ceiling of a
  // bridge that never stops talking (docs/50).
  (void)g_system.wait_quiet(12, 20);
  return g_system.bring_up();
}

void shutdown() {}

// **Una placa no té finestra que mirar** (0.190.3): the page of an error waits on the panel itself —the finger on the glass
// and the dial of a board with no glass— and the turn of the kernel is not what keeps it alive, so the hook exists here and
// does nothing. It is defined and not only declared because the page calls it on every board (docs/30, docs/50).
void set_pump(bool (*fn)(void*), void* ctx) {
  (void)fn;
  (void)ctx;
}

bool pump() { return false; }

// **La porta de la interrupció, i tota a la IRAM** (0.190.206): `g_system` és un global —o sigui que no hi ha crida a `system()`— i el
// que es crida a sota és un mètode no virtual. El que hi ha entre aquesta funció i `xQueueSendFromISR` no toca la flash.
IRAM_ATTR void wake_the_kernel_from_isr() { g_system.wake_an_isr_here(); }

ISystem& system() { return g_system; }

IExtraMemory& extra_memory() { return g_extra; }

// **Les eines de la memòria, dites al prompt** (0.190.101): el build de diagnòstic les té, i un build de vida contesta que no i no fa
// res més (docs/95).
void debug_watch_a_block_of(size_t bytes) {
#if LUNOK_MEMORY_GUARDS
  g_watch_bytes = bytes;
  printf("\n[MEMORY] guard: the next block of %lu B will be watched\n", static_cast<unsigned long>(bytes));
#else
  (void)bytes;
#endif
}

void debug_watch_off() {
#if LUNOK_MEMORY_GUARDS
  g_watch_bytes = 0;
#if LUNOK_MEMORY_GUARDS
  (void)watch_a_store(nullptr);
#endif
#endif
}

uint32_t debug_guards_on() {
#if LUNOK_MEMORY_GUARDS
  return g_guard_breaks + 1;  // one is «on and nothing broken yet», which is not zero (docs/95)
#else
  return 0;
#endif
}

bool debug_heap_check() {
  // **Els dos munts, un darrere l'altre** (0.190.101): the walk says which heap and where; the return value is what it found.
  const bool home = heap_caps_check_integrity(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, true);
  const bool extra = heap_caps_check_integrity(MALLOC_CAP_SPIRAM, true);
  printf("\n[MEMORY] heap: home %s, extra %s\n", home ? "whole" : "BROKEN", extra ? "whole" : "BROKEN");
  return home && extra;
}

void set_dispatch_us(uint32_t us) {
  // **The tick of the chip dispatches for itself**: a board is not told how its own FreeRTOS wakes
  // (docs/85).
  (void)us;
}

void set_wait_slice_ms(uint32_t ms) {
  // **The tick of the chip wakes the system by itself**: a slice is an emulator's way of looking at a window, and
  // a board has no window (docs/85).
  (void)ms;
}

void set_heap_timing(uint32_t step_ns, uint32_t bytes_per_us) {
  // **The chip's allocator is the chip's**: a board is not told what its own malloc costs (docs/85).
  (void)step_ns;
  (void)bytes_per_us;
}

MemoryLeft memory_left(bool extra) {
  // **The chip answers for itself**: the board's own RAM through `MALLOC_CAP_INTERNAL` and the PSRAM
  // through `MALLOC_CAP_SPIRAM`, which is what `mem` prints and what `sys.heap()` already reads. A board
  // with no PSRAM has no extra home and says so (docs/75).
  MemoryLeft out = {};
  if (extra && heap_caps_get_total_size(kExtraCaps) == 0) return out;
  const uint32_t caps = extra ? kExtraCaps : kHomeCaps;
  out.counted = true;
  out.free_bytes = heap_caps_get_free_size(caps);
  out.lowest_free_bytes = heap_caps_get_minimum_free_size(caps);
  return out;
}

void set_memory_bytes(uint32_t home_bytes, uint32_t extra_bytes) {
  // **A board is not told how much memory it has**: the chip answers for itself, and a ceiling here
  // would be a lie told to the firmware that runs on the glass (docs/85).
  (void)home_bytes;
  (void)extra_bytes;
}

void set_random_seed(uint32_t seed) {
  // **A board is not told its noise**: a firmware whose sequences can be made to repeat is a firmware whose keys can be
  // guessed, and the chip has a generator that nobody has to configure (docs/20, docs/85).
  (void)seed;
}

void set_memory_relief(void (*relief)(void* keep, size_t bytes)) { g_relief = relief; }

void delay_us(uint32_t us) {
  // **The chip's own busy wait**: on the board nothing asks for this —the time is the real one— and the
  // honest answer to "wait" is to wait (docs/85).
  if (us > 0) esp_rom_delay_us(us);
}

void delay_ms(uint32_t ms) {
  // **I aquesta espera sí que deixa córrer el que toca** (0.190.137): és el que fa que una comanda llarga no es mengi el watchdog de tasca
  // (docs/50, docs/95).
  //
  // **I compta mil·lisegons de debò des de la 0.190.244**: la versió anterior comptava deu mil·lisegons per tick (`(ms + 9) / 10`), que era
  // cert quan el tick era de deu; amb el tick d'un mil·lisegon que aquesta IDF té (`CONFIG_FREERTOS_HZ=1000`), una espera de mil
  // mil·lisegons en feia cent. No hi havia cap crida que ho patís —la primera és la tasca de la shell—, o sigui que s'arregla amb la primera
  // que en depèn i no abans (docs/95). Zero continua essent **un** tick i no cap.
  lunok_delay_ms(ms);
}

// **El pany de debò** (0.190.244): the queues that cross a task need one, and here it is a recursive mutex, perquè la shell pot prendre el pany
// i cridar una cosa que el torna a prendre sense penjar-se. Un pany que no s'ha pogut fer es contesta nul i la cua queda sense pany, que en una
// placa amb memòria lliure no passa mai (docs/50).
pal::Lock make_lock() {
  static_assert(sizeof(SemaphoreHandle_t) <= sizeof(pal::Lock), "un pany no cap en un punter");
  return static_cast<pal::Lock>(xSemaphoreCreateRecursiveMutex());
}

void take_lock(pal::Lock lock) {
  if (lock != nullptr) xSemaphoreTakeRecursive(static_cast<SemaphoreHandle_t>(lock), portMAX_DELAY);
}

void give_lock(pal::Lock lock) {
  if (lock != nullptr) xSemaphoreGiveRecursive(static_cast<SemaphoreHandle_t>(lock));
}

// **Aquesta banda sí que té tasques** (0.190.244): the shell runs in its own task here, because the glass takes turns away from everything
// else and a prompt that only reads between paints is not a terminal (docs/30, docs/50).
bool tasks_are_here() { return true; }

namespace {
// **El que la tasca ha de saber quan arrenca** (0.190.244): FreeRTOS passes one pointer, so the entry and its argument travel together and
// the trampoline frees the parcel before it lets go, o sigui que la tasca que no torna mai no deixa res a deure.
struct Start {
  void (*entry)(void*);
  void* argument;
};

void run_a_start(void* parcel) {
  Start start = *static_cast<Start*>(parcel);
  vPortFree(parcel);
  start.entry(start.argument);
  vTaskDelete(nullptr);
}
}  // namespace

bool start_a_task(const char* name, uint32_t stack_bytes, uint8_t priority, void (*entry)(void*), void* argument) {
  if (entry == nullptr) return false;
  // **I una pila sense número no en té** (0.190.244): a FreeRTOS task with zero bytes is a task with the idle stack, o sigui que es demana el
  // mínim honest i no es deixa la decisió a un zero (docs/50).
  const uint32_t bytes = (stack_bytes == 0) ? 4096u : stack_bytes;
  Start* parcel = static_cast<Start*>(pvPortMalloc(sizeof(Start)));
  if (parcel == nullptr) return false;
  parcel->entry = entry;
  parcel->argument = argument;
  // **I la pila va en bytes i no en paraules** (0.190.244): aquesta IDF compta la profunditat de la pila en bytes (`StackType_t` és un
  // `uint8_t`), al revés que el FreeRTOS de sempre, o sigui que el número que demana el nucli és el que arriba tal qual (docs/50).
  static_assert(sizeof(StackType_t) == 1, "aquesta IDF compta la pila en bytes");
  const BaseType_t made = xTaskCreate(&run_a_start, (name == nullptr) ? "lunok" : name, bytes, parcel, priority, nullptr);
  if (made != pdPASS) {
    vPortFree(parcel);
    return false;
  }
  return true;
}

}  // namespace pal
