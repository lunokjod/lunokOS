// The memory of the chip behind its bank switcher: what there is of it, and a proof that it holds what
// is written (docs/50, docs/75). See `esp32/extra_memory.h` for why it is not a heap.
#include "esp32/extra_memory.h"

#include <string.h>

// **El banc de commutació només hi és als xips que en tenen** (0.190.173): `esp_himem_*` is compiled by
// the PSRAM component when `CONFIG_SPIRAM_BANKSWITCH_ENABLE` is on, and the S3, the ATOM and the DevKit
// would ask the linker for symbols that this build does not have (docs/50).
#include <sdkconfig.h>
// **La mida del xip es pregunta a la PSRAM i no al heap** (0.190.173): `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`
// answers what the CPU can address —four megabytes— and the chip has eight. It is the number the datasheet, `mem` and
// docs/75 have always printed (docs/50).
#include <esp_psram.h>
#if CONFIG_SPIRAM_BANKSWITCH_ENABLE
#define LUNOK_HAS_HIMEM 1
#include <esp32/himem.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace hal {
namespace {

#ifdef LUNOK_HAS_HIMEM
// A pattern that depends on where it is written, so a block that comes back shifted is a block that
// does not match: the same thing the IDF's own example checks.
uint32_t pattern_at(uint32_t offset, uint32_t index) { return 0xa5a5a5a5u ^ (offset + index); }
#endif

}  // namespace

uint32_t esp32_chip_psram_bytes() {
#if CONFIG_SPIRAM
  return static_cast<uint32_t>(esp_psram_get_size());
#else
  return 0;
#endif
}

ExtraMemory esp32_extra_memory() {
  ExtraMemory out;
#ifdef LUNOK_HAS_HIMEM
  // **Els primers blocs de l'himem són els de la finestra i no es compten** (0.190.173, mesurat a la placa el 21/11):
  // `esp_himem_get_phys_size()` diu **4 352 KB** —els 4 MB de dalt i les vuit pàgines de 32 KB que el banc necessita per
  // mapejar-hi— i el heap diu que en té **4 096**. Sumats fan 8 448 i el xip en té 8 192: les 256 KB de la finestra es
  // compten dos cops. La IDF calcula `paddr_start = 4 MB − finestra` i descompta la finestra del heap a `esp_psram.c`,
  // però la dada del heap diu que en aquest build no s'hi ha descomptat, i **una escriptura al primer bloc de l'himem
  // cauria dins del que el heap creu seu**. O sigui que el que aquest sistema en dona i en fa servir és el que queda
  // després de la finestra, que és exactament els quatre megabytes de dalt de tot (docs/50, docs/75).
  const uint32_t window = static_cast<uint32_t>(esp_himem_reserved_area_size());
  const uint32_t phys = static_cast<uint32_t>(esp_himem_get_phys_size());
  const uint32_t free_bytes = static_cast<uint32_t>(esp_himem_get_free_size());
  out.himem_bytes = (phys > window) ? phys - window : 0;
  out.himem_free_bytes = (free_bytes > window) ? free_bytes - window : 0;
  out.himem_window_bytes = window;
#endif
  return out;
}

#ifdef LUNOK_HAS_HIMEM
namespace {

// **Una regió del banc, una sola vegada** (0.190.174): the chip has one window of `SPIRAM_BANKSWITCH_RESERVE` pages and this
// class is the whole of it. The region is asked for a size, the first `esp_himem_reserved_area_size()` bytes of the handle
// are never touched —they are the blocks the heap of this build still believes are its own— and every access maps the one
// 32 KB block it lands in and unmaps the one before, because two blocks cannot be mapped at the same time (docs/50, docs/75).
class BankedRegion : public IBankedMemory {
 public:
  bool open(uint32_t bytes) {
    if (is_open_ || bytes == 0) return false;
    const uint32_t skip = static_cast<uint32_t>(esp_himem_reserved_area_size());
    if (skip + bytes > esp_himem_get_free_size()) return false;
    if (esp_himem_alloc(skip + bytes, &handle_) != ESP_OK) return false;
    if (esp_himem_alloc_map_range(kExtraMemoryBlockBytes, &window_) != ESP_OK) {
      esp_himem_free(handle_);
      handle_ = nullptr;
      return false;
    }
    skip_ = skip;
    size_ = bytes;
    mapped_block_ = kNoBlock;
    mapped_ = nullptr;
    is_open_ = true;
    return true;
  }

  void close() {
    if (!is_open_) return;
    unmap();
    esp_himem_free_map_range(window_);
    esp_himem_free(handle_);
    window_ = nullptr;
    handle_ = nullptr;
    size_ = 0;
    skip_ = 0;
    is_open_ = false;
  }

  uint32_t size() const override { return size_; }

  uint32_t read(uint32_t offset, void* out, uint32_t bytes) override {
    return walk(offset, bytes, static_cast<uint8_t*>(out), true);
  }

  uint32_t write(uint32_t offset, const void* in, uint32_t bytes) override {
    return walk(offset, bytes, const_cast<uint8_t*>(static_cast<const uint8_t*>(in)), false);
  }

 private:
  static const uint32_t kNoBlock = 0xffffffffu;

  // One loop for the two directions: the only thing that changes is which side of the `memcpy` the window is on.
  uint32_t walk(uint32_t offset, uint32_t bytes, uint8_t* outside, bool reading) {
    if (!is_open_ || outside == nullptr || offset >= size_) return 0;
    if (bytes > size_ - offset) bytes = size_ - offset;
    uint32_t done = 0;
    while (done < bytes) {
      const uint32_t at = offset + done;
      const uint32_t block = at - (at % kExtraMemoryBlockBytes);
      if (!map_block(block)) break;
      const uint32_t inside = at - block;
      uint32_t take = kExtraMemoryBlockBytes - inside;
      if (take > bytes - done) take = bytes - done;
      if (take > size_ - at) take = size_ - at;
      if (reading) {
        memcpy(outside + done, static_cast<uint8_t*>(mapped_) + inside, take);
      } else {
        memcpy(static_cast<uint8_t*>(mapped_) + inside, outside + done, take);
      }
      done += take;
    }
    return done;
  }

  bool map_block(uint32_t block) {
    if (block == mapped_block_) return true;
    unmap();
    void* mapped = nullptr;
    if (esp_himem_map(handle_, window_, skip_ + block, 0, kExtraMemoryBlockBytes, 0, &mapped) != ESP_OK) return false;
    mapped_ = mapped;
    mapped_block_ = block;
    return true;
  }

  void unmap() {
    if (mapped_ == nullptr) return;
    esp_himem_unmap(window_, mapped_, kExtraMemoryBlockBytes);
    mapped_ = nullptr;
    mapped_block_ = kNoBlock;
  }

  esp_himem_handle_t handle_ = nullptr;
  esp_himem_rangehandle_t window_ = nullptr;
  uint32_t size_ = 0;
  uint32_t skip_ = 0;
  uint32_t mapped_block_ = kNoBlock;
  void* mapped_ = nullptr;
  bool is_open_ = false;
};

BankedRegion g_banked;

}  // namespace

IBankedMemory* esp32_open_banked_memory(uint32_t bytes) { return g_banked.open(bytes) ? &g_banked : nullptr; }

void esp32_close_banked_memory(IBankedMemory* block) {
  if (block == &g_banked) g_banked.close();
}
#else
IBankedMemory* esp32_open_banked_memory(uint32_t bytes) {
  (void)bytes;
  return nullptr;
}

void esp32_close_banked_memory(IBankedMemory* block) { (void)block; }
#endif

bool esp32_extra_memory_round_trip(uint32_t bytes) {
#ifdef LUNOK_HAS_HIMEM
  if (bytes == 0 || bytes % kExtraMemoryBlockBytes != 0) return false;
  // **La prova comença després de la finestra** (0.190.173): `esp_himem_alloc` numera els blocs des del principi de
  // l'himem, i els primers són els de la finestra, que el heap d'aquest build encara compta com a seus. Demanar-los de
  // més i no mapejar-los mai és el preu de no escriure a casa d'un altre; es tornen amb la resta del handle.
  const uint32_t skip = static_cast<uint32_t>(esp_himem_reserved_area_size());
  if (skip + bytes > esp_himem_get_free_size()) return false;

  esp_himem_handle_t memory = nullptr;
  if (esp_himem_alloc(skip + bytes, &memory) != ESP_OK) return false;
  esp_himem_rangehandle_t window = nullptr;
  if (esp_himem_alloc_map_range(kExtraMemoryBlockBytes, &window) != ESP_OK) {
    esp_himem_free(memory);
    return false;
  }

  bool good = true;
  for (uint32_t offset = 0; good && offset < bytes; offset += kExtraMemoryBlockBytes) {
    void* mapped = nullptr;
    if (esp_himem_map(memory, window, skip + offset, 0, kExtraMemoryBlockBytes, 0, &mapped) != ESP_OK) {
      good = false;
      break;
    }
    uint32_t* words = static_cast<uint32_t*>(mapped);
    const uint32_t count = kExtraMemoryBlockBytes / sizeof(uint32_t);
    for (uint32_t i = 0; i < count; ++i) words[i] = pattern_at(offset, i);
    for (uint32_t i = 0; i < count; ++i) {
      if (words[i] != pattern_at(offset, i)) {
        good = false;
        break;
      }
    }
    esp_himem_unmap(window, mapped, kExtraMemoryBlockBytes);
    // **El gos del nucli no borda mentre es prova**: a block is microseconds of PSRAM and a run of them
    // is seconds, so the task gives the rest of the system a turn between two blocks (docs/75).
    vTaskDelay(1);
  }

  esp_himem_free_map_range(window);
  esp_himem_free(memory);
  return good;
#else
  (void)bytes;
  return false;
#endif
}

}  // namespace hal
