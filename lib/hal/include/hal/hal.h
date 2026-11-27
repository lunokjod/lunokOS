// Hardware abstraction layer.
//
// Interfaces only: this library has no sources worth speaking of. Each board is
// its own library (hal_atom, hal_emulator) and implements what it really has.
// A board that lacks a device returns nullptr and leaves the capability off.
#ifndef LUNOK_HAL_HAL_H
#define LUNOK_HAL_HAL_H

#include <stddef.h>
#include <stdint.h>

// **What a board has is said twice, and each way answers a different question** (0.179.9): `kCapDisplay` below is the
// answer of a *running* system —a board that has the panel and has booted it—, and `LUNOK_GLASS` of its `board/identity.h`
// is the answer **the compiler** gets, which is what decides whether the page, the reader and the faces are built at all
// (`#if LUNOK_GLASS` in the kernel, in the bridge and in the shell). The header is included here so that no file has to
// remember it, and a board that forgets to say which of the two it is does not build instead of building a system with no
// page and no word about it (docs/50, docs/75).
#include "board/identity.h"
#if !defined(LUNOK_GLASS)
#error "board/identity.h has to say LUNOK_GLASS (0 or 1): whether this board carries a glass"
#endif

// **La ràdio de LoRa, que és una interfície i prou** (0.192.49): viu en un capçol seu perquè té el seu vocabulari —una
// configuració de recepció, un paquet amb el que el xip ha mesurat— i perquè el `IPlatform` l'ha de poder nomenar.
#include "hal/lora.h"
// **I el GNSS, que també té el seu vocabulari** (0.192.63): una frase del receptor, un satèl·lit, la data del cel; i el mateix
// motiu que el LoRa, que `IPlatform` l'hagi de poder nomenar (docs/30, docs/50).
#include "hal/gps.h"

namespace hal {

enum Capability : uint32_t {
  kCapConsole = 1u << 0,
  // **El bit 1 va ser `ansi` i no es torna a fer servir** (0.190.187): the system stopped speaking escape codes, i una
  // capacitat que diu que el terminal en parla convida a escriure'ls. El número queda lliure i els altres no es mouen (docs/30, docs/50).
  kCapDisplay = 1u << 2,
  kCapInput = 1u << 3,
  kCapLed = 1u << 4,
  kCapLittlefs = 1u << 5,
  kCapSd = 1u << 6,
  kCapNvs = 1u << 7,
  kCapRtc = 1u << 8,
  kCapRtcWake = 1u << 9,
  kCapWifi = 1u << 10,
  kCapHttp = 1u << 11,
  kCapBattery = 1u << 12,
  kCapSleepLight = 1u << 13,
  kCapSleepDeep = 1u << 14,
  kCapProvisioning = 1u << 15,
  kCapImu = 1u << 16,
  // The air around the board: temperature and humidity, which is the SHT30 of the Paper and nothing
  // at all on the ATOMs (docs/50).
  kCapEnv = 1u << 17,
  // The glass: somebody can touch the board, and the kernel turns the fingers into taps, long
  // presses and drags (docs/30, docs/50).
  kCapTouch = 1u << 18,
  // **Aquest vidre es renta sencer a cada arrencada** (0.192.10, l'amo: *«Que aquest model SEMPRE al boot netegi la pantalla amb
  // splash hide (que neteja al màxim)»*): a panel that keeps a ghost of whatever it was showing —the T5S3's ED047TC1 does, and the
  // picture of a sleep left on it is ink nobody wrote— gets `splash::hide()` at the boot, a `gc16` of the whole glass, **whatever
  // the reason of the boot is**: a power-on, a restart, a return from a sleep or an alarm with nobody in front. It is the board
  // that says it because it is the panel's behaviour and not a preference of a run (docs/50, docs/60).
  kCapWashAtBoot = 1u << 19,
  // **La ràdio de LoRa** (0.192.49): a board with a SX1262 soldered to it and a driver that can put it to listen. És una
  // capacitat i no una suposició perquè el mòdul és opcional a la placa i perquè una app que ensenya un espectre ha de
  // poder dir «aquesta placa no escolta» en comptes de dibuixar una línia plana que sembla un aire buit (docs/30, docs/50).
  kCapLora = 1u << 20,
  // **El receptor GNSS** (0.192.63): una placa amb un mòdul de satèl·lits connectat a un cable sèrie i un driver que en sap treure
  // la posició. És una capacitat i no una suposició pel mateix motiu que la ràdio —el mòdul és opcional i la mateixa placa en porta
  // dos de possibles—, o sigui que una pàgina que ensenya el cel ha de poder dir «aquesta placa no mira el cel» en comptes de
  // dibuixar un mapa buit que sembla una antena tancada (docs/30, docs/50).
  kCapGps = 1u << 21
};

struct Caps {
  uint32_t bits = 0;

  bool has(Capability c) const { return (bits & static_cast<uint32_t>(c)) != 0; }
  void add(Capability c) { bits |= static_cast<uint32_t>(c); }
  // A board starts from `esp32_caps()` and takes away here the one its hardware really
  // lacks, with the reason written next to it (docs/50).
  void remove(Capability c) { bits &= ~static_cast<uint32_t>(c); }
};

// Capability table, indexed by position. Used by the shell and by sys.caps().
uint32_t cap_count();
const char* cap_name(uint32_t index);
uint32_t cap_bit(uint32_t index);

// Why the board came back. `kPowerOn` is a plain boot, `kTimer` is the chip's own sleep
// timer, `kConsole` a byte on the serial port, `kOther` anything else, and `kClock` is the
// **clock chip's alarm**: a board that was powered off and that a quartz brought back, which
// is not the same thing as a timer and is the strongest evidence of a good wake (docs/60).
// **Què ha despertat la placa, i el dit és una cosa a part del botó** (0.190.19): a deep sleep of this system has three doors —the hour, a
// key and, if a person asked for it, the glass— and the board knows which one it was: the chip's `ext0` is the key of the dial and its
// `ext1` is the line of the touch controller. Un sol `kOther` per a tots dos era una línia de registre que deia «un botó» quan havia estat
// un dit (docs/50, docs/60).
enum class WakeCause : uint8_t { kPowerOn = 0, kTimer = 1, kConsole = 2, kOther = 3, kClock = 4, kTouch = 5 };

enum class OpenMode : uint8_t { kRead = 0, kWrite = 1, kAppend = 2, kReadWrite = 3 };

enum class NodeType : uint8_t { kNone = 0, kFile = 1, kDir = 2 };

struct Stat {
  NodeType type = NodeType::kNone;
  uint32_t size = 0;
  int64_t mtime = 0;  // epoch seconds, 0 when the backend does not know
};

struct ListEntry {
  const char* name = nullptr;  // owned by the backend during the call only
  NodeType type = NodeType::kNone;
  uint32_t size = 0;
  int64_t mtime = 0;
};

class ListSink {
 public:
  virtual ~ListSink() {}
  // Return false to stop the listing early.
  virtual bool on_entry(const ListEntry& entry) = 0;
};

// Space the VFS pool lends to a backend for one open file. Fixed size on
// purpose: opening a file must never allocate.
struct FileSlot {
  uint32_t size = 0;
  void* impl = nullptr;
};

class IClock {
 public:
  virtual ~IClock() {}
  virtual int64_t epoch_ms() = 0;
  virtual bool set_epoch_ms(int64_t epoch_ms) = 0;
  virtual int64_t monotonic_ms() = 0;
  // Only one wake is ever armed: the nearest deadline. See docs/60.
  virtual bool arm_wake(int64_t at_epoch_ms) = 0;
  virtual void disarm_wake() = 0;
  virtual WakeCause last_wake() = 0;
  virtual bool can_wake() const = 0;
  // **Qui s'ha quedat el despertar** (0.189.149): on the last `arm_wake` went —`"chip"` is the battery-backed clock of the
  // board (the Paper's BM8563, whose alarm is wired to the power circuit and whose quartz counts the wait), `"timer"` is
  // the SoC's own RTC timer, and `"none"` is nothing armed—. A board with both keeps the deadline in one of them and never
  // in the two (docs/50, docs/60), o sigui que «una alarma armada» sense dir on és una línia que no explica res, que és el
  // que l'amo va demanar de veure al registre.
  virtual const char* wake_keeper() const { return "none"; }
  // Takes the hour back from wherever this board keeps it properly. A board whose hour is a
  // chip with a battery behind it answers by reading it; one whose hour is the chip's own
  // counter has nothing to take and does nothing. The system asks after every sleep that came
  // back, because a sleep is exactly when its own counter drifts (docs/60).
  virtual void resync() {}
  // Where the epoch comes from: "rtc" when a battery-backed chip keeps it,
  // "soc" when it is the SoC's own counter, "none" when nobody has set it yet
  // and the epoch is 0. docs/60 is the rule; time.caps() repeats it to apps.
  virtual const char* source() const = 0;
  // **Quin rellotge és, i si el pot portar** (0.190.153, l'amo: *«Afegeix l'informació a l'app Battery»*). `chip_name()` és el
  // model del xip amb pila que aquesta placa té a la vora —**el nom el dona la placa i no el driver**, perquè el mateix codi
  // parla amb un PCF8563 i amb un BM8563 i són xips diferents amb els mateixos registres— i és buit on no n'hi ha cap.
  // `chip_state()` és el que aquell xip diu de si mateix: `"none"` que la placa no en té, `"silent"` que no contesta o que el
  // que contesta no és una hora, `"lost"` que la seva pila el va deixar (el bit VL) i `"ok"` que la guarda de debò. Una app
  // que viu d'una alarma vol saber-ho: una mostra que depèn d'un rellotge sense pila només arriba mentre la placa és encesa
  // (docs/30, docs/50, docs/60).
  virtual const char* chip_name() const { return ""; }
  virtual const char* chip_state() const { return "none"; }
};

class IStorage {
 public:
  virtual ~IStorage() {}
  virtual bool stat(const char* rel, Stat& out) = 0;
  virtual bool list(const char* rel, ListSink& sink) = 0;
  virtual bool open(const char* rel, OpenMode mode, FileSlot& slot) = 0;
  virtual int32_t read(FileSlot& slot, void* buf, uint32_t len) = 0;
  virtual int32_t write(FileSlot& slot, const void* buf, uint32_t len) = 0;
  virtual bool seek(FileSlot& slot, uint32_t pos) = 0;
  virtual bool close(FileSlot& slot) = 0;
  virtual bool remove(const char* rel) = 0;
  virtual bool mkdir(const char* rel) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  virtual bool space(uint64_t& total, uint64_t& free) = 0;
  // **El que el volum tingui a mig fer, acabat** (0.189.18, l'amo: *«Haurà donat temps a gravar-ho tot al SD?? s'ha apagat
  // tot molt ràpid. Fes flush a tots els dispositius que es puguin abans d'iniciar el procés d'apagada»*): every file of
  // this system is opened, written and closed inside one call —`write_block` is the only writer of a session— so the
  // medium has the bytes by the time `close` answers, and what this door adds is the half a backend may still be keeping
  // in its own buffers. **A backend that has nothing pending answers true and says so in its own comment** instead of
  // pretending it did something; what the caller does with it is log the answer and then let the board rest (docs/70,
  // docs/75).
  virtual bool flush() { return true; }
  // **Empty the volume** (0.171.0, docs/70, docs/90): every file and every folder of its root goes and the volume
  // stays mounted with the format it has — the card is FAT before and after, and what a person gets is an empty card—.
  // It is what the page of the eraser means by formatting, and `removed` says how many files went so the page can
  // report it. **A backend that cannot do it says so instead of pretending**: the default answers false and the kernel
  // turns that into `E_UNSUPPORTED`, which is the difference between "there is nothing to erase" and "not here"
  // (docs/30).
  virtual bool format(uint32_t& removed) {
    removed = 0;
    return false;
  }
};

class IKeyValue {
 public:
  virtual ~IKeyValue() {}
  // `cap` is how much room the caller has and `len` comes back with what was
  // read: two numbers and not one, because the one-number version asked the
  // caller to put the capacity in `len` before the call, and a caller that wrote
  // `size_t len = 0;` read nothing and was never told (measured on the board,
  // 31/10: the network was saved and `network:` kept saying none).
  virtual bool get(const char* ns, const char* key, void* buf, size_t cap, size_t& len) = 0;
  virtual bool set(const char* ns, const char* key, const void* buf, size_t len) = 0;
  virtual bool erase(const char* ns, const char* key) = 0;
  // Iteration: pass first = true for the first key of the namespace.
  virtual bool next(const char* ns, const char*& key, bool& first) = 0;
  // The namespaces themselves, for the /nvs mount to list them as directories
  // (docs/70). Same convention: first = true starts again.
  virtual bool next_namespace(const char*& ns, bool& first) = 0;
  // **El que el magatzem tingui a mig fer, acabat** (0.189.18): the board's `set` commits its namespace when it closes
  // it, and a backend that keeps its write in a buffer of its own says here that it has written it. What the caller does
  // with the answer is say it in the log before the current is cut (docs/75).
  virtual bool commit() { return true; }
  // How full the store is, in **entries**: the NVS is not bytes, it holds a fixed number
  // of entries and a store with none left refuses the next save. `used` of `total` is the
  // number that says whether the next alarm fits; `total` is 0 when the backend has no
  // ceiling to report, which is the emulator's case (docs/70).
  virtual bool stats(uint32_t& used, uint32_t& total) = 0;
};

// How the network is, as the kernel keeps asking. A board never blocks on the
// radio: it starts and then says in which of these it is (docs/30, docs/50).
enum class NetState : uint8_t { kOff = 0, kConnecting = 1, kUp = 2, kFailed = 3 };

// Provisioning: the board opens its own access point, somebody hands it the
// network (the Espressif app, over protocomm) and then it is done. It is the
// board that keeps the credentials, in its own store, so that `net.provision`
// is only about the act of handing them over (docs/50).
enum class ProvState : uint8_t { kOff = 0, kRunning = 1, kDone = 2, kFailed = 3 };

// How a person hands the network to the board. Bluetooth is the way the maker of
// the chip recommends and it does not take the phone off its own network; the
// access point is kept because it needs no Bluetooth at all, and a board that
// cannot open one is a board that cannot be provisioned (docs/50).
enum class ProvTransport : uint8_t { kSoftAp = 0, kBle = 1 };

// What a download is allowed to cost. There is no header list: nothing has asked
// for one and a list of headers costs memory that docs/75 has not given away.
struct HttpOpts {
  uint32_t timeout_ms = 10000;
  // How many bytes of the page the caller wants in memory. **Zero is a page that is only wanted on a
  // volume**, and then `sink` has to be there (docs/30).
  uint32_t max_bytes = 16384;
  // **A page too big for memory goes to a file instead of being refused**: with a sink and a path
  // inside that unit, the body that grows past `max_bytes` is written there —the first `max_bytes`
  // included— and the whole page can be worked with once it is down. A page that fits never touches
  // the volume. `nullptr` is a page that only lives in memory (docs/30, docs/75).
  IStorage* sink = nullptr;
  const char* sink_path = nullptr;
  // **El mètode, que fins ara era sempre GET** (0.199.3): a POST **sense cos** i prou, que és el que són totes les ordres
  // d'un servidor de mitjans i el que un comandament necessita; una ordre amb cos no l'ha demanada ningú i no hi és
  // (docs/30, docs/50).
  bool post = false;
};

// Why a page did not arrive, said as a cause and not as a word: the code an app
// reads (E_HTTP, E_TLS, E_TOOBIG) is the kernel's to choose (docs/30).
enum class HttpError : uint8_t {
  kNone = 0,
  kConnect,   // the server was not reachable, or it stopped talking
  kTls,       // the certificate was not accepted, or the handshake failed
  kTooBig,    // more bytes than the caller allowed
  kNoMemory,  // no room for the page in what is left of the heap
  kStorage,   // the volume refused the page: no room, or a write that did not go in
  kStopped    // nobody wants the answer any more (docs/75)
};

struct HttpResult {
  int status = 0;        // the HTTP status, 0 when there was no answer
  char* body = nullptr;  // owned by the board until http_release
  // In memory, or **in the file** when `saved`: the size the page has, which is what the caller wants
  // to hear whether it went to the heap or to a volume (docs/30).
  uint32_t body_len = 0;
  // True when the page is in the sink and not here: `body` is null and `body_len` is the size of the
  // file that was written, the whole page from its first byte (docs/30).
  bool saved = false;
  HttpError error = HttpError::kNone;
};

class INet {
 public:
  virtual ~INet() {}
  // Starts connecting and returns at once. There is no timeout here on purpose:
  // the deadline belongs to the kernel, which is the one that must keep turning
  // while the radio works (docs/30).
  virtual bool wifi_start(const char* ssid, const char* pass) = 0;
  // Connects with the network the board already has stored. This is what makes
  // provisioning worth anything after a reboot (docs/50).
  virtual bool wifi_start_stored() = 0;
  // Puts a network in the board's own store, so that it is there after a reboot
  // and so that `net.connect_stored` can use it. It does not connect.
  virtual bool wifi_store(const char* ssid, const char* pass) = 0;
  // Which network the board has stored, so an app can say it out loud.
  virtual bool wifi_stored_ssid(char* out, size_t cap) = 0;
  virtual NetState wifi_state() = 0;
  virtual bool wifi_disconnect() = 0;

  // **La ràdio del wifi, encesa i apagada de debò** (0.190.221, l'amo: *«de moment aquests botons son per apagar i encendre radios»*): fins
  // ara aquesta banda només sabia **connectar-se** —`wifi_start` encén la ràdio i s'hi lliga—, i una persona que volia la ràdio encesa sense
  // xarxa no tenia cap porta. Encendre vol dir el driver enlaire i l'estació començada; apagar vol dir el driver aturat, i la propera
  // encesa el torna a començar sense tornar-lo a instal·lar (docs/50).
  //
  // **I tenen definició per defecte** com la resta del HAL: una banda que no en sàpiga res contesta que no, i el commutador de la targeta
  // no es pinta perquè `net::available()` ja ho diu abans (docs/45).
  virtual bool wifi_radio_on() { return false; }
  virtual bool wifi_radio_off() { return false; }
  virtual bool wifi_radio_is_on() const { return false; }

  // Scanning the networks around, without waiting for the radio: start it, ask
  // whether it is done in every turn, and then how many it found (docs/50).
  virtual bool wifi_scan_start() = 0;
  // **Why the last thing asked of the radio was refused**, in the driver's own words when it has them
  // (0.118.10). `E_BUSY` is the right answer for a radio that is working on something else, and the
  // wrong one for a radio that never came up —no memory for it, a driver that would not start, a scan
  // the chip turned down—: they are different faults with different fixes and the board answered the
  // same four letters to all of them (the ATOM, 8/11) (docs/30, docs/50).
  virtual const char* last_error() const { return ""; }
  // **Per què va caure l'última associació**, en el número del driver (0.197.0): `15` és una contrasenya que no és i `201` un punt d'accés
  // que no hi és, i la diferència és la que decideix si val la pena tornar-ho a provar —tres cops amb una contrasenya dolenta són tres
  // cops la mateixa resposta— (docs/30, docs/50). Zero vol dir que la ràdio no en sap res.
  virtual int32_t wifi_last_reason() const { return 0; }
  virtual bool wifi_scan_done() = 0;
  virtual uint32_t wifi_scan_count() = 0;
  // One entry of the last finished scan: its name and its signal. True when the
  // index is there.
  virtual bool wifi_scan_record(uint32_t index, char* ssid, size_t cap, int32_t& rssi) = 0;

  // Provisioning, again without waiting: start the access point and the
  // protocomm service, and ask how it is going in every turn. `pop` may be
  // nullptr when the board has no way to show one (docs/50).
  // `service_name` comes in empty and the board fills it: the name a phone has to
  // look for is the one the radio announces, and only the board knows its MAC. The
  // caller prints the QR with what comes back (docs/50).
  virtual bool prov_start(const char* pop, char* service_name, size_t cap,
                          ProvTransport transport) = 0;
  // Time from a server on the network: start and return, and the kernel asks
  // `ntp_synced` in every turn. `epoch_ms` comes back with the time the server
  // gave, and the kernel puts it in the clock, so both bands set the clock the
  // same way (docs/60).
  virtual bool ntp_start(const char* server) = 0;
  virtual bool ntp_synced(int64_t& epoch_ms) = 0;

  virtual ProvState prov_state() = 0;
  virtual bool prov_stop() = 0;
  // True when the board already knows a network: provisioned once, or given one
  // by hand. There is nothing to provision in that case.
  virtual bool prov_provisioned() = 0;
  // Forgets the stored network, so the next boot is a board with no network at
  // all (docs/50).
  virtual bool prov_forget() = 0;
  // A page, downloaded in a task of the board's own so that nothing here blocks:
  // the kernel starts it and asks `http_poll` in every turn (docs/30). The answer
  // is 0 when there is nothing to start, and then nothing will be answered.
  virtual uint32_t http_get_start(const char* url, const HttpOpts& opts) = 0;
  // False when the operation is not one of this board's. `done` says whether the
  // answer is ready; `out` is only read when it is.
  virtual bool http_poll(uint32_t op, bool& done, HttpResult& out) = 0;
  // Frees what the request left behind, and it must be called once whatever the
  // outcome was: a caller that forgets it leaves a page in the heap. On a request
  // that is still running it means "nobody wants the answer", and the task drops
  // it (docs/75).
  virtual void http_release(uint32_t op) = 0;
};

// **El Bluetooth, que en aquest sistema és un teclat** (0.190.179, l'amo: *«afegeix bluetooth al projecte, anem a fer un experiment»*).
// The radio of an ESP32 can be a keyboard for a phone or a computer, and that is the one use this system has for it: it is not a
// network —there is nothing to connect to— and it is not a device of its own —it has no page, no session and nothing to store—, o
// sigui que el que hi ha és una porta: **escrivre caràcters a un altre aparell**. Les lletres hi són per Usage ID del HID (`0x04` és
// la `a`, `0x28` el retorn), que és el que entén el teclat de qualsevol sistema (docs/30, docs/50).
//
// **I el ràdio mana sobre la son**: un son lleuger atura els rellotges que el link viu, o sigui que mentre això és engegat el xip no fa
// becaines (`wants_the_chip_awake`). És el preu de ser un teclat i es diu, en lloc de deixar una connexió que cau sola (docs/50, docs/60).
// **Una xarxa de Bluetooth Mesh** (0.190.212, l'amo: *«pots posar algun mesh?»*): the board as a **node** of a mesh, o sigui que un
// provisionador —el telèfon amb l'app de nRF Mesh o la d'Espressif— el descobreix, li dóna una adreça i el deixa dins la xarxa. **No és el
// teclat**: el mesh fa servir l'anunci i la connexió per als seus portadors propis, o sigui que és una altra manera d'encendre la mateixa
// ràdio i no una cosa que es posa a sobre del HID (docs/50).
class IMesh {
 public:
  virtual ~IMesh() {}
  // Whether this build carries the stack at all: a board whose build has no `CONFIG_BLE_MESH` says no and the prompt says so.
  virtual bool can_mesh() const { return false; }
  // The stack up and the node offering itself to a provisioner. **La ràdio ha d'estar lliure**: si el teclat de Bluetooth és enlaire això
  // contesta que no i ho diu, perquè dos anunciadors alhora no hi caben (docs/50).
  virtual bool start(const char* name) {
    (void)name;
    return false;
  }
  virtual void stop() {}
  virtual bool is_up() const { return false; }
  // What the provisioner left: the unicast address of the first element of this node, and whether it is in a mesh yet.
  virtual bool provisioned() const { return false; }
  virtual uint16_t address() const { return 0; }

  // ---- la xarxa que aquesta placa té a les mans ----
  // **Aquesta placa és el provisionador de la seva pròpia xarxa** (0.190.218, l'amo: *«no puc provisionar jo manualment la clau del mesh
  // per linia de comandaments, com amb el wifi?»*). Un node no pot rebre les claus a mà —la provisió **és** l'intercanvi que les crea—, o
  // sigui que el que sí que es pot és tenir-les aquí i repartir-les als nodes. Dues portes: aquest build porta el provisionador, i aquesta
  // placa ja té xarxa.
  //
  // **Les claus són setze bytes i prou**: el mesh no hi posa cap checksum, els bytes que es donen són els bytes de la clau. El que mana la
  // xarxa és la clau de xarxa (NetKey); la d'aplicació (AppKey) és la que fa que els models es puguin parlar (docs/50).
  virtual bool can_keep_the_keys() const { return false; }
  virtual bool has_the_network() const { return false; }
  // Unes claus noves del generador del xip, i desades. False si aquest build no porta el provisionador o si la pila les refusa.
  virtual bool make_keys(uint8_t net_key[16], uint8_t app_key[16]) {
    (void)net_key;
    (void)app_key;
    return false;
  }
  // Les claus que et dóna l'amo, tal qual.
  virtual bool own_the_network(const uint8_t net_key[16], const uint8_t app_key[16]) {
    (void)net_key;
    (void)app_key;
    return false;
  }
  // Quines són, per poder-les apuntar fora d'aquí.
  virtual bool network_keys(uint8_t net_key[16], uint8_t app_key[16]) const {
    (void)net_key;
    (void)app_key;
    return false;
  }
  // **Escoltar qui no és enlloc** i comptar-los: un provisionador només pot donar adreça a qui ha sentit. Espera `ms` mil·lisegons, o
  // sigui que bloqueja el que ho demana, i el prompt ho diu abans.
  virtual int watch(uint32_t ms) {
    (void)ms;
    return 0;
  }
  virtual int heard_count() const { return 0; }
  // Qui és el número `index` de la llista: la seva UUID, la ràdio per la qual s'ha sentit i la força amb què arriba.
  virtual bool heard_at(int index, uint8_t uuid[16], int* rssi) const {
    (void)index;
    (void)uuid;
    (void)rssi;
    return false;
  }
  // **Donar-li una adreça**: zero vol dir la propera de la taula, i el número que ha quedat es diu a `given`.
  virtual bool provision(int index, uint16_t address, uint16_t* given) {
    (void)index;
    (void)address;
    (void)given;
    return false;
  }
  // Qui és dins la xarxa d'aquesta placa: torna quants n'hi ha, omplint `addresses` fins a `cap`.
  virtual int known(uint16_t* addresses, int cap) const {
    (void)addresses;
    (void)cap;
    return 0;
  }
};

class IBluetooth {
 public:
  virtual ~IBluetooth() {}
  // Si aquesta placa té la ràdio i el build la porta a dins. Una placa que diu que no no ha de rebre cap crida i el sistema ho diu.
  virtual bool can_bluetooth() const { return false; }
  // **Encendre el teclat amb el nom que veurà l'altre aparell**, i apagar-lo. Encendre'l dues vegades no és un error.
  virtual bool keyboard_start(const char* name) {
    (void)name;
    return false;
  }
  virtual void keyboard_stop() {}
  // Si algú hi és connectat: un teclat que no ho diu envia caràcters al buit.
  virtual bool keyboard_connected() const { return false; }
  // **El nom amb què aquesta placa s'anuncia**: el seu, amb els dos últims bytes de l'adreça del ràdio (`lunokOS-A1B2`), perquè dues
  // plaques a prop no siguin dues vegades el mateix nom (0.190.197).
  virtual const char* device_name() const { return "lunokOS"; }
  // Una tecla premuda i deixada anar, amb les de modificació del HID (`0x02` és Shift). Torna fals si no hi ha ningú a l'altre cap.
  virtual bool keyboard_tap(uint8_t mods, uint8_t usage) {
    (void)mods;
    (void)usage;
    return false;
  }
  // **Si el ràdio vol el xip despert**: verdader mentre el teclat és engegat, o sigui que el torn no obre el pany de la becaina.
  virtual bool wants_the_chip_awake() const { return false; }

  // ---- l'iBeacon (0.194.0) ----
  //
  // **El mateix anunci amb un altre contingut** (l'amo: *«pots integrar això (iBeacon) al ble dev? `ble dev beacon on`»*, amb el
  // tutorial de Mynewt a la mà): a beacon does not want a link and does not carry a report map — it *is* four numbers said into the air
  // (a proximity UUID, a major, a minor and the power it is expected to give at one metre) so a phone can place it. **A legacy
  // advertisement is one payload and not two**, o sigui que mentre el beacon és ences el costat dispositiu anuncia això i no el teclat,
  // i el teclat és apagat però no oblidat: `beacon_stop()` el torna a posar (docs/50).
  //
  // El format és el de Apple i el posa la pila de la IDF: `ble_ibeacon_set_adv_data` de NimBLE fa els vint-i-cinc bytes de dades de
  // fabricant (`4C 00`, `02 15`, l'UUID, el major i el minor en big-endian, i el senyal calibrat) i les banderes, que és el tutorial de
  // Mynewt sencer (docs/50).
  struct Beacon {
    uint8_t uuid[16];  // the proximity UUID, in the order the air carries it
    uint16_t major;
    uint16_t minor;
    int8_t power;  // the RSSI this beacon is expected to give at one metre, in dBm
  };
  virtual bool beacon_start(const Beacon& beacon) {
    (void)beacon;
    return false;
  }
  // ---- la bateria i la consola, per BLE (0.196.0) ----
  //
  // **Dos serveis de la taula de GATT que es poden obrir i tancar** (l'amo: *«Afegeix al bt dev battery on/off per notificar la batería»*
  // i *«bt dev console on/off per exposar la shell de sistema (el mateix que es veu via serial, però per ble)»*): el de la bateria és el
  // **Battery Service** del Bluetooth (0x180F) amb el nivell a 0x2A19, que és el que un telèfon o un rellotge ja sap llegir; el de la
  // consola és el **Nordic UART Service** —dues característiques, una per escriure-hi i una per rebre'n notificacions—, que és el que
  // parlen les apps de terminal de BLE que hi ha per telèfon (docs/50).
  //
  // **Tancats vol dir que no contesten**: the services are in the table from the first pair and `on` is what makes them answer, o
  // sigui que una escriptura al port de la consola amb el commutador apagat es refusa i no arriba a la shell de ningú (docs/30,
  // docs/50).
  typedef void (*ConsoleSink)(const uint8_t* data, uint16_t len);
  virtual bool battery_on() { return false; }
  virtual void battery_off() {}
  virtual bool battery_is_on() const { return false; }
  // **El nivell, quan canvia**: fals vol dir que no hi ha ningú escoltant o que el servei és tancat, i cap de les dues coses és un error.
  virtual bool battery_level(uint8_t percent) {
    (void)percent;
    return false;
  }
  // **On arriba el que s'escriu pel port de la consola**, i el que se n'envia: el ràdio no sap què és una comanda, només rep bytes i els
  // dona al sistema, que és qui té la shell (docs/30, docs/50).
  virtual void console_sink(ConsoleSink sink) { (void)sink; }
  virtual bool console_on() { return false; }
  virtual void console_off() {}
  virtual bool console_is_on() const { return false; }
  virtual bool console_send(const uint8_t* data, uint16_t len) {
    (void)data;
    (void)len;
    return false;
  }
  // Apagar-lo: si el costat dispositiu era engegut, el teclat i el ratolí tornen a anunciar-se.
  virtual void beacon_stop() {}
  virtual bool beacon_on() const { return false; }

  // ---- el costat amfitrió (0.190.188) ----
  //
  // **La mateixa ràdio, l'altra direcció**: `bt scan` mira qui hi ha, `bt pair` es queda amb un i el que el teclat escriu arriba al
  // sistema com si vingués del cable. The board is the **host** here and the keyboard is somebody else's, o sigui que les tres
  // preguntes són les tres d'una connexió: qui hi ha, a qui ens hem lligat, i si la lligadura encara hi és (docs/30, docs/50).
  //
  // **La ràdio mana sobre la son igual que al device**: un link que dorm no és un link, i el pany del torn ja pregunta
  // `wants_the_chip_awake` per als dos costats.
  virtual bool can_host() const { return false; }
  // **La ràdio per a l'amfitrió, sense ser un teclat** (0.190.192): `bt host on` encén la pila i prou; `off` tanca el link i, si el
  // dispositiu d'aquesta placa també és apagat, baixa la ràdio sencera. Sense això, l'amfitrió només s'encenia quan algú explorava, i
  // l'estat no deia de qui era la ràdio (docs/50).
  virtual bool host_on() { return false; }
  virtual void host_off() {}
  // **Si el costat amfitrió s'ha demanat**: la ràdio pot ser engegada pel dispositiu i això no vol dir que algú escolti. És el que fa
  // que `bt` pugui dir `off` quan els dos costats ho són, i `on` quan un dels dos viu (docs/30, docs/50).
  virtual bool host_is_up() const { return false; }
  // Un aparell que ha contestat a l'exploració, amb **tot** el que l'anunci porta (0.190.196, l'amo: *«No em crec que no pesqui ni un sol
  // nom! ni identificar el tipus, ni cap info adicional (senyal batería… no sé alguna dada mes)»*): the signal it came with, the appearance
  // the maker declared and the first service it announced, que és el que permet dir què és quan no diu com es diu (docs/50).
  struct Peer {
    uint8_t addr[6];
    uint8_t addr_type;
    uint8_t name[24];  // buit si l'anunci no en duia
    bool hid;          // anuncia el servei 0x1812
    int8_t rssi;       // el senyal, en dBm: més a prop de zero és més a prop
    uint16_t appearance;  // 0 si no en declara cap
    uint16_t service;     // el primer servei de setze bits que anuncia, 0 si no n'anuncia cap
  };
  // **Explorar i esperar**: omple `out` i torna quants n'hi ha. Bloqueja el que demana (uns segons), o sigui que el prompt ho diu
  // abans de fer-ho; res del torn depèn d'això (docs/30).
  virtual uint8_t host_scan(uint32_t ms, Peer* out, uint8_t cap) {
    (void)ms;
    (void)out;
    (void)cap;
    return 0;
  }
  // Lligar-se a un dels aparells de l'última exploració, per l'índex. Torna fals si l'aparell no hi és o si la ràdio no pot.
  virtual bool host_pair(uint8_t index) {
    (void)index;
    return false;
  }
  // Si hi ha algú lligat i escrivint.
  virtual bool host_connected() const { return false; }
  // **Si aquesta placa ha aparellat mai res** (0.190.203): preguntar-ho a la botiga del ràdio vol la pila a dalt, i engegar la pila
  // d'una placa verge només perquè et digui que no hi ha res —amb la calibració del PHY que se'n va darrere la primera vegada— és el
  // que aquesta pregunta estalvia. Un ràdio que no ho sap diu que sí: el defecte és l'antic comportament, i equivocar-se cap a
  // «intenta-ho» no trenca res (docs/50, docs/60).
  // **Si l'altre cap ha encès els reports** (0.190.208): un aparell es connecta i tot seguit escriu el CCCD de cada característica
  // d'entrada que vol rebre, i fins que no ho fa una notificació no arriba enlloc. És la diferència entre «connectat» i «connectat i
  // escoltat», i una pàgina que ho sap ho pot dir en comptes de deixar una persona prement tecles al buit (docs/50).
  //
  // **I des de la 0.191.0 se'n pot preguntar un de sol** (l'amo: *«i no podem anunciar els dos dispositius sempre? després que hi hagi
  // una app de teclat o de mouse és indiferent»*): la placa publica sempre el teclat i el ratolí, o sigui que l'altre cap pot encendre'n
  // un i no l'altre —i una pàgina que mou un punter vol saber del ratolí, no del parell—. `reports_enabled()` és el «algun», que és el
  // que una persona vol saber d'un cop d'ull (docs/50, docs/30).
  // **Oblidar l'altre cap** (0.191.2): un amfitrió es queda **el report map que va llegir quan va aparellar** i no el torna a llegir mai, o
  // sigui que un mapa que ha canviat —com el d'aquesta placa, que va passar de tres mapes a un— deixa un vincle que parseja els reports
  // amb el mapa vell. El que la placa pot fer és oblidar el vincle per la seva banda i tancar l'enllaç, perquè l'altre cap hagi de tornar a
  // aparellar; **el que no pot fer és esborrar la memòria de l'altre**: allà el que cal és treure l'aparell de la llista (docs/50).
  virtual void device_forget() {}

  enum class Report : uint8_t { kKeyboard = 0, kMouse = 1 };
  virtual bool report_enabled(Report which) const {
    (void)which;
    return false;
  }
  virtual bool reports_enabled() const { return false; }

  virtual bool host_remembers() const { return true; }
  // **Tornar amb el teclat que ja hi era** (0.190.191): the bonds are in the store, so the board can go back to the keyboard it was
  // paired with without a person scanning anything. Llança el link i torna; el que arribi després ja ho diu `host_connected` (docs/50).
  virtual bool host_reconnect() { return false; }
  // **Els cinc llums del teclat virtual**, tal com els va deixar l'altre aparell (0.190.191). Zero en una placa sense ràdio.
  virtual uint8_t keyboard_leds() const { return 0; }

  // ---- el que aquesta placa és, no el que escolta (0.190.193, i sempre les dues coses des de la 0.191.0) ----
  //
  // **El teclat i el ratolí, sempre i alhora** (0.191.0, l'amo: *«i no podem anunciar els dos dispositius sempre? després que hi hagi una
  // app de teclat o de mouse és indiferent»*): hi havia hagut tres modes —teclat, ratolí i els dos— i el mode era una decisió que
  // l'amfitrió pagava, perquè un mapa nou no cap en un vincle vell (un telèfon llegeix el mapa quan aparella i no el torna a llegir mai).
  // Amb un sol mapa, amb **report IDs** (el teclat l'1 i el ratolí el 2), no hi ha res a canviar i no hi ha res a esborrar: el que una
  // app faci amb la placa és cosa de l'app i no de l'anunci (docs/50). **El preu és el boot protocol**, que un aparell amb report IDs no
  // pot servir; el que el vol era només el teclat d'un PC abans del sistema operatiu (docs/50).
  // Un moviment relatiu, i el report torna a zero de seguida perquè el cursor no se'n vagi.
  virtual bool mouse_move(int8_t dx, int8_t dy) {
    (void)dx;
    (void)dy;
    return false;
  }
  // Els botons que queden premuts: bit 0 esquerre, 1 dret, 2 mig.
  virtual bool mouse_press(uint8_t buttons) {
    (void)buttons;
    return false;
  }
  virtual uint8_t mouse_buttons() const { return 0; }
  // Un clic sencer: es prem, s'espera un moment i es deixa anar.
  virtual bool mouse_click(uint8_t buttons) {
    (void)buttons;
    return false;
  }
  // **Desfer la lligadura**: el que el teclat té desat a l'altra banda també s'ha de poder esborrar, o un aparell que ja no hi és es
  // queda lligat per sempre (docs/50).
  virtual void host_forget() {}
  // **On van les tecles**: el ràdio no sap què és un caràcter, només rep els bytes d'un report HID i els dona al sistema amb la seva
  // mida —vuit per a un teclat, tres o quatre per a un ratolí—. El que en fa text és el nucli, que és qui sap de tecles (docs/30, docs/50).
  typedef void (*HostSink)(const uint8_t* report, uint8_t len);
  virtual void host_sink(HostSink sink) { (void)sink; }
};

// How deep the sleep is (docs/90, M6). `kLight` keeps the memory, the interpreter
// and the console: the system goes on where it was. `kDeep` keeps only what the
// RTC holds: the board restarts and boots again, and what was running comes back
// from the session record (docs/30).
enum class SleepMode : uint8_t { kLight = 0, kDeep = 1 };

// **Per què s'ha reiniciat el xip, que és una cosa que només ell sap** (0.190.68, l'amo: *«recorda que l'esp32 te estats al arrencar, així
// el firmware sabrà si ha despertat normal o d'un deep sleep»*). L'ESP32 respon `esp_reset_reason()` i el que en surt no és opinable:
// power-on, deep sleep, un reinici de programari, un pànic, una caiguda de tensió. És el que fa que una placa sàpiga si l'arrencada és
// la primera o una tornada, i el que cap marca desada pot saber del cert —una marca es pot perdre, i una placa que ha quedat sense
// corrent just després d'escriure-la diria que ha dormit quan el que ha passat és que l'han desendollada— (docs/50, docs/60).
enum class ResetCause : uint8_t {
  kPowerOn = 0,    // el corrent ha tornat: el primer engegament, o la pila posada
  kDeepSleep = 1,  // el xip s'ha despertat d'un son profund (ESP_RST_DEEPSLEEP)
  kSoftware = 2,   // un reinici demanat pel sistema (ESP_RST_SW)
  kPanic = 3,      // l'excepció, el gos o el watchdog del xip (ESP_RST_PANIC i companyia)
  kBrownout = 4,   // la tensió ha caigut per sota del que el xip vol (ESP_RST_BROWNOUT)
  kExternal = 5,   // el pin de reset (ESP_RST_EXT)
};

// **El nom de la causa, en una paraula**, que és el que una línia del registre pot dir.
inline const char* reset_cause_word(ResetCause cause) {
  switch (cause) {
    case ResetCause::kDeepSleep:
      return "a deep sleep";
    case ResetCause::kSoftware:
      return "a restart of the system";
    case ResetCause::kPanic:
      return "a panic";
    case ResetCause::kBrownout:
      return "a brownout";
    case ResetCause::kExternal:
      return "the reset pin";
    default:
      return "the power coming back";
  }
}

class IPower {
 public:
  virtual ~IPower() {}
  // The battery, when the board can tell. False is "this board has no gauge", not
  // "empty": the ATOM is powered by its cable and says it cannot tell (docs/50).
  virtual bool battery(int& mv, int& pct, bool& charging) = 0;
  // **Si hi ha un cable a dins, que no és el mateix que estar carregant** (0.192.12, l'amo: *«I perqué no es veu l'icona del usb quan està
  // endollat?»*): `charging` is a cell that is filling —the BQ25896's `CHRG_STAT` in pre-charge or fast—, and a board with a full cell on
  // its cable says `done` and answers **no** there; the cable, though, is in. The mark of the port of the bar is «there is a cable»
  // (a feeding one or one an app has taken), so it asks this and not the other, and a board that cannot tell answers false and keeps the
  // figure of the bolt as its only word about the charge (docs/30, docs/50, docs/85).
  virtual bool on_a_cable() const { return false; }
  // **El que un indicador de càrrega sap i un divisor en un ADC no** (0.190.155, l'amo: *«Mira els exemples de batería (es el hardware
  // que tenim?) ... Si ens serveixen, afegeix la informació a l'app battery»*). The T5 E-Paper S3 Pro carries a **BQ27220** that
  // counts coulombs —the maker's own example reads its current, its capacities and its health—, i una placa amb un divisor només té
  // dos números. `kUnknown` is «this part does not say» and is not zero: zero mA is a board doing nothing and not a board that
  // cannot measure. The defaults answer false, o sigui que una placa sense indicador no paga res (docs/30, docs/50, docs/60).
  static const int kUnknown = -32768;
  struct BatteryDetail {
    int current_ma = kUnknown;   // negative while the cell feeds the board
    int full_mah = kUnknown;     // what the cell holds now that it is older
    int left_mah = kUnknown;
    int design_mah = kUnknown;   // what it was sold as
    int temp_c10 = kUnknown;     // tenths of a degree Celsius, the cell's own sensor
    int health = kUnknown;       // percent of the design capacity the gauge still trusts
    int cycles = kUnknown;       // charge cycles it has counted
    // **I el que el gauge diu del ple** (0.192.16, l'amo: *«perqué si la batt està al 38% no està carregant?»*): `FC` is the gauge's own
    // «full-charged is detected» —the bit it puts up when the charge current has tapered at the top of the cell— and `TCA` its terminate-charge
    // alarm. No és el percentatge i no és el carregador: és el que el comptador pensa de la cel·la, i sense això un 38 % amb 4159 mV no es pot
    // desfer (docs/50, docs/95).
    bool declared_full = false;
    bool terminate_alarm = false;
  };
  virtual bool battery_detail(BatteryDetail& out) {
    (void)out;
    return false;
  }

  // **El carregador, que és l'altre xip de la bateria** (0.190.156, l'amo: *«Tenim el PMU molt abandonat ... Busca quin model
  // tenim al T5S3 Pro i implementa'l»*). The T5 E-Paper S3 Pro carries a **BQ25896** at 0x6B —the maker's README says so and its
  // own driver reads these same registers— and what it adds to the gauge is the other half of the picture: whether a cable is in,
  // what it is (a host or an adapter), how much current is going **into** the cell, the voltage of the input and the system rail.
  // A board without one answers false and the page says nothing about a charger it does not have (docs/30, docs/50, docs/60).
  struct ChargerDetail {
    int vbus_mv = kUnknown;    // 0 with no cable, and the value of the supply with one
    int ichgr_ma = kUnknown;   // what is going into the cell, and nothing while it is not charging
    int vsys_mv = kUnknown;    // the rail the board itself runs on
    int ntc_pct = kUnknown;    // the thermistor, as a percentage of REGN and not a temperature
    const char* input = "";    // "no input", "USB host", "adapter" or "OTG"
    const char* charge = "";   // "not charging", "pre-charge", "fast" or "done"
    bool power_good = false;
  };
  virtual bool charger_detail(ChargerDetail& out) {
    (void)out;
    return false;
  }
  // Sleeps until `at_epoch_ms` (0 = no deadline). **A deep sleep does not return**: the board restarts (docs/90). True when it came
  // back from a light sleep.
  //
  // **És l'única crida d'una baixada** (0.190.15): the idle policy, the card of the system and the `deepsleep` of the prompt all come
  // here, and a board that has **both** doors arms **both** —the hour it was told and the key a person presses— when it has them. Zero
  // is not an hour: it is a board that comes back on a key, and a board with no key and no hour is a board that did not answer this
  // call at all (`can_sleep_with_the_keys` and `deep_powers_off` are how the system knows before asking) (docs/50, docs/60).
  // **`keep_the_panel`** (0.190.19, l'amo: *«No baixis els rails, així no es veuen canvis ni es gasta energia»*): a way down that has not
  // touched the glass leaves the panel's supply where it is, so the controller keeps its state and the image it is showing and the next
  // wake has nothing to re-initialise and nothing to repaint. It is the way down of an alarm that runs with nobody in front (docs/50,
  // docs/60).
  virtual bool sleep(SleepMode mode, int64_t at_epoch_ms, bool keep_the_panel = false) = 0;

  // **Una becaina en lloc d'esperar** (0.190.63, l'amo: *«[X] Light sleep instead of wait. Aplica lightSleep allá on puguis»*): el
  // sistema passa mitja vida esperant —el torn del nucli quan no hi ha res, la tinta del vidre, el marge de la targeta abans de tallar
  // el corrent— i esperar amb el processador despert és el corrent més fàcil d'estalviar d'aquesta placa: el xip atura els rellotges,
  // la RAM i l'estat no es perden (no és un reset), i el temporitzador del RTC el torna sol. `nap(max_ms)` dorm com a molt `max_ms` i
  // **torna sempre** —a diferència de `sleep`—, o sigui que el que l'ha demanat segueix el seu camí quan torna.
  //
  // **Les portes del despert són les de la placa**, i per això la resposta no és del nucli: una tecla, el dit al vidre (si és una
  // porta) i la consola, perquè un prompt que no llegeix mentre dorm és un prompt que ha perdut el que s'hi ha escrit. `can_nap()` és
  // la pregunta —una placa que no pot escoltar res mentre dorm diu que no, i llavors el sistema espera despert com sempre— i una placa
  // que no en sap res no ha de fer res: la base respon que no i `nap` no dorm (docs/50, docs/60, docs/95).
  virtual bool can_nap() const { return false; }

  // **Si la darrera baixada ha reiniciat el xip, i es pregunta una vegada** (0.190.68, docs/85): en una placa de debò un son profund no
  // torna —el xip s'arrenca—, i el model de l'emulador sí que ha de tornar perquè el procés no pot reiniciar-se. El que fa aquesta
  // pregunta és dir «aquest son ha estat una arrencada» i buidar-la, o sigui que el nucli que la llegeixi acabi el torn amb un
  // `kernel::boot` i no segueixi amb el que hi havia a la RAM. False per a tota placa: allà la resposta no arriba mai perquè la crida
  // no torna.
  virtual bool take_the_restart() { return false; }
  // **Quina porta ha despertat la placa** (0.191.5, i amb noms a la 0.191.11): `esp_sleep_get_wakeup_cause()` diu «ho ha fet una línia» i
  // prou, i en una placa amb tres línies armades alhora això és el mateix que no dir res —la línia de l'arrencada deia «a finger on the
  // glass» quan el que havia baixat era el BOOT, o al revés—. El que sap quines línies hi havia és la placa, i és ella qui ho contesta.
  //
  // **I la diferència és de política, no de log**: una porta que és una persona (la tecla, el dit) vol dir que el vidre s'ha de despertar i
  // pintar la pàgina; una que és el rellotge o el temporitzador vol dir que no hi ha ningú i que el vidre es queda com el son el va deixar
  // (docs/30, docs/50, docs/60). Una placa que no ho sàpiga diu `kUnknown` i el nucli es queda amb el que digui el seu rellotge.
  enum class Door : uint8_t { kUnknown = 0, kTimer, kKey, kGlass, kClock, kMore };

  virtual Door the_door_that_opened() const { return Door::kUnknown; }

  virtual bool nap(int32_t max_ms) {
    (void)max_ms;
    return false;
  }

  // **El pany de la becaina que fa el xip sol** (0.190.178, l'amo: *«activem el lightsleep automàtic d'idf»*): `nap` és una dormida que
  // el nucli demana i espera; aquest parell és la mateixa dormida **sense demanar-la** — obre les portes de la placa (una tecla, el vidre
  // i la consola) i deixa que la gestió de potència del xip adormi mentre qui espera és a la cua. `open_the_nap_doors()` diu si les
  // portes s'han pogut obrir; si diu que no, **no s'ha de dormir**, i el nucli espera despert com sempre. `close_the_nap_doors()` les
  // tanca i torna la línia del panell al que era (docs/50, docs/60).
  //
  // **I el que no s'obre no dorma** (0.190.178): there is no lock to take and no flag for the kernel to forget, because the chip asks
  // the board before every automatic sleep, and a closed pany answers no. That is what keeps the ink of a paint —twenty-millisecond
  // slices the chip must not take (0.190.69)—, a console that has not been quiet, and a port lent to an app out of a light sleep,
  // without any of them having to know that the sleep exists (docs/50, docs/95).
  virtual bool open_the_nap_doors() { return false; }
  virtual void close_the_nap_doors() {}
  // **El pany llarg: «mentre jo el tingui, no adormis el xip»** (0.190.184, l'amo: *«Es podia parar el lightsleep simplement amb un mutex o
  // algo així oi?»* — i sí: és el lock `ESP_PM_NO_LIGHT_SLEEP` de la gestió de potència). És una cosa diferent del pany del torn: aquell obre
  // les portes per a **una** espera, i aquest diu «no n'hi ha cap» mentre dura. **Compta**, o sigui que el poden agafar la consola, el teclat i
  // una app alhora i el xip només dorma quan no el té ningú (docs/50, docs/60).
  virtual bool hold_no_light_sleep(bool on) {
    (void)on;
    return false;
  }
  // **Qui ha despertat la darrera becaina** (0.190.178): `nap` ho sap perquè és qui ha dormit; la becaina que fa el xip sola no torna per
  // la mateixa crida, o sigui que la pregunta és a part i la contesta la placa quan en sap. Null és «el xip no sap per què s'ha despertat»,
  // que vol dir que no ha dormit (docs/50, docs/95).
  virtual const char* the_word_of_the_last_nap() { return nullptr; }

  // Whether this board's deep sleep is a **full power off**: the board does not sleep,
  // it stops being powered and the clock chip's alarm turns it back on (docs/50). It is
  // the least current a board can draw, and it means the wake does not come from the
  // chip's own timer but from the clock, which is why the system says so at boot.
  virtual bool deep_powers_off() const { return false; }

  // **Si la política d'oci pot posar aquesta placa a dormir** (0.190.272, l'amo: *«l'M5Paper funciona genial i s'apaga/deepsleep,
  // aquesta versió no sembla fer-ho bé»*). És una pregunta diferent de la de dalt i la diferència va costar un forat: `deep_powers_off`
  // diu **com** és la baixada —una apagada de debò, amb el rellotge del xip de rellotge que torna a engegar la placa— i el nucli la feia
  // servir com a **si aquesta placa pot dormir quan no hi ha ningú**, o sigui que la T5 E-Paper S3 Pro, on el son profund és un son
  // profund de l'ESP32 i no una apagada, no dormia mai per inactivitat: el temporitzador de `Sleep after` no disparava i la placa es
  // quedava encesa amb el vidre il·luminat. **El que la política ha de saber és si baixar estalvia la placa i si hi ha una porta per
  // tornar-ne**, i això és el que responen `can_sleep_with_the_keys`, `can_wake_on_touch` i la cua d'alarmes, que el nucli ja mira
  // després (docs/50, docs/60).
  //
  // La resposta per defecte és la de sempre —una placa que no diu res només dorm si la seva baixada és una apagada—, o sigui que cap
  // de les altres cinc bandes canvia de comportament per aquesta línia (docs/50, docs/60).
  virtual bool can_sleep_on_idle() const { return deep_powers_off(); }
  // Cuts the board's own supply, and **nothing comes back from it**: it is not a sleep, no alarm is
  // armed, and only the button —or the cable, on a board that cannot switch itself off while it is
  // plugged in— brings the board up again (docs/50). False is a board that cannot do it, said out
  // loud instead of looking like it worked: the ATOMs have no hold on their supply and the Paper
  // does not drop while the cable is in.
  virtual bool power_off() { return false; }
  // **Si aquesta placa sap tallar-se el corrent, preguntat sense intentar-ho** (0.189.18): `power_off()` is the act, and a
  // board that cannot do it only says so after the picture and the wait —so the shutdown sequence has to know **before** it
  // spends the margin of a card's rest on a board that was never going to be cut—. True is a board with a hold on its own
  // supply; the default is the honest «no» of the one that is powered by its cable (docs/50, docs/75).
  virtual bool can_power_off() const { return false; }
  // **La tecla que desperta aquesta placa** (0.189.25): whether a key a person presses brings the chip back from a deep sleep. It is a
  // capability of the wiring and not a way down of its own: **a board with a key sleeps with `sleep(kDeep, 0)`**, the same call as
  // every other way down (0.190.15), and the board arms the key there along with the hour when it has one (docs/50).
  virtual bool can_sleep_with_the_keys() const { return false; }

  // **Tornar d'un son profund amb el dit** (0.190.16, l'amo: *«del DeepSleep s'ha de poder tornar sempre a part de per les alarmes, també
  // els botons físics i opcionalment el panell tàctil (per defecte false) així no cal mantenir despert el panell»*): whether this board
  // can hear a finger while the chip is asleep. **Off by default and off on every board that does not say otherwise**: hearing a finger
  // means keeping its touch controller awake —and scanning, at the quiet rate— for the whole sleep, which is exactly the current a deep
  // sleep exists to stop. **I el rail del panell sí que baixa** (`ISurface::rest`, que és el que fa la imatge del son): el que es queda
  // encès és el controlador, que és qui ha de veure el dit (0.192.0, docs/50, docs/60).
  //
  // The key and the alarm are **not** this switch: a deep sleep always arms them where the board has them, and a board that says false
  // here is a board that comes back on its hour or on a key, never on the glass (docs/30, docs/50).
  virtual bool can_wake_on_touch() const { return false; }
  // **L'ajust, i tota placa el guarda** (0.190.17, l'amo: *«a koala, atom i matri[x] simplement s'ignora el paràmetre»*): the switch of the
  // Battery tab is a setting of the system and not a promise of the wiring, o sigui que una placa que no pot sentir el dit **l'accepta i
  // el deixa estar** en lloc de refusar-lo amb un error que una persona ha de llegir com si hagués fet alguna cosa malament. The way down
  // is what reads it, and a board whose `can_wake_on_touch()` is false has no arming to do with it (docs/30, docs/50).
  virtual bool wake_on_touch() const { return wake_on_touch_; }
  virtual bool set_wake_on_touch(bool on) {
    wake_on_touch_ = on;
    return true;
  }
  // **La consola com a porta de la becaina** (0.190.147, l'amo: *«Posa un toggle a Settings->Battery que doni la opció a "Wake from
  // UART"»*): a light sleep comes back on a key, on the glass and on the console, and the console is the door a board with no keyboard
  // cannot lose —el prompt és l'única manera de parlar-hi—, però també és un cable, i qui té la placa en una prestatgeria pot voler que
  // les esperes només les desfaci un dit. Encès de sèrie, que és el que aquest sistema feia abans de tenir el commutador (docs/50, docs/60).
  // **Les portes de la darrera dormida, en una frase que el nucli pugui desar** (0.190.149, l'amo: *«quan entra en sleep perdo el serial
  // ... no podrè veure mai el log»*). A deep sleep is a **reset**: the line that says which door was already open is gone by the time the
  // cable is plugged back in, and that line is the whole explanation of a board that comes back at once. A board that can keep it in a
  // memory that survives the sleep answers here, once, at the boot that follows it (docs/50, docs/60).
  // **I qui la desa és el nucli, no la placa** (0.190.151, l'amo: *«el botó de power fa un reset i tot s'envà a la merda»*): the RTC RAM
  // survives a reset of the chip and **not** a cycle of the power button, which on this board is the supply going away, o sigui que el
  // text ha d'acabar a la botiga i la placa només ha de saber dir-lo (docs/50, docs/70).
  virtual size_t describe_the_last_sleep(char* out, size_t cap) {
    (void)out;
    (void)cap;
    return 0;
  }
  virtual bool wake_from_uart() const { return wake_from_uart_; }
  virtual bool set_wake_from_uart(bool on) {
    wake_from_uart_ = on;
    return true;
  }
  // **Si la consola d'aquesta placa pot ser una porta del son lleuger** (0.190.152): a board whose console is the chip's own USB cannot,
  // because a light sleep stops that peripheral and a byte typed while the chip naps is lost no matter what is armed (docs/50). The
  // switch of the Battery tab is a wish of the person; this is a fact of the wiring, and the arming asks both.
  virtual bool the_console_can_wake() const { return true; }

  // Where this board can be woken from: "timer", "button", "console" or "none".
  // Said by the board, because it is the wiring that decides it (docs/50).
  virtual const char* wakes() = 0;
  // How fast the processor runs, in MHz, and the change of that speed. The steps
  // are the board's: a value it cannot do is refused, and never rounded to the
  // nearest one, because a rounded speed is a lie about what it costs (docs/50).
  virtual uint32_t cpu_mhz() = 0;
  virtual bool set_cpu_mhz(uint32_t mhz) = 0;
  // **La velocitat la governa el `esp_pm` del xip** (0.190.175, l'amo: *«la cpu es pot escalar sola a esp_idf... activa la opció i
  // adapta el que tenim fet manualment»*). A board with the chip's power management answers here and `set_cpu_mhz` is turned into a
  // range of one value; a board without it —the emulator, a build with `CONFIG_PM_ENABLE` off— keeps moving the clock itself, which
  // is what this family did until today (docs/50, docs/60).
  //
  // `min` and `max` are the floor and the ceiling the chip may move between, in MHz. **The chip keeps the tick and the PLLs in step
  // when it changes** —the SPI, the I2C, the I2S and the SDMMC drivers take the `APB_FREQ_MAX` lock around their transactions, so
  // the glass and the card do not care— and the frequency it is on is read with `cpu_mhz()` (docs/50).
  virtual bool set_cpu_range(uint32_t min_mhz, uint32_t max_mhz) {
    (void)min_mhz;
    (void)max_mhz;
    return false;
  }
  // **I quan el nucli vol el sostre i quan no** (0.190.175): `on` takes the chip's top-step lock and `off` gives it back, which is the
  // whole of what `hold_cpu` and `rest_cpu` used to do by hand. The chip counts the takes, so asking twice and giving back once leaves
  // it at the top —the same rule the old window had, without a clock to keep (docs/50, docs/60).
  virtual bool hold_cpu_top(bool on) {
    (void)on;
    return false;
  }
  // **Si aquesta placa escala el rellotge tota sola** (0.190.227, l'amo: *«Desactiva el Dynamic Frequency Scaling per l'M5 Atom Lite i l'M5
  // Atom Matrix. Només per aquests dos models. Que vagin sempre a 240Mhz»*): ho contesta la placa, que és qui sap quin xip té a sota. Una
  // placa que diu que no **no pot** tenir un rang de freqüències —el nucli li demana `240` i `240` i prou— i el nucli no li'n demana cap
  // altre: el commutador de la pàgina i el `power auto` ho diuen i no fan res, perquè un commutador que es mou i no canvia res és una
  // mentida (docs/50, docs/60).
  virtual bool scales_the_clock() const { return true; }

 protected:
  // **El commutador del dit, que és de tota placa** (0.190.17): kept here and not in each board, because the setting belongs to the system
  // and only the arming belongs to the wiring (docs/30, docs/50).
  bool wake_on_touch_ = false;
  bool wake_from_uart_ = true;
};

// Reserved: implemented when a board with a screen exists (docs/50).
class IDisplay {
 public:
  virtual ~IDisplay() {}
};

// How the glass paints (docs/50). The panel of the Paper has these and they are the whole vocabulary
// of an e-ink refresh: `kInit` is the two-second beginning, `kDu` and `kDu4` are the quick ones for
// text and menus, and `kGc16` is the one that cleans the ghost of the quick ones.
enum class UpdateMode : uint8_t {
  kInit = 0,
  kDu = 1,
  kGc16 = 2,
  kGl16 = 3,
  kGlr16 = 4,
  kGld16 = 5,
  kDu4 = 6,
  kA2 = 7,
};

// **Com arriben els píxels al controlador** (0.189.66): la mateixa imatge es pot enviar de tres maneres i cada una té el seu
// preu. No és la qualitat de la tinta —això és `UpdateMode`— sinó com es porta la imatge pel cable, i el que guanya es mesura
// a la placa i no es discuteix (docs/50).
enum class FlushStyle : uint8_t {
  // **Una transacció per fila**: el camí de sempre i el que se sap que funciona. Cada fila paga el seu prefix i la seva vora
  // de CS, i això són 1 100 µs per fila mesurats a la placa (docs/50, docs/95).
  kRows = 0,
  // **Bursts**: un sol prefix per tros i el CS aguantat fins que el tros s'acaba. El que costa passa a ser el cable —800 ns
  // per byte— i el preu és que un prefix de més, amb el CS aguantat, el controlador el llegeix com dos píxels i la pàgina
  // llisca (mesurat l'1/11, docs/50).
  kBurst,
  // **Dos bits per píxel**: la meitat dels bytes, i el controlador els llegeix com quatre nivells de gris. **És el format més
  // fondo que el controlador té** —`IT8951_2BPP 0`, i no hi ha cap valor d'un bit a `IT8951_Defines.h`—, o sigui que la
  // imatge ha de baixar a quatre tons en enviar-la (docs/50).
  kBits2,
};

// **The name of a quality**, which is the word the glass speaks: every line of the log says `gl16` and not a
// five, because a number a reader has to look up in a header is a number that says nothing —and the question
// «which quality painted this rectangle?» is the one the log is read for— (docs/50, docs/85).
inline const char* mode_name(UpdateMode mode) {
  switch (mode) {
    case UpdateMode::kInit:
      return "init";
    case UpdateMode::kDu:
      return "du";
    case UpdateMode::kGc16:
      return "gc16";
    case UpdateMode::kGl16:
      return "gl16";
    case UpdateMode::kGlr16:
      return "glr16";
    case UpdateMode::kGld16:
      return "gld16";
    case UpdateMode::kDu4:
      return "du4";
    case UpdateMode::kA2:
      return "a2";
  }
  return "?";
}

// **El temps de tinta d'un mode, del full del fabricant** (docs/50): the ink is what a paint costs and the panel's own table says how long
// it takes to move —120 ms for `du`, 450 for `gc16`—, and that wait is paid **per paint and not per pixel**. És la base del cost que un
// vidre contesta i, quan el vidre no en diu res, la dels comptes del nucli (docs/50, docs/95).
inline uint32_t ink_ms_of(UpdateMode mode) {
  switch (mode) {
    case UpdateMode::kDu:
    case UpdateMode::kDu4:
      return 120;
    case UpdateMode::kA2:
      return 290;
    case UpdateMode::kGl16:
    case UpdateMode::kGlr16:
      return 350;
    case UpdateMode::kGc16:
    case UpdateMode::kGld16:
      return 450;
    case UpdateMode::kInit:
      return 0;
  }
  return 0;
}

// A surface an app can draw on, and the paint that puts it on the glass. It is a **surface** and not a
// streaming driver: what is drawn lives in the board's own memory, a rectangle remembers what changed,
// and `flush` sends only that rectangle. An e-ink glass takes 120 to 450 ms to move its ink, so a
// paint is asked for and **not waited on**: `idle()` is how a later turn knows it is done (docs/50).
class ISurface : public IDisplay {
 public:
  virtual uint16_t width() const = 0;
  virtual uint16_t height() const = 0;
  // **La pàgina i el vidre no són el mateix rectangle** (0.190.124): `width`/`height` are what an app draws in —this panel is a portrait page
  // of 540×960 on a landscape glass of 960×540, and the driver turns it— i aquestes dues diuen la mida de la tinta. Una pintada de mesura parla de
  // la del vidre, perquè és la que es paga (docs/50, docs/95).
  virtual uint16_t glass_width() const { return width(); }
  virtual uint16_t glass_height() const { return height(); }
  // 0 is black and 15 is white: the four bits of the panel, which is what every level here means
  // (docs/50).
  virtual void clear(uint8_t level) = 0;
  virtual void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) = 0;
  virtual void pixel(uint16_t x, uint16_t y, uint8_t level) = 0;
  // A whole rectangle of 4-bit pixels **that is already drawn**, one byte per two of them and the
  // left one in the high nibble: it is how the view of an app reaches the glass in one pass instead
  // of half a million `pixel` calls (docs/30, docs/50). `stride` is the bytes of a line of the source,
  // which is not always `w / 2` —a rectangle out of a wider canvas is the normal case— and pixels
  // outside the panel are dropped. **`packed` points at the first pixel of the region**, so a source
  // is indexed by its own column and not by the column it lands on: an odd `x` splits a byte with the
  // neighbour, and reading the source with the panel's column is a rectangle of somebody else's
  // pixels (docs/50).
  virtual void blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed,
                    uint16_t stride) = 0;
  // Sends what changed and asks for it to be painted, with the mode that says how good it has to
  // look. False when the panel did not take it.
  //
  // **Every write above marks the rectangle it touched**, and this call sends the one rectangle that
  // covers them all and forgets it. With nothing marked there is nothing to paint and nothing is sent:
  // a page nobody asked to be repainted is a page this system does not touch (docs/30, docs/50).
  virtual bool flush(UpdateMode mode) = 0;
  virtual bool idle() = 0;
  // **Els píxels que hi ha al vidre, donats enrere** (0.192.59): a row at a time, two pixels a byte and the left one in the high nibble
  // —the packing of every canvas of this system— **in the page's own frame**, so what a caller copies is the picture a person is
  // looking at and not the chip's rectangle. És la porta d'una captura de pantalla i d'una prova, i **no escriu res al vidre**: el que
  // torna és la imatge que el controlador té dibuixada. False quan aquest vidre no pot deixar els seus píxels (docs/50).
  //
  // El gir el desfà el vidre i no el nucli: `to_glass` és seu, i un vidre que tornés la imatge girada donaria un fitxer que a
  // l'ordinador es mira de costat (docs/50, docs/85).
  virtual bool read_row(uint16_t y, uint8_t* packed, uint16_t stride) const {
    (void)y;
    (void)packed;
    (void)stride;
    return false;
  }
  // **El paper, netejat al màxim que aquest panell sap** (0.192.11, l'amo: *«ENCARA VEIG LA IMATGE D'SLEEP SOTA EL GRID DEL HOME»*): a pass
  // of the whole glass that **does not trust what the driver remembers of it**. Un panell conduit per una llibreria d'alt nivell neix «blanc»
  // a cada arrencada —`epd_hl_init` posa els dos buffers a 0xFF—, o sigui que un `clear(15)` seguit d'un `gc16` és un diff de res, no s'envia
  // tinta i el registre diu que s'ha rentat: el dibuix del son es queda al vidre i la pàgina nova li cau a sobre. El vidre que se sap netejar
  // de debò ho diu aquí amb el seu propi cicle; el que no, fa el de sempre (docs/50).
  virtual bool wash_the_glass() {
    clear(15);
    return flush(UpdateMode::kGc16);
  }
  // **El torn del panell** (0.190.140): what a glass has to do when nobody is painting it. Aquesta placa hi baixa les tensions del TPS quan fa
  // estona que ningú no pinta —les que una pintada puja i que el fabricant baixa a cada update, pagant cent mil·lisegons per pintada que la UI
  // no ha de pagar—. Un vidre que no té res a fer cada torn no ha de respondre res (docs/50, docs/95).
  virtual void poll() {}
  // **Una pintada que només compta** (0.190.123, l'amo: *«Vull que facis possible això dels 60hz»*): pinta aquesta àrea amb aquesta qualitat
  // **sense tocar el que hi ha dibuixat** —el que costa una pintada és l'àrea i el mode, i no el contingut— i contesta els mil·lisegons que ha
  // costat, o zero si aquest vidre no en sap fer una de mesura. És el que `panel bench` fa servir per dir quant costa cada mode amb els números
  // d'aquest panell i no amb els d'un full de dades (docs/50, docs/95).
  virtual uint32_t paint_for_the_count(uint16_t x, uint16_t y, uint16_t w, uint16_t h, UpdateMode mode) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)mode;
    return 0;
  }
  // **Quant costarà pintar aquest rectangle amb aquesta qualitat, en mil·lisegons, sense pintar-lo** (0.190.142): the answer the kernel
  // needs before it decides whether the regions of a turn go up as one paint or as several. It is not a measurement and it must not block:
  // és el compte del vidre —la tinta del mode, que es paga sencera, més el cable dels píxels— i cada controlador el fa amb el seu model.
  // **Zero vol dir que aquest vidre no en sap res** i el nucli es queda amb la regla conservadora (docs/50, docs/95).
  virtual uint32_t paint_cost_ms(uint16_t w, uint16_t h, UpdateMode mode) const {
    // The panel's own ink time plus the wire, which is half a byte per pixel of the page at eight hundred nanoseconds a byte (docs/50).
    return ink_ms_of(mode) + (static_cast<uint32_t>(w) * static_cast<uint32_t>(h)) / 2500u;
  }
  // **Whether a partial paint may start where the change is, or has to start at the edge of the glass** (0.179.12):
  // the controller of the Paper was measured to show nothing for a rectangle that does not begin at the left edge
  // of its own image (0.118.7), and that measurement was taken while another fault was in the way —the panel was
  // never selected again after the splash (0.118.9)—, so since then the pixels between that edge and the change are
  // paid with every paint: a column of the page for one cell of a launcher. Which of the two a glass does is a
  // question for the board and not for the kernel, and `panel` at the prompt asks it (docs/30, docs/50).
  virtual bool partial_from_the_box() const { return false; }
  virtual bool set_partial_from_the_box(bool on) {
    (void)on;
    return false;
  }
  // **Com s'envien els píxels** (0.189.66): `kRows` és el que fa tota placa que no digui res, i una que en sàpiga més ho diu i
  // ho canvia. La pregunta i el canvi són aquí i no al nucli perquè és el controlador qui ho sap, i el nucli només ho mana
  // (docs/50).
  virtual FlushStyle flush_style() const { return FlushStyle::kRows; }
  virtual bool set_flush_style(FlushStyle style) {
    (void)style;
    return false;
  }
  // **El rellotge del bus, en megahertz** (0.189.77): la preparació d'una pàgina són 214 ms de cable i 99 de gir (docs/95),
  // o sigui que el cable és el que queda i l'única palanca que hi ha damunt és la velocitat de l'SPI. El panell accepta fins
  // a vint-i-quatre i el driver de referència les fa servir per a les dades; aquest projecte hi va a deu perquè mentre el
  // handshake no era clar el rellotge era una variable massa. **Ara ho és a propòsit**, i es canvia en calent per mesurar els
  // tres números en una sola sessió de placa (docs/50). Zero vol dir un vidre que no té bus, com el de l'emulador.
  virtual uint32_t bus_mhz() const { return 0; }
  // **Quant temps té la porta de la fila per moure la tinta** (0.190.57), en **nanosegons**: the source driver holds the codes of
  // a row, but the charge only reaches the ink while the gate of that line is open, o sigui que aquest número decideix en quants
  // esglaons es parteix el camí i, per tant, quants grisos separa el vidre. És el que el driver del fabricant no ha de comptar
  // perquè la seva fabricació de fila ja és curta. Zero és un vidre sense porta pròpia (docs/50, docs/95).
  // **La VCOM del panell** (0.190.60), in millivolts: the common voltage of the ink, the number that decides how much of the drive
  // actually lands across the pigment and the only electrical knob a driver can turn. Cada panell el vol diferent —el firmware de
  // fàbrica el deixa ajustar des del seu menú— i un valor heretat d'una altra unitat és una pàgina cremada (docs/50, docs/95).
  // **Quina forma d'ona porta el panell** (0.190.134, l'amo: *«pots canviar `epdiy_ED047TC2.h` i si provem la C1? Els colors a la C2 no
  // semblen correctes del tot... els 2,4 i 8 colors es veuen bé, pero els 16 no tant»*). Cada panell té la seva taula i aquesta placa en
  // porta dues de compilades: la **C2** —set temperatures i tres modes— i la **C1** —un sol rang i cinc modes, amb el monocrom i el
  // `white_to_gl16` que l'altra no té—. Es pot canviar en calent perquè la taula és el que el driver consulta en cada pintada (docs/50, docs/95).
  virtual const char* waveform() const { return "?"; }
  // **El nom de la taula que hi ha en aquest índex, o res** (0.190.135): perquè el prompt pugui llistar-les totes sense saber-les.
  // **I quants modes i quants rangs de temperatura té** (0.190.136): dues preguntes que el prompt imprimeix al costat del nom, perquè triar una
  // taula d'una altra tinta sense saber què porta és endevinar (docs/50, docs/95).
  virtual const char* waveform_at(uint8_t index, uint8_t* modes = nullptr, uint8_t* ranges = nullptr) const {
    (void)index;
    (void)modes;
    (void)ranges;
    return nullptr;
  }
  virtual bool set_waveform(const char* name) {
    (void)name;
    return false;
  }
  virtual uint16_t vcom_mv() const { return 0; }
  virtual bool set_vcom_mv(uint16_t mv) {
    (void)mv;
    return false;
  }
  virtual uint32_t gate_ns() const { return 0; }
  virtual bool set_gate_ns(uint32_t ns) {
    (void)ns;
    return false;
  }
  virtual bool set_bus_mhz(uint32_t mhz) {
    (void)mhz;
    return false;
  }
  // **How the surface is turned.** 0, 90, 180 or 270 degrees: the width and height the surface reports are
  // the ones an app draws in, and a surface that turns does the work itself when it sends what changed.
  // A surface that cannot turn says no, and the panel of this board turns in software (docs/50).
  virtual bool set_rotation(uint16_t degrees) {
    (void)degrees;
    return false;
  }
  virtual uint16_t rotation() const { return 0; }
  // Why the last thing that failed failed, in words, for the line the caller writes. Empty when it did
  // not fail. A paint that is refused without a reason is a flash to find out which of five things it
  // was (docs/50).
  virtual const char* last_error() const { return ""; }
  // **The panel goes to sleep, and this is the last thing it is asked** (0.183.0): the controller of the glass has a
  // sleep of its own —`IT8951_TCON_SLEEP` on the Paper— and the maker's own shutdown writes it **before** the rails are
  // cut, because a controller left awake on a supply that is going away keeps asking for what it no longer has. Nothing
  // may paint after it: the controller answers nothing until `SYS_RUN`, which is what a boot writes. A board that is
  // going to stop calls it, and one that is only going to sleep keeps its panel awake, because a deep sleep of this
  // board takes the supply down anyway (docs/50).
  virtual bool rest() { return false; }
};

// One finger on the glass, as the controller reports it (docs/50): where it is, how much of the
// glass it covers, and which of the fingers of this gesture it is. The number matters on a panel
// that reports five at once: it is what tells `the same finger moved` from `one finger went and
// another came` (docs/50).
struct TouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
  // The area the chip measured under the finger. It is a size and not a pressure: the controller
  // counts how much of the glass is covered, and two people pressing equally hard give two sizes
  // (docs/50).
  uint16_t size = 0;
  uint8_t id = 0;
};

// The glass of the board. The chip measures by itself and keeps its last frame until somebody takes
// it, so this is a **read and not a wait**: the kernel asks in its own turns and the answer is what
// the glass has now (docs/50, docs/30).
class ITouch {
 public:
  virtual ~ITouch() {}
  // How many fingers the glass reports at once.
  virtual uint8_t max_points() const = 0;
  // The frame the coordinates come in: what the controller was configured with, which is the panel
  // in its own portrait and not the way the board is held (docs/50).
  virtual uint16_t width() const = 0;
  virtual uint16_t height() const = 0;
  // The points that are down now, up to `capacity`; 0 when nobody is touching. A frame that is
  // taken is cleared, so a stale finger is never read twice (docs/50).
  virtual uint8_t read(TouchPoint* out, uint8_t capacity) = 0;
  // **How often this glass can have news** (0.174.4): a controller that scans the panel by itself hands over the frame
  // of its last scan and keeps it until a read takes it, so a caller that asks twice inside one scan can only be told
  // what the last read already said —and on this board that read is 400 microseconds of a hundred-kilohertz bus shared
  // with the clock, the FRAM and the air sensor (docs/50, docs/95). Zero is "this glass does not say", and then the
  // caller asks on every turn, which is what every glass did before.
  virtual uint32_t scan_ms() const { return 0; }
  // **Whether the glass warns instead of being asked** (0.188.0): a controller whose interrupt line is wired answers a
  // read that has nothing to hand over **without touching the bus**, so the caller can ask it on every turn and take a
  // frame on the turn it lands instead of up to a scan later. False is a glass that has to be asked at the rate
  // `scan_ms` says, because every ask is the status of the chip on a bus shared with the clock, the FRAM and the sensor
  // (docs/50, docs/95). **It is not called `interrupts()`** because Arduino has that name taken by a macro of its own.
  virtual bool warns() const { return false; }
  // **Quants avisos de la línia no tenien cap frame darrere** (0.189.86): a wire that falls with nobody on the glass is a
  // wait of twenty milliseconds per warning, and it is invisible in any other number. A glass with no line says nothing
  // (docs/50, docs/95).
  virtual uint32_t false_warnings() const { return 0; }
  // **Whether the frame that was just handed over is of a finger that is still there** (0.83.0). A controller that
  // scans the glass by itself hands over the frame of its last scan, and a finger that came down and went away
  // between two reads is a tap that is **already over** when the caller hears about it: `true` here would say the
  // release comes later, and a keyboard of one press per finger would swallow the next tap. A panel that cannot
  // tell says `true`, which is the safe answer —a frame is a finger until the panel says otherwise— (docs/50).
  virtual bool finger_still_down() const { return true; }
  // **Take what the chip has and let its warning line go** (M5Unified #91): a controller that keeps a frame unread
  // keeps the line it warns on asserted —and some pulse it again while nobody reads—, and a deep sleep armed on
  // "this line low" would wake at once, and again on every attempt. It is a read that hands nothing to anybody;
  // a glass with no such line answers false (docs/50).
  virtual bool flush() { return false; }
  // **I el que cal fer després que el xip hagi dormit** (0.190.74): a light sleep, the GPIO the line is on is armed as a **level** wake and left
  // that way —`gpio_ll_wakeup_enable` writes the pin's interrupt type and `gpio_wakeup_disable` does not put it back—, o sigui que la vora de
  // baixada que aquesta línia tenia deixa de ser una vora i el panell deixa de respondre (el que va deixar el tàctil del Paper mort dues
  // vegades). Aquesta crida li torna a posar la configuració que tenia: una línia que no ho necessita no fa res (docs/50, docs/95).
  virtual void rearm() {}
  // **El vidre es pot apagar** (0.190.105): a touch controller that scans its glass by itself costs about a milliamp forever, and a
  // deep sleep that is **not** listening for a finger has no reason to pay it. `sleep_now` is the chip's own sleep command and
  // `wake_up` is its own way back —the manufacturer's driver does exactly this pair (`writeCommand(0x05)` and a pulse on the line,
  // `TouchDrvGT911::sleep`/`wakeup`)—; a glass that has no such thing answers false and nothing is done (docs/50, docs/60).
  virtual bool sleep_now() { return false; }
  virtual bool wake_up() { return false; }
  // **El règim dels escombrats** (0.190.107, l'amo: *«afegeix a Settings->Input un toggle que posi "Adaptative touch scan", aquesta opciò
  // activa o desactiva el comportament del panell de baixar la sensibilitat i pujar-la quan hi ha dit. Deixant el panell a máxima velocitat
  // mentre estigui el dispositiu despert»*): cert és el vidre que baixa el ritme quan no hi ha ningú i l'apuja amb un dit; fals és el que es
  // queda al màxim mentre el sistema és despert. **Qui mana durant el son és el son** i no aquest commutador —cada mode configura el vidre
  // com el necessita—, i un vidre que no en sap no fa res (docs/30, docs/50, docs/60).
  virtual void set_adaptive_scan(bool on) { (void)on; }
  // **I el règim més baix, demanat per qui ha de dormir** (0.190.107): a deep sleep that is listening for a finger wants the glass at its
  // floor and not at the speed of a person who is awake, sigui quin sigui el commutador (docs/50, docs/60).
  virtual void quiet_now() {}
  // **El vidre amb la llum encesa** (0.190.119, l'amo: *«triga tant en respondre? sembla polling»*). Un panell que ningú no mira pot
  // escombrar-se a poc a poc; un que té la llum encesa és un panell que una persona està mirant i que **espera mig escombrat i no un de
  // sencer** per cada marc del dit. `brisk` demana el màxim de la taula (vint hertzs) i el deixa quan la llum s'apaga; i el que no en
  // sap —un vidre que no es pot configurar— no fa res (docs/30, docs/50, docs/60).
  virtual void set_brisk(bool on) { (void)on; }
};

// Where a key sits, said the same way on every board: the board is a box and a
// key is on one of its six faces, at one of the five places of that face. An app
// that wants "up" to mean one thing and "left" another has to be able to ask
// (docs/30, docs/50).
enum class Face : uint8_t { kFront = 0, kBack = 1, kLeft = 2, kRight = 3, kTop = 4, kBottom = 5 };

// The five places of a face: the middle and the four sides of it.
enum class Spot : uint8_t { kCentre = 0, kUp = 1, kDown = 2, kLeft = 3, kRight = 4 };

// How a key is built. The one button of the ATOM and the three of the Paper's
// dial are the same kind of contact, and they are not the same thing to touch: a
// simple key is one key, and a compound one is several keys of a single component,
// which is why they share a group (docs/50).
enum class KeyKind : uint8_t { kSimple = 0, kCompound = 1 };

const char* face_name(Face face);
const char* spot_name(Spot spot);
const char* kind_name(KeyKind kind);

// What a person does to this board, and what the board says back. Both are
// optional: a board without them returns nullptr and leaves the capability off
// (docs/50).
class IInput {
 public:
  virtual ~IInput() {}
  // How many keys the board has: one on the ATOM, three on the Paper. Key 0 is
  // the main one, and it is the one the system counts taps on, because three
  // taps of a side key is a menu and three of the middle one is "get out"
  // (docs/30).
  virtual uint32_t count() = 0;
  // True while that key is held. The board debounces it, so the same press
  // answers the same in every turn the kernel asks in (docs/50).
  virtual bool pressed(uint32_t key) = 0;
  // How long it has been held, in ms, or 0 when it is not held or the key does
  // not exist (docs/50).
  virtual int64_t held_ms(uint32_t key) = 0;

  // Where the key is and how it is built, so an app can give a function to a
  // position instead of to a number (docs/30).
  virtual Face face(uint32_t key) = 0;
  virtual Spot spot(uint32_t key) = 0;
  virtual KeyKind kind(uint32_t key) = 0;
  // Keys of the same physical component share a group: the three of the Paper's
  // dial are group 0, and a board with two separate buttons has two groups.
  virtual uint32_t group(uint32_t key) = 0;
};

// **Què és una llum** (0.190.106, l'amo: *«Els leds es poden declarar com a "backlight", així el sistema pot identificar quin led usar-lo per
// il·luminar la pantalla»*): a **light of the board** says things —the breath of the boot, the SOS of an error, the colour an app asks
// for— and a **backlight** lights the glass and says nothing: what it does is a level a person chooses, not a message. The system needs
// to tell them apart to know which one to leave on, i una placa ho diu al seu descriptor i no al codi del nucli (docs/30, docs/50).
enum class LedRole : uint8_t {
  kLight,
  kBacklight,
};

inline const char* led_role_name(LedRole role) {
  return (role == LedRole::kBacklight) ? "backlight" : "light";
}

// One light or a strip of them. The system paints **the whole light** with one
// colour, so the driver of a board with one LED and the driver of a board with
// twenty-five are the same interface and the policy grows nothing (docs/50).
class ILed {
 public:
  virtual ~ILed() {}
  // **El que aquesta llum és** (0.190.106): the board answers, and the default is the light that says things, which is what every board
  // before this one has (docs/30, docs/50).
  virtual LedRole role() const { return LedRole::kLight; }
  // How many lights the board has: one on the ATOM Lite, twenty-five on the Matrix.
  virtual uint32_t count() = 0;
  // The highest a channel may be driven at. It is a hardware limit and not a
  // taste: the panel of the Matrix heats up above the maker's number (docs/50),
  // and the system does not let an app go past it. 255 on a light that has no
  // such limit.
  virtual uint32_t max_value() = 0;
  // Paints every light with one colour. False when the board cannot.
  virtual bool fill(uint8_t r, uint8_t g, uint8_t b) = 0;
  // One light, for a board that has more than one. False when there is no such
  // index.
  virtual bool pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) = 0;
  // Sends what has been painted: one frame is one write on the wire (docs/75).
  virtual void show() = 0;
};

// The inertial sensor: which way is down and how fast the board is turning. It is
// not on every board —the ATOM Lite has none, the Matrix has a MPU6886— and the one
// that lacks it returns nullptr and leaves the capability off (docs/50).
class IImu {
 public:
  virtual ~IImu() {}
  // Acceleration in g on the three axes, turn rate in degrees per second, and the
  // temperature the chip measures in degrees Celsius (it is in the same burst of
  // registers, so it costs nothing). False when the sensor did not answer.
  virtual bool read(float& ax, float& ay, float& az, float& gx, float& gy, float& gz,
                    float& celsius) = 0;
};

// The **extra** memory of the board —the PSRAM of the M5Paper— with the three numbers that are not
// the same and that people confuse (docs/75):
//
//   - `chip_bytes` is what the chip says it has, which is what a datasheet prints;
//   - `total_bytes` is what the CPU can **address**, which is what the system can hand out;
//   - `free_bytes` is what is left of that.
//
// On this family the first and the second do not match: the ESP32's window for external RAM is
// **4 MB**, so a board that carries 8 MB keeps four of them behind the bank switcher of the chip,
// and memory that moves under a pointer cannot be given to an app (docs/50, docs/75). All zero on a
// board with no extra memory, which is the truth about the hardware and not an error.
//
// **I darrere el banc de commutació hi ha els altres quatre** (0.190.173): `himem_bytes` és el que el
// xip por fer servir pel `himem`, `himem_free_bytes` el que ningú no ha pres i `himem_window_bytes`
// l'espai d'adreces que el banc té per mapejar-hi blocs —`CONFIG_SPIRAM_BANKSWITCH_RESERVE` pàgines de
// 32 KB—. Zero als tres vol dir que aquest xip no té banc (l'ESP32-S3 no en té) o que no hi ha PSRAM,
// que és la mateixa resposta per a qui ho pregunta (docs/50, docs/75).
struct ExtraMemory {
  uint32_t chip_bytes;
  uint32_t total_bytes;
  uint32_t free_bytes;
  uint32_t himem_bytes;
  uint32_t himem_free_bytes;
  uint32_t himem_window_bytes;

  // **Un `ExtraMemory out;` que omple només els tres primers ha de dir zero a la resta**: the HALs that
  // have no bank switcher ask for a reading and fill in what they have, and none of them has to
  // remember every field that another board added (docs/50).
  ExtraMemory()
      : chip_bytes(0),
        total_bytes(0),
        free_bytes(0),
        himem_bytes(0),
        himem_free_bytes(0),
        himem_window_bytes(0) {}
};

// **32 KB és el bloc del banc de commutació** (el `ESP_HIMEM_BLKSZ` de l'ESP32): la unitat més petita que el xip pot
// mapejar-hi i, per tant, la de qualsevol adreça que hi passi. Viu aquí i no a la capçalera de l'ESP32 perquè el nucli
// —que compila a l'emulador— ha de saber de quina mida és el bloc que demana provar (docs/50, docs/75).
const uint32_t kExtraMemoryBlockBytes = 0x8000;

// **La memòria que hi ha darrere el commutador de bancs del xip, com una regió que es llegeix i s'escriu per trossos**
// (0.190.174). The chip can only have a window of it mapped at a time, so there is **no pointer to give**: the door takes
// an offset and copies. That is why it is not `pal::IExtraMemory` —a heap promises a pointer that stays put— and why the
// session memory, which was built around offsets from the first day (`block:read(offset, n)`), is what lives here: a cache
// of a launcher does not need to be one piece, it needs to be able to give a piece at a time (docs/50, docs/75).
class IBankedMemory {
 public:
  virtual ~IBankedMemory() {}
  virtual uint32_t size() const = 0;
  // **Els bytes que s'han copiat**, que són menys que `bytes` quan la regió s'acaba: a read that runs past the end is a
  // read that answers what there was and not an error (docs/30).
  virtual uint32_t read(uint32_t offset, void* out, uint32_t bytes) = 0;
  virtual uint32_t write(uint32_t offset, const void* in, uint32_t bytes) = 0;
};

// The air around the board: how warm it is and how wet. It is not on every board —the Paper has an
// SHT30 and the ATOMs have nothing— and a board without one returns nullptr from `sensor()` and
// leaves the capability off (docs/50).
//
// **It is a reading and not a policy**: the chip measures when it is asked and holds no state, so
// there is nothing to switch on, nothing to keep warm and nothing to turn off afterwards. The one
// thing the driver has to say is how long asking costs, and it says it in docs/50.
class ISensor {
 public:
  virtual ~ISensor() {}
  // The temperature in degrees Celsius and the relative humidity in percent. False when the chip
  // did not answer, or answered with a crossed CRC, which is the same thing to an app: no number is
  // better than a wrong one (docs/30).
  virtual bool read(float& celsius, float& humidity) = 0;
};

class IPlatform {
 public:
  virtual ~IPlatform() {}
  // The board this build runs on, by the name the board has in platformio.ini
  // (`board = ...`) — or the name of the board of its family that differs in what the
  // app can touch (`m5stack-atom-matrix`). The PC build answers the model it was told
  // to stand in for: it is never "emulator", which is not a board anybody can flash
  // (docs/50, docs/85).
  virtual const char* id() const = 0;
  // The chip family this board belongs to ("esp32"). It is what an app means when
  // its manifest says it runs on "esp32": every board of the family accepts it
  // (docs/40). The emulator is not a chip of its own and answers the family of the model
  // it is, because that is what it is imitating.
  virtual const char* family() const = 0;
  // True only in the PC build: no real clock, no real flash, no real port. It is what
  // tells an app that the board under `id()` is a fake.
  virtual bool emulator() const = 0;
  // The board names this band answers to, which is what an app's `boards` list is
  // matched against. A board answers with its own `id()` and nothing else, and the emulator
  // does the same with the model it was told to be: one name, so an app is accepted or
  // refused on the PC exactly as it would be on the glass (docs/40, docs/85).
  virtual uint32_t board_count() const { return 1; }
  virtual const char* board_name(uint32_t index) const {
    return (index == 0) ? id() : nullptr;
  }
  // **The supply of the board is held from here and as early as the boot can** (0.189.24): a board that latches its own power
  // —the Paper does, with a pin and a switch that gives up about a second after the key— dies in the middle of a boot that
  // takes longer than that to reach the platform, and a cable hides it because the cable is what is feeding it. The kernel
  // calls this **before anything else**, and a board whose power does not depend on a pin does nothing (docs/50).
  virtual void hold_the_power() {}
  // **I per què s'ha reiniciat el xip** (0.190.68): el que respon l'ESP32 (`esp_reset_reason`) quan hi ha maquinari, i el que el model
  // de l'emulador diu quan no n'hi ha. La base contesta «el corrent ha tornat», que és la veritat d'una placa que no ho sap dir.
  virtual ResetCause reset_cause() const { return ResetCause::kPowerOn; }
  // **Si una pintada és feina o és espera, i ho diu la placa** (0.190.39). A glass with a controller paints from its own
  // memory: the chip hands the rectangle over and waits for ink it cannot hurry, o sigui que el pas de baix del processador és
  // el que li toca. A glass **without** a controller is the opposite: the chip is the driver —it turns the frame into the
  // panel's codes row by row and feeds the peripheral— and the same full page measured on the T5 E-Paper S3 Pro costs **2,67 s
  // at 80 MHz and 1,2 s at 240**. **La pregunta és de la placa i no del vidre ni del driver**: dues plaques poden portar el
  // mateix panell i el mateix driver i no costar el mateix —el xip, el bus, el perifèric i la memòria són de la placa—, i per
  // això el que decideix la velocitat és qui la paga (docs/50, docs/95). False by default: totes les plaques anteriors tenen
  // controlador i no canvien de comportament.
  virtual bool a_paint_is_work() const { return false; }
  virtual const Caps& caps() const = 0;
  virtual uint32_t heap_limit_kb() const = 0;
  // The extra memory of the board, as the board sees it (docs/75). The interpreter puts its big
  // blocks there and an app is told what is left; it is the board that answers, so the emulator answers
  // what the model carries (docs/85).
  virtual ExtraMemory extra_memory() const { return ExtraMemory(); }
  // **Una prova i no una política** (0.190.173): escriu i torna a llegir `bytes` de la memòria que no és
  // adreçable —els quatre megabytes de darrere el banc de commutació del xip— i diu si el que torna és el
  // que hi va anar. És el mateix que fa l'exemple `system/himem` de la IDF. `false` vol dir «aquest xip no
  // en té» o «no n'hi ha prou», que per a qui ho pregunta és la mateixa resposta, i per això la placa que
  // no en té no ha de fer res: el valor d'aquí ja és el seu (docs/50, docs/75).
  virtual bool extra_memory_round_trip(uint32_t bytes) {
    (void)bytes;
    return false;
  }
  // **Una regió del banc, comprada amb una mida i tornada en acabar** (0.190.174): `nullptr` vol dir que aquest xip no té
  // banc de commutació o que no hi ha prou memòria lliure, i el qui ho demana es queda on era. El que torna és del HAL i no
  // s'allibera: es torna amb `close_banked_memory`, que és el que fa que una regió no es quedi sense amo (docs/50, docs/75).
  virtual IBankedMemory* open_banked_memory(uint32_t bytes) {
    (void)bytes;
    return nullptr;
  }
  virtual void close_banked_memory(IBankedMemory* block) { (void)block; }
  virtual IClock* clock() = 0;
  virtual IStorage* storage(const char* mount) = 0;
  virtual IKeyValue* nvs() = 0;
  virtual INet* net() = 0;
  // **I la ràdio que no és una xarxa** (0.190.179): the keyboard of Bluetooth, which is null on every board that does not have the
  // radio in its build —the emulator, and the bands that have not enabled it— (docs/50).
  virtual IBluetooth* bluetooth() { return nullptr; }
  // **I el mesh, que és una xarxa i no un teclat** (0.190.212): null on every board whose build does not carry it.
  virtual IMesh* mesh() { return nullptr; }
  // **I la ràdio que només escolta** (0.192.49): el SX1262 de la T5S3, que no és cap xarxa i no té cap manera de
  // transmetre des d'aquí. Null a tot arreu on no hi ha el xip, que és el que una pàgina ha de poder saber abans de
  // dibuixar un espectre (docs/30, docs/50).
  virtual ILora* lora() { return nullptr; }
  // **I el receptor del cel, quan la placa en porta un** (0.192.63): `nullptr` és el que ha de dir una placa sense mòdul, i el
  // que fa que `kCapGps` no s'encengui (docs/50).
  virtual IGps* gps() { return nullptr; }
  virtual IPower* power() = 0;
  virtual ISurface* display() = 0;
  virtual ILed* led() = 0;
  virtual IInput* input() = 0;

  // **Si les tecles d'aquesta placa són per a la pàgina** (0.190.103, l'amo: *«en aquesta placa només tenim un botó físic real, no serveix
  // per el focus. En aquesta placa no hi ha opció a focus per botons»*): a dial of three keys walks the ring and the middle one presses
  // what has it, i això demana un joc de tecles pensat per a això. **Certa per defecte**, perquè és el que fa el Paper des del primer dia;
  // una placa amb una tecla de debò —o amb el BOOT del xip— diu que no, i aleshores cada pulsació va a l'app com anava abans que hi
  // hagués un anell: la promesa de `button.wait`, la cua de qui l'hagi presa o l'`on_button` (docs/30, docs/50).
  virtual bool keys_are_for_the_page() const { return true; }

  // **Si la caixa té un botó de sota el vidre** (0.190.110, l'amo: *«sota el panell hi ha un cercle que sembla ser un botó tàctil, i es fals. No
  // es un botó, però es tant petit que quant el polses, amb la punta del dit toques el panell tàctil»*): some cases draw a circle under the screen
  // that is not a button and is pressed through the glass, i això és una propietat de la caixa i no del tàctil —la Paper té el mateix vidre i
  // **no** té cap cercle allà—. Fals per defecte: una placa ho diu i el sistema li fa la tecla (docs/30, docs/50).
  virtual bool a_button_under_the_glass() const { return false; }
  virtual IImu* imu() = 0;
  // The ambient sensor, when the board has one. The default is nullptr because that is what a
  // board without it has to say, and a board that has it overrides this (docs/50). `const` because
  // the capabilities ask it too, and a board is not changed by being asked what it has.
  virtual ISensor* sensor() const { return nullptr; }
  // The glass of the board, or nullptr when it has none (docs/50). It is not pure for the same
  // reason as `sensor()`, and it is **not announced** as a capability yet: the chip answers and its
  // frame is known, but a name an app can ask for has to come with the call that uses it, and a
  // touch means nothing without something drawn under it — the panel (docs/90).
  virtual ITouch* touch() const { return nullptr; }
  // **What one instruction of the interpreter costs when this band has to simulate a processor it is
  // not running on**, in nanoseconds: zero on a board, where the time is the real one, and a number on
  // an emulator that is paying for it, scaled by `power()->cpu_mhz()` (docs/85).
  virtual uint32_t simulated_instruction_ns() { return 0; }
};

// Implemented once per board library (hal_atom, hal_emulator).
IPlatform& platform();

}  // namespace hal

// What a battery voltage means: the curve that turns millivolts into a percentage. It is
// here and not in a board because it is about a cell and not about a chip (docs/50).
#include "hal/battery.h"

// Dates and epochs: the maths a clock chip needs, and the same reason to live here (docs/50).
#include "hal/civil.h"
#include "hal/alarm.h"

#endif  // LUNOK_HAL_HAL_H
