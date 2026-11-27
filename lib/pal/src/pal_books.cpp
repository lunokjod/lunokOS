// **Els llibres de la memòria** (0.190.0): the one place that knows which block is whose. The door of `IExtraMemory` calls the
// three doors of this file —taken, moved, released— and the prompt asks it what it has. Res és de cap mòdul: `lib/pal` no sap què
// és un intèrpret de Lua ni un llibre, i per això el que hi diu qui demana és una marca (`MemoryTag`).
//
// **I aquest fitxer no té ni una variable estàtica**: the table is the PAL's —it is given once, from the extra memory when the board
// has one— and it arrives here in every call, o sigui que els llibres no costen ni un byte de la RAM del Paper, que ahir en tenia
// tres de lliures (docs/75). El que hi ha és **una capçalera dins de la mateixa taula**, que és on viu l'amo d'ara, i les files que
// la segueixen.
#include <string.h>

#include "pal/pal.h"

namespace pal {
namespace {

// **Una fila del llibre**: the pointer the caller has, the bytes it asked for, who asked and the app that was in front. Sixteen
// bytes of name and not a pointer to the app's, perquè el nom d'una app viu a l'intèrpret i un bloc el sobreviu —i el que es llegeix
// aquí és una llista que ha de ser certa hores després, quan l'app que la va demanar ja no hi és— (docs/75).
struct Row {
  void* block;
  uint32_t bytes;
  uint8_t owner;
  char app[16];
};

// **El cap dels llibres**, the first bytes of the table the PAL gives: how many rows there are, how many asks did not fit, and who
// is asking right now —the head of the mark that `MemoryTag` pushes and pops— (docs/75).
struct Head {
  uint32_t magic;
  uint32_t slots;
  uint32_t lost;
  // **Les que no hi van cabre, per casa** (0.192.92): the two homes have two totals and a bar of the memory paints them apart, so a
  // row that was lost has to be counted where it was asked for. Zero and one are the RAM and the extra memory, as everywhere else
  // in this layer (docs/75).
  uint32_t lost_in[2];
  uint8_t owner;
  char app[16];
};

constexpr uint32_t kMagic = 0x424B4C4DU;  // "MLKB", so a table of somebody else is not taken for the books

// **Un quart de la taula és dels blocs grans**: a Lua machine has thousands of small blocks alive and a few big ones, so a table
// that is first-fit fills up with the small ones and loses exactly what a person came to see. A block of `kBigBytes` or more may
// use the reserved rows even when the rest is full; the small ones never touch them (docs/75).
constexpr uint32_t kBigBytes = 4096;
constexpr uint32_t kReservedShare = 4;  // one row in four

// **The books of this call**: the table the PAL keeps, or nullptr when this platform has none —or before the first block, when there
// is nothing to write down yet— (docs/75).
Head* the_books() {
  void* table = extra_memory().books();
  if (table == nullptr) return nullptr;
  Head* head = static_cast<Head*>(table);
  if (head->magic != kMagic) return nullptr;
  return head;
}

Row* rows_of(Head* head) { return reinterpret_cast<Row*>(head + 1); }

uint32_t reserved_room(uint32_t slots) {
  const uint32_t share = slots / kReservedShare;
  return (share == 0) ? 0 : share;
}

void copy_the_name(char* to, const char* from) {
  if (from == nullptr) {
    to[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; i + 1 < 15 && from[i] != '\0'; ++i) to[i] = from[i];
  to[i] = '\0';
}

bool is_big(uint32_t bytes) { return bytes >= kBigBytes; }

// **A row for a block of these bytes**, from its own part of the table, or nullptr when the part is full (docs/75).
Row* a_free_row(Head* head, bool big) {
  Row* rows = rows_of(head);
  const uint32_t reserved = reserved_room(head->slots);
  const uint32_t limit = head->slots - reserved;
  if (big) {
    for (uint32_t i = limit; i < head->slots; ++i) {
      if (rows[i].block == nullptr) return &rows[i];
    }
  }
  for (uint32_t i = 0; i < limit; ++i) {
    if (rows[i].block == nullptr) return &rows[i];
  }
  // **I si la part dels petits és plena, un bloc gran encara hi cap**: a block of four kilobytes is worth a row of the small ones,
  // que tornaran a preguntar de seguida (docs/75).
  if (big) {
    for (uint32_t i = limit; i < head->slots; ++i) {
      if (rows[i].block == nullptr) return &rows[i];
    }
  }
  return nullptr;
}

Row* the_row_of(Head* head, void* block) {
  Row* rows = rows_of(head);
  for (uint32_t i = 0; i < head->slots; ++i) {
    if (rows[i].block == block) return &rows[i];
  }
  return nullptr;
}

}  // namespace

const char* memory_owner_word(MemoryOwner who) {
  switch (who) {
    case kMemoryKernel:
      return "kernel";
    case kMemoryShell:
      return "shell";
    case kMemoryUi:
      return "ui";
    case kMemorySession:
      return "session";
    case kMemoryBook:
      return "book";
    case kMemoryThumb:
      return "thumb";
    case kMemorySplash:
      return "splash";
    case kMemoryLua:
      return "lua";
    case kMemoryMeter:
      return "meter";
    case kMemoryNet:
      return "net";
    case kMemoryBoard:
      return "board";
    case kMemoryOther:
      return "other";
    default:
      return "nobody";
  }
}

MemoryTag::MemoryTag(MemoryOwner who, const char* app) : books_(nullptr), was_owner_(kMemoryNobody), was_app_{} {
  // **La taula abans que el nom** (0.190.0): the table is made here if this is the first tag of the run, or the owner would be
  // written into a head that does not exist yet and the first hundreds of blocks would be nobody's (docs/75).
  extra_memory().open_the_books();
  books_ = the_books();
  if (books_ == nullptr) return;
  Head* head = static_cast<Head*>(books_);
  was_owner_ = head->owner;
  copy_the_name(was_app_, head->app);
  head->owner = static_cast<uint8_t>(who);
  copy_the_name(head->app, app);
}

MemoryTag::~MemoryTag() {
  if (books_ == nullptr) return;
  Head* head = static_cast<Head*>(books_);
  head->owner = was_owner_;
  copy_the_name(head->app, was_app_);
}

Scratch::Scratch(size_t bytes, MemoryOwner who, bool prefer_extra) : block_(nullptr) {
  if (bytes == 0) return;
  MemoryTag who_asks(who);
  block_ = extra_memory().allocate(bytes, prefer_extra);
}

Scratch::~Scratch() {
  if (block_ != nullptr) extra_memory().release(block_);
}

void memory_books_begin(void* table, size_t bytes) {
  if (table == nullptr || bytes < sizeof(Head) + sizeof(Row)) return;
  Head* head = static_cast<Head*>(table);
  head->slots = static_cast<uint32_t>((bytes - sizeof(Head)) / sizeof(Row));
  head->lost = 0;
  head->lost_in[0] = 0;
  head->lost_in[1] = 0;
  head->owner = kMemoryNobody;
  head->app[0] = '\0';
  Row* rows = rows_of(head);
  for (uint32_t i = 0; i < head->slots; ++i) {
    rows[i].block = nullptr;
    rows[i].bytes = 0;
    rows[i].owner = kMemoryNobody;
    rows[i].app[0] = '\0';
  }
  // **La marca va al final**: a table that is half written is not the books of anything, and the magic is what says that the block
  // is (docs/75).
  head->magic = kMagic;
}

void memory_book_taken(void* block, size_t bytes) {
  if (block == nullptr) return;
  Head* head = the_books();
  if (head == nullptr) return;
  Row* row = a_free_row(head, is_big(static_cast<uint32_t>(bytes)));
  if (row == nullptr) {
    ++head->lost;
    // **I en quina casa** (0.192.92): the pointer is the one thing that says where the bytes are, so the count of what the books
    // could not write down is kept apart for the two homes (docs/75).
    ++head->lost_in[extra_memory().in_the_extra(block) ? 1 : 0];
    return;
  }
  row->block = block;
  row->bytes = static_cast<uint32_t>(bytes);
  row->owner = head->owner;
  copy_the_name(row->app, head->app);
}

void memory_book_moved(void* was, void* block, size_t bytes) {
  if (block == nullptr) return;
  // **Un `resize` d'un bloc que no hi era és un bloc nou** (0.190.0): `realloc(NULL, n)` is how the interpreter asks for a block it has
  // never had, and looking for a row with a null pointer found **the first free one** —which is nobody's— and left the block without an
  // owner for ever. Es va veure a l'emulador: dues-centes quaranta-nou files sense amo i totes les de Lua amb amo (docs/75).
  if (was == nullptr) {
    memory_book_taken(block, bytes);
    return;
  }
  Head* head = the_books();
  if (head == nullptr) return;
  Row* row = the_row_of(head, was);
  if (row == nullptr) {
    memory_book_taken(block, bytes);
    return;
  }
  // **Una fila que es mou no canvia d'amo**: the bytes are the new ones and the rest of the row is the block's history, perquè un
  // `resize` no és una petició nova sinó el mateix bloc que ha canviat de mida (docs/75).
  row->block = block;
  row->bytes = static_cast<uint32_t>(bytes);
}

void memory_book_released(void* block) {
  Head* head = the_books();
  if (head == nullptr || block == nullptr) return;
  Row* row = the_row_of(head, block);
  if (row == nullptr) return;
  row->block = nullptr;
  row->bytes = 0;
  row->owner = kMemoryNobody;
  row->app[0] = '\0';
}

uint32_t memory_slots() {
  Head* head = the_books();
  return (head == nullptr) ? 0 : head->slots;
}

uint32_t memory_lost() {
  Head* head = the_books();
  return (head == nullptr) ? 0 : head->lost;
}

uint32_t memory_lost_in(uint8_t home) {
  Head* head = the_books();
  if (head == nullptr || home > 1) return 0;
  return head->lost_in[home];
}

MemoryHeld memory_held(uint8_t home) {
  MemoryHeld held;
  Head* head = the_books();
  if (head == nullptr || home > 1) return held;
  const Row* rows = rows_of(head);
  for (uint32_t i = 0; i < head->slots; ++i) {
    if (rows[i].block == nullptr) continue;
    // **La casa es pregunta al punter i no es desa**: a block may move from one home to the other with a `resize`, and a row that
    // kept the home it was born in would paint the PSRAM's bytes in the bar of the RAM (docs/75).
    if (extra_memory().in_the_extra(rows[i].block) != (home == 1)) continue;
    held.blocks += 1;
    held.bytes += rows[i].bytes;
  }
  return held;
}

uint32_t memory_live() {
  Head* head = the_books();
  if (head == nullptr) return 0;
  const Row* rows = rows_of(head);
  uint32_t live = 0;
  for (uint32_t i = 0; i < head->slots; ++i) {
    if (rows[i].block != nullptr) ++live;
  }
  return live;
}

uint32_t memory_blocks(MemoryBlock* out, uint32_t cap) {
  Head* head = the_books();
  if (head == nullptr) return 0;
  const Row* rows = rows_of(head);
  uint32_t total = 0;
  uint32_t kept = 0;
  for (uint32_t i = 0; i < head->slots; ++i) {
    if (rows[i].block == nullptr) continue;
    ++total;
    if (out == nullptr || cap == 0) continue;
    // **The biggest first, placed where it goes**: the list a person reads is a few dozen rows and the table is a few hundred, so
    // an insertion into the caller's array is cheaper than sorting the table and it does not need a second one (docs/75). **I una
    // llista plena encara accepta el que és més gran que el seu últim**: without this a table whose big blocks are in the reserved
    // rows at the end —which is where the canvases of the ui are— printed only the small ones of the front (found on the emulator,
    // 18/11: three blocks of 779 873 B in `mem` and not one of them in the list).
    if (kept >= cap && rows[i].bytes <= out[cap - 1].bytes) continue;
    uint32_t at = (kept < cap) ? kept : cap - 1;
    while (at > 0 && out[at - 1].bytes < rows[i].bytes) {
      if (at < cap) out[at] = out[at - 1];
      --at;
    }
    out[at].block = rows[i].block;
    out[at].bytes = rows[i].bytes;
    out[at].owner = rows[i].owner;
    copy_the_name(out[at].app, rows[i].app);
    if (kept < cap) ++kept;
  }
  return total;
}

uint32_t memory_bills(MemoryBill* out, uint32_t cap) {
  Head* head = the_books();
  if (head == nullptr || out == nullptr) return 0;
  const Row* rows = rows_of(head);
  uint32_t groups = 0;
  for (uint32_t i = 0; i < head->slots; ++i) {
    if (rows[i].block == nullptr) continue;
    uint32_t at = groups;
    for (uint32_t g = 0; g < groups; ++g) {
      if (out[g].owner != rows[i].owner) continue;
      if (strncmp(out[g].app, rows[i].app, sizeof(out[g].app)) != 0) continue;
      at = g;
      break;
    }
    if (at == groups) {
      if (groups >= cap) continue;
      out[groups].owner = rows[i].owner;
      copy_the_name(out[groups].app, rows[i].app);
      out[groups].blocks = 0;
      out[groups].bytes = 0;
      ++groups;
    }
    out[at].blocks += 1;
    out[at].bytes += rows[i].bytes;
  }
  // **And the summary is read the same way as the list**: the module that holds the most bytes is the first line (docs/75).
  for (uint32_t i = 1; i < groups; ++i) {
    const MemoryBill row = out[i];
    uint32_t at = i;
    while (at > 0 && out[at - 1].bytes < row.bytes) {
      out[at] = out[at - 1];
      --at;
    }
    out[at] = row;
  }
  return groups;
}

}  // namespace pal
