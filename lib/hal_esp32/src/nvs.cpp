// The key-value store of the ESP32, over the `nvs` partition.
//
// A namespace is an NVS namespace and a key is a key; the value is opaque bytes on
// both sides, like the emulator's one-file-per-key store. Nothing here knows what is
// being saved (docs/70).
#include <esp_idf_version.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdlib.h>
#include <string.h>

#include "hal/log.h"

#include "esp32/nvs.h"

namespace hal {
namespace {

// The data partition in the partition table.
const char kPartition[] = "nvs";

// A namespace and a key have to be plain names: they end up in the NVS index, and
// the point of refusing them here is that a typo is caught where it is made.
bool plain_name(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  for (const char* p = name; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\' || *p == '.') return false;
  }
  return true;
}

// A copy that cannot be truncated silently: an NVS key is short by design, and one
// that does not fit is not a key of ours (docs/70).
bool copy_name(char* dst, size_t cap, const char* src) {
  size_t i = 0;
  for (; src[i] != '\0'; ++i) {
    if (i + 1 >= cap) return false;
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return true;
}

// **Les escriptures s'aguanten a la memòria fins que la placa baixa** (0.190.109, l'amo: *«Usa el flush (forçar escriptura) de l'nvs només quan
// el sistema s'apagui, vaig a deepsleep o reinicii. Guarda els settings el major temps possible sense comitejar, degut a que l'nvs està a la
// flash i la flash te un número d'escriptures molt limitat, comptant que l'nvs son blocs de 4k, per guardar un sol byte... S'ha de procurar fer
// perdurar el màxim possible»*). La botiga del xip escriu una entrada a la flash a cada `nvs_set_blob` i una pàgina sencera quan la que té
// s'omple, o sigui que un comptador que canvia cada minut és una pàgina cada pocs dies; aquí el que canvia s'apunta **a la RAM** i el que ho
// escriu és `commit()`, que el nucli ja crida a `settle_before_leaving` —la baixada, l'apagada i el reinici— (docs/70, docs/75).
//
// Un `get` llegeix el que hi ha pendent i, si no n'hi ha, la botiga: el que una persona escriu es llegeix de seguida (`cfg keyboard x` i tot
// seguit `cfg keyboard`) i un `erase` pendent és una clau que ja no hi és (docs/30, docs/70).
struct Pending {
  char ns[16];
  char key[16];
  uint8_t* bytes;  // nullptr vol dir una clau que s'ha d'esborrar
  size_t len;
};

// **La botiga s'ha d'encendre, i en un build d'ESP-IDF no ho fa ningú** (0.190.117, l'amo: *«[W][NVS] 1 of the 1 change(s) did not reach the
// flash: session/wake_at»*). `nvs_flash_init()` és el que llegeix la partició, la reconstrueix si li cal i deixa el `nvs_open` engegat; el nucli
// d'Arduino la crida a l'arrencada i per això tot això funcionava amb `framework = arduino`. **En aquest build no la cridava ningú**, o sigui que
// cada `nvs_open` tornava `ESP_ERR_NVS_NOT_INITIALIZED` i **no s'hi ha escrit mai res**: el `0 setting(s)` de l'arrencada, el `cannot write the
// queue to nvs` de la primera línia que em vas ensenyar i el `session/wake_at` d'ara són el mateix forat, vist tres vegades (docs/50, docs/70).
//
// L'error que la IDF documenta —una partició sense pàgines lliures o amb una versió que no és la d'aquest build— es resol **esborrant-la**, que és el
// que fa tothom i el que aquí es diu en veu alta: les claus es poden tornar a escriure i el que hi ha és un ajust, no una dada (docs/70).
bool ensure_the_store() {
  static int8_t state = -1;  // -1 no s'ha provat, 0 no, 1 sí
  if (state >= 0) return state == 1;
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    logf(pal::kLogWarn, "NVS", "the store did not come up (%s); it is erased and made again", esp_err_to_name(err));
    if (nvs_flash_erase() != ESP_OK) {
      state = 0;
      return false;
    }
    err = nvs_flash_init();
  }
  state = (err == ESP_OK) ? 1 : 0;
  if (state == 0) logf(pal::kLogWarn, "NVS", "the store did not come up: %s", esp_err_to_name(err));
  return state == 1;
}

class Esp32Nvs : public IKeyValue {
 public:
  bool get(const char* ns, const char* key, void* buf, size_t cap, size_t& len) override {
    if (buf == nullptr || cap == 0 || !plain_name(ns) || !plain_name(key)) return false;
    if (!ensure_the_store()) return false;

    // **El que encara no s'ha escrit és el que mana** (0.190.109): a value that is waiting is the value, and a key waiting to be erased is a
    // key that is gone (docs/70).
    const int waiting = waiting_at(ns, key);
    if (waiting >= 0) {
      const Pending& row = pending_[waiting];
      if (row.bytes == nullptr || row.len > cap) return false;
      memcpy(buf, row.bytes, row.len);
      len = row.len;
      return true;
    }

    // The IDF's own open and not Arduino's `Preferences`: Preferences logs
    // `nvs_open failed: NOT_FOUND` when the namespace does not exist yet, and a board
    // that has never saved anything is not an error — it printed two of those lines
    // on every `provision` (measured on the board, 31/10).
    nvs_handle_t handle = 0;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) return false;
    // Only a blob is a value here: a key of another type belongs to the IDF or to
    // somebody else (the `phy` namespace keeps integers), and `nvs_get_blob` says
    // TYPE_MISMATCH and logs nothing (docs/70).
    size_t stored = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &stored);
    if (err != ESP_OK || stored == 0 || stored > cap) {
      nvs_close(handle);
      return false;
    }
    err = nvs_get_blob(handle, key, buf, &stored);
    nvs_close(handle);
    if (err != ESP_OK) return false;
    len = stored;
    return true;
  }

  bool set(const char* ns, const char* key, const void* buf, size_t len) override {
    if (buf == nullptr || len == 0 || !plain_name(ns) || !plain_name(key)) return false;
    if (!ensure_the_store()) return false;
    // **I s'apunta, no s'escriu** (0.190.109): the bytes are copied out of the caller's buffer —it is a stack of the turn and it will be gone—
    // and they go to the flash at `commit`, which is the way down and not the turn (docs/70, docs/75).
    return remember(ns, key, static_cast<const uint8_t*>(buf), len);
  }

  bool set_now(const char* ns, const char* key, const void* buf, size_t len) {
    if (buf == nullptr || len == 0 || !plain_name(ns) || !plain_name(key)) return false;

    // **El camí de l'IDF i no el d'Arduino** (0.190.90): `Preferences` és un embolcall de tres crides d'aquest mateix IDF i, a més,
    // escriu una línia quan l'espai de noms no existeix. Aquí es fa la crida directa, que és la mateixa que fa servir la lectura de
    // sobre i la que val per als dos entorns —el d'Arduino i el d'ESP-IDF— (docs/50, docs/70).
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(ns, NVS_READWRITE, &handle);
    if (opened != ESP_OK) {
      // **I per què no s'obre, dit** (0.190.117): abans aquesta porta tornava `false` i callava, i una botiga que no s'obre i una que diu que no
      // eren el mateix silenci (docs/70, docs/95).
      logf(pal::kLogWarn, "NVS", "the space %s did not open (%s); %s/%s is not written", ns, esp_err_to_name(opened), ns, key);
      return false;
    }
    const esp_err_t put = nvs_set_blob(handle, key, buf, len);
    if (put == ESP_OK) (void)nvs_commit(handle);
    nvs_close(handle);
    // **I quan la flash diu que no, es diu per què** (0.190.109): fins ara aquesta porta tornava `false` i el nucli només podia dir «cannot
    // write the queue»; el codi de la IDF és el que distingeix una partició plena (`ESP_ERR_NVS_NOT_ENOUGH_SPACE`) d'un valor massa gran o
    // d'un espai de noms que no s'obre, i és el que una persona necessita llegir (docs/70, docs/95).
    if (put != ESP_OK) {
      logf(pal::kLogWarn, "NVS", "the flash did not take %s/%s (%u B): %s", ns, key, static_cast<unsigned>(len),
           esp_err_to_name(put));
    }
    return put == ESP_OK;
  }

  bool erase(const char* ns, const char* key) override {
    if (!plain_name(ns) || !plain_name(key)) return false;
    if (!ensure_the_store()) return false;
    // **Esborrar també s'apunta** (0.190.109): a key that is going does not have to cost a page to say so, i el `commit` ho fa (docs/70).
    return remember(ns, key, nullptr, 0);
  }

  bool erase_now(const char* ns, const char* key) {
    if (!plain_name(ns) || !plain_name(key)) return false;

    // **I esborrar el que no hi és no és un error** (0.190.90): la mateixa lliçó que a `get`, sense l'embolcall d'Arduino pel mig.
    nvs_handle_t handle = 0;
    if (nvs_open(ns, NVS_READWRITE, &handle) != ESP_OK) return false;
    // **I el «no hi era» tampoc** (0.191.11, trobat al registre de l'amo): `nvs_erase_key` torna `ESP_ERR_NVS_NOT_FOUND` per a una clau
    // que no existeix, i aquesta porta ho comptava com un fracàs —o sigui que una baixada que esborrava `session/detached` quan no s'havia
    // escrit mai deia `1 of the 4 change(s) did not reach the flash` i una placa perfectament desada quedava amb un avís (docs/70, docs/75).
    const esp_err_t gone = nvs_erase_key(handle, key);
    const bool removed = (gone == ESP_OK || gone == ESP_ERR_NVS_NOT_FOUND);
    if (gone == ESP_OK) (void)nvs_commit(handle);
    nvs_close(handle);
    return removed;
  }

  // Iteration goes through the IDF's own iterator: Arduino's Preferences has no way
  // to walk a namespace. In this IDF the iterator is the return value of
  // nvs_entry_find and nvs_entry_next, and it is released when it runs out.
  // **El cercador de claus té dues formes i el sistema en té dues** (0.190.90): aquest IDF —el 4.4— engega el cercador amb
  // `nvs_entry_find(partició, espai, tipus, &iterador)` i diu el resultat en un `esp_err_t`, mentre que el que porta el nucli
  // d'Arduino el 5 fa el mateix i torna l'iterador. Dues línies aquí i la resta de la casa no sap res d'aquesta diferència
  // (docs/50, docs/70).
  nvs_iterator_t find(const char* ns) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    nvs_iterator_t it = nullptr;
    return (nvs_entry_find(kPartition, ns, NVS_TYPE_ANY, &it) == ESP_OK) ? it : nullptr;
#else
    return nvs_entry_find(kPartition, ns, NVS_TYPE_ANY);
#endif
  }

  nvs_iterator_t step(nvs_iterator_t it) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    return (nvs_entry_next(&it) == ESP_OK) ? it : nullptr;
#else
    return nvs_entry_next(it);
#endif
  }

  bool next(const char* ns, const char*& key, bool& first) override {
    if (!plain_name(ns)) return false;
    if (!ensure_the_store()) return false;

    if (first) {
      release();
      first = false;
      iterator_ = find(ns);
    } else if (iterator_ != nullptr) {
      iterator_ = step(iterator_);
    }

    while (iterator_ != nullptr) {
      nvs_entry_info_t info;
      nvs_entry_info(iterator_, &info);  // void in this IDF
      if (copy_name(key_buf_, sizeof(key_buf_), info.key)) {
        key = key_buf_;
        return true;
      }
      iterator_ = step(iterator_);  // a key that does not fit is skipped
    }
    release();
    return false;
  }

  // The namespaces themselves: the same iterator, asked for with a null namespace
  // name, which hands back one entry per key. The names are remembered so a
  // namespace with three keys is listed once (docs/70).
  bool next_namespace(const char*& ns, bool& first) override {
    if (!ensure_the_store()) return false;
    if (first) {
      release();
      first = false;
      seen_count_ = 0;
      iterator_ = find(nullptr);
    } else if (iterator_ != nullptr) {
      iterator_ = step(iterator_);
    }

    while (iterator_ != nullptr) {
      nvs_entry_info_t info;
      nvs_entry_info(iterator_, &info);
      if (copy_name(key_buf_, sizeof(key_buf_), info.namespace_name) &&
          !already_seen(key_buf_)) {
        remember(key_buf_);
        ns = key_buf_;
        return true;
      }
      iterator_ = step(iterator_);
    }
    release();
    return false;
  }

  // **I el que les escriu totes**, que és el que el nucli crida quan la placa baixa (0.189.18, i ara és on van a parar les que s'aguantaven:
  // 0.190.109). A pending row that the flash refuses is said and dropped: keeping it would be asking the same page again at the next
  // commit, i el que s'ha perdut és un ajust i no la placa (docs/70, docs/75).
  bool commit() override {
    if (pending_ == nullptr || pending_count_ == 0) return true;
    if (!ensure_the_store()) return false;
    // **El que s'ha escrit, dit amb les seves claus** (0.190.111): the way down is the one moment the flash is touched, o sigui que és el moment
    // de dir quantes claus hi han anat i quines —una llista que cap al final del run és l'única manera de saber si un ajust va arribar-hi—.
    char names[kWaitingMost * 20 + 1] = {0};
    size_t used = 0;
    for (uint8_t i = 0; i < pending_count_; ++i) {
      const int wrote = snprintf(names + used, sizeof(names) - used, "%s%s/%s", (i == 0) ? "" : " ", pending_[i].ns,
                                 pending_[i].key);
      if (wrote <= 0 || static_cast<size_t>(wrote) >= sizeof(names) - used) break;
      used += static_cast<size_t>(wrote);
    }
    const uint8_t how_many = pending_count_;
    bool all = true;
    uint8_t did_not = 0;
    for (uint8_t i = 0; i < pending_count_;) {
      Pending& row = pending_[i];
      const bool taken = (row.bytes != nullptr) ? set_now(row.ns, row.key, row.bytes, row.len)
                                                : erase_now(row.ns, row.key);
      if (!taken) {
        all = false;
        ++did_not;
        ++refused_;
      }
      free(row.bytes);
      row.bytes = nullptr;
      row.len = 0;
      row.ns[0] = '\0';
      row.key[0] = '\0';
      ++i;
    }
    pending_count_ = 0;
    if (all) {
      logf(pal::kLogInfo, "NVS", "%u change(s) to the flash: %s", static_cast<unsigned>(how_many), names);
    } else {
      logf(pal::kLogWarn, "NVS", "%u of the %u change(s) did not reach the flash: %s", static_cast<unsigned>(did_not),
           static_cast<unsigned>(how_many), names);
    }
    return all;
  }

  // What the partition holds: used entries of total entries. A full store answers
  // ESP_ERR_NVS_NOT_ENOUGH_SPACE to the next save, and this is how a person sees it
  // coming (docs/70).
  bool stats(uint32_t& used, uint32_t& total) override {
    if (!ensure_the_store()) return false;
    nvs_stats_t info;
    memset(&info, 0, sizeof(info));
    if (nvs_get_stats(nullptr, &info) != ESP_OK) return false;  // nullptr: the default partition
    used = info.used_entries;
    total = info.total_entries;
    return true;
  }

 private:
  bool already_seen(const char* name) const {
    for (uint32_t i = 0; i < seen_count_; ++i) {
      if (strcmp(seen_[i], name) == 0) return true;
    }
    return false;
  }

  void remember(const char* name) {
    if (seen_count_ >= kSeenMax) return;
    copy_name(seen_[seen_count_], sizeof(seen_[0]), name);
    ++seen_count_;
  }

  // **Una fila per clau que espera** (0.190.109): twelve is more than the system writes between two ways down —the settings of `cfg`, the
  // alarms and the network are three of them— i una taula plena no perd res: el que fa és escriure el que té i tornar a començar (docs/75).
  static const uint8_t kWaitingMost = 12;

  int waiting_at(const char* ns, const char* key) const {
    if (pending_ == nullptr) return -1;
    for (uint8_t i = 0; i < pending_count_; ++i) {
      if (strcmp(pending_[i].ns, ns) == 0 && strcmp(pending_[i].key, key) == 0) return static_cast<int>(i);
    }
    return -1;
  }

  bool remember(const char* ns, const char* key, const uint8_t* bytes, size_t len) {
    if (the_waiting_list() == nullptr) return false;
    uint8_t* copy = nullptr;
    if (bytes != nullptr) {
      copy = static_cast<uint8_t*>(malloc(len));
      if (copy == nullptr) return false;
      memcpy(copy, bytes, len);
    }
    const int waiting = waiting_at(ns, key);
    if (waiting >= 0) {
      Pending& row = pending_[waiting];
      free(row.bytes);
      row.bytes = copy;
      row.len = len;
      return true;
    }
    if (pending_count_ >= kWaitingMost) {
      // **La taula plena escriu el que té i torna a començar**: losing the oldest setting to keep a new one would be the wrong trade, i
      // el que es paga és una pàgina de la flash una vegada i no a cada torn (docs/75).
      commit();
    }
    if (pending_count_ >= kWaitingMost) {
      free(copy);
      return false;
    }
    Pending& row = pending_[pending_count_++];
    copy_name(row.ns, sizeof(row.ns), ns);
    copy_name(row.key, sizeof(row.key), key);
    row.bytes = copy;
    row.len = len;
    return true;
  }

  // **La taula de les que esperen, al munt i no al BSS** (0.190.109): dotze files són quatre-cents vuitanta bytes, i el Paper —que juga amb
  // els últims centenars de bytes de la seva DRAM— va deixar de linkar per dos-cents vuitanta. O sigui que la taula es demana al primer
  // valor que s'apunta i no abans, i una placa que no escriu res no la paga (docs/70, docs/75).
  Pending* pending_ = nullptr;
  uint8_t pending_count_ = 0;

  Pending* the_waiting_list() {
    if (pending_ == nullptr) {
      pending_ = static_cast<Pending*>(calloc(kWaitingMost, sizeof(Pending)));
    }
    return pending_;
  }
  // **Quantes vegades la flash ha dit que no** (0.190.109): el número que diu si el disc de les claus és ple, i que el registre ensenya quan
  // passa en comptes de callar-ho com fins ara (docs/70, docs/95).
  uint32_t refused_ = 0;

  static const uint32_t kSeenMax = 4;  // more than the system uses, by design
  char seen_[kSeenMax][16] = {{0}};
  uint32_t seen_count_ = 0;
  void release() {
    if (iterator_ != nullptr) {
      nvs_release_iterator(iterator_);
      iterator_ = nullptr;
    }
  }

  nvs_iterator_t iterator_ = nullptr;
  char key_buf_[32] = {0};
};

Esp32Nvs g_nvs;

}  // namespace

IKeyValue* esp32_nvs() { return &g_nvs; }

}  // namespace hal
