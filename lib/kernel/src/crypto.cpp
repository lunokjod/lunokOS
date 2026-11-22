// ============================================================================
//  lunokOS :: kernel/crypto.cpp
// ============================================================================
//  The arithmetic of `crypto.h`: FIPS 180-4, RFC 2104, RFC 8018 and RFC 8439, written plainly and without a library, and
//  the four calls the interpreter is given (0.190.258).
//
//  **IT COMES FROM THE OLD PROJECT AND IT COMES WHOLE** (`OLD/PaperOS/src/system/crypto.cpp`, `system/crypto.h`): the same
//  four primitives, the same block layout —`nonce(12) | ciphertext | tag(16)` over ChaCha20 with counter 0 and the first
//  sixteen bytes of `HMAC(mac key, nonce | ciphertext)`— and therefore **the same files**: a vault saved by the Paper
//  opens here and a vault saved here opens there. The vectors that prove each of them are in `test/test_crypto/`, with the
//  document and the section named next to every number (docs/45, docs/80).
//
//  IT IS WALKED, NOT TRUSTED. A hash that is only "a hash" is a hash that will disagree with the rest of the world the day
//  somebody reads the file with a computer, and nobody will notice on the glass.
//
//  **WHAT THE ENTROPY IS HERE** (0.190.258). The salt of the vault and the nonce of every seal come from
//  `pal::random_bytes()`, which is the one door that asks the band for entropy *every time* instead of reading its mixer:
//  on the board it is the chip's own source —the RF noise while the radio is up and the ADC window when it is not— and on
//  the PC it is the mixer the run seeded. This is the one thing the old file did not have to decide: there, the number came
//  from `esp_random()` through the services of the interpreter.
//
//  **AND THE PRICE IS MEASURED** (the old `[CRYPTO] key: … ms` line, kept): the derivation goes to the log with its
//  iterations, its bytes of salt and the milliseconds it took, because what a master password costs on this board is a
//  number to read and not a guess in a comment (docs/95).
#include "kernel/crypto.h"

#include <string.h>

#include "pal/pal.h"

#include "kernel/log.h"
#include "kernel/meter.h"
#include "luabind/luabind.h"

namespace kernel {
namespace crypto {
namespace {

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// **The first 32 bits of the fractional parts of the cube roots of the first sixty-four primes** (FIPS 180-4, 4.2.2).
const uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void put32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

// **CHACHA20 IS LITTLE-ENDIAN AND SHA-256 IS NOT**, and mixing the two up is the one mistake that makes a cipher that looks
// perfect and agrees with nobody: the first version of the old file read the key with `be32()` and produced a stream of its
// own. The two readers sit next to each other on purpose, so the next person sees that there are two.
inline uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void put32le(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

// A KEY LONGER THAN A BLOCK IS HASHED and a shorter one is padded with zeros: that is HMAC's own rule (RFC 2104, §2), and
// the two halves of it are built ONCE for PBKDF2 because they are the same on every iteration.
void hmac_pad(const void* key, size_t key_len, uint8_t k[64]) {
  memset(k, 0, 64);
  if (key_len > 64) {
    uint8_t digest[kSha256Bytes];
    sha256(key, key_len, digest);
    memcpy(k, digest, sizeof(digest));
    memset(digest, 0, sizeof(digest));
  } else if (key_len > 0) {
    memcpy(k, key, key_len);
  }
}

}  // namespace

/* ------------------------------------------------------------------ SHA-256 */

Sha256::Sha256() : fill_(0), len_(0) {
  h_[0] = 0x6a09e667u;
  h_[1] = 0xbb67ae85u;
  h_[2] = 0x3c6ef372u;
  h_[3] = 0xa54ff53au;
  h_[4] = 0x510e527fu;
  h_[5] = 0x9b05688cu;
  h_[6] = 0x1f83d9abu;
  h_[7] = 0x5be0cd19u;
  memset(buf_, 0, sizeof(buf_));
}

void Sha256::compress(const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) w[i] = be32(block + i * 4);

  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = h_[0];
  uint32_t b = h_[1];
  uint32_t c = h_[2];
  uint32_t d = h_[3];
  uint32_t e = h_[4];
  uint32_t f = h_[5];
  uint32_t g = h_[6];
  uint32_t h = h_[7];

  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = h + s1 + ch + kK[i] + w[i];
    const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + mj;

    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}

void Sha256::update(const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  if (p == nullptr || len == 0) return;

  len_ += static_cast<uint64_t>(len);

  while (len > 0) {
    const size_t room = 64 - fill_;
    const size_t take = (len < room) ? len : room;
    memcpy(buf_ + fill_, p, take);
    fill_ += take;
    p += take;
    len -= take;
    if (fill_ == 64) {
      compress(buf_);
      fill_ = 0;
    }
  }
}

void Sha256::finish(uint8_t out[kSha256Bytes]) {
  const uint64_t bits = len_ * 8;

  uint8_t pad[72];
  size_t pad_len = 0;
  pad[pad_len++] = 0x80u;
  // Room for the length in the same block: 64 - 9 leaves at least one zero.
  while ((fill_ + pad_len) % 64 != 56) pad[pad_len++] = 0;
  update(pad, pad_len);

  // `update()` counted the padding as message; the length was taken above.
  uint8_t len_bytes[8];
  for (int i = 0; i < 8; ++i) len_bytes[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
  update(len_bytes, sizeof(len_bytes));

  for (int i = 0; i < 8; ++i) put32(out + i * 4, h_[i]);

  memset(pad, 0, sizeof(pad));
  memset(buf_, 0, sizeof(buf_));
  fill_ = 0;
}

void sha256(const void* data, size_t len, uint8_t out[kSha256Bytes]) {
  Sha256 s;
  s.update(data, len);
  s.finish(out);
}

/* ---------------------------------------------------------------------- HMAC */

void hmac_sha256(const void* key, size_t key_len, const void* msg, size_t msg_len, uint8_t out[kSha256Bytes]) {
  uint8_t k[64];
  uint8_t ipad[64];
  uint8_t opad[64];

  hmac_pad(key, key_len, k);
  for (int i = 0; i < 64; ++i) {
    ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36u);
    opad[i] = static_cast<uint8_t>(k[i] ^ 0x5cu);
  }

  uint8_t inner[kSha256Bytes];
  Sha256 in;
  in.update(ipad, sizeof(ipad));
  in.update(msg, msg_len);
  in.finish(inner);

  Sha256 ou;
  ou.update(opad, sizeof(opad));
  ou.update(inner, sizeof(inner));
  ou.finish(out);

  memset(k, 0, sizeof(k));
  memset(ipad, 0, sizeof(ipad));
  memset(opad, 0, sizeof(opad));
  memset(inner, 0, sizeof(inner));
}

/* -------------------------------------------------------------------- PBKDF2 */

bool pbkdf2_sha256(const void* password, size_t password_len, const void* salt, size_t salt_len, uint32_t iterations,
                   uint8_t* out, size_t out_len) {
  if (iterations == 0) return false;
  if (out_len == 0) return true;
  if (out == nullptr) return false;
  if (salt_len > 0 && salt == nullptr) return false;
  if (password_len > 0 && password == nullptr) return false;

  uint8_t k[64];
  uint8_t ipad[64];
  uint8_t opad[64];

  hmac_pad(password, password_len, k);
  for (int i = 0; i < 64; ++i) {
    ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36u);
    opad[i] = static_cast<uint8_t>(k[i] ^ 0x5cu);
  }
  memset(k, 0, sizeof(k));

  // THE TWO HALVES ARE BUILT ONCE AND COPIED THOUSANDS OF TIMES. An HMAC is `H(opad || H(ipad || msg))`, and the first
  // block of each half is always the same: it is compressed once here and every iteration starts from the copy instead of
  // hashing sixty-four bytes of padding again. Same answer, a third less work —which is exactly the budget a password has
  // on a 240 MHz chip—.
  Sha256 inner_base;
  Sha256 outer_base;
  inner_base.update(ipad, sizeof(ipad));
  outer_base.update(opad, sizeof(opad));

  const size_t blocks = (out_len + kSha256Bytes - 1) / kSha256Bytes;
  uint8_t u[kSha256Bytes];
  uint8_t t[kSha256Bytes];

  for (size_t b = 0; b < blocks; ++b) {
    const uint32_t index = static_cast<uint32_t>(b + 1);
    const uint8_t be[4] = {static_cast<uint8_t>(index >> 24), static_cast<uint8_t>(index >> 16),
                           static_cast<uint8_t>(index >> 8), static_cast<uint8_t>(index)};

    Sha256 in = inner_base;
    in.update(salt, salt_len);
    in.update(be, sizeof(be));
    in.finish(u);

    Sha256 ou = outer_base;
    ou.update(u, sizeof(u));
    ou.finish(u);

    memcpy(t, u, sizeof(t));

    for (uint32_t i = 1; i < iterations; ++i) {
      Sha256 in2 = inner_base;
      in2.update(u, sizeof(u));
      in2.finish(u);

      Sha256 out2 = outer_base;
      out2.update(u, sizeof(u));
      out2.finish(u);

      for (size_t j = 0; j < sizeof(t); ++j) t[j] = static_cast<uint8_t>(t[j] ^ u[j]);
    }

    const size_t at = b * kSha256Bytes;
    const size_t n = (out_len - at < kSha256Bytes) ? (out_len - at) : kSha256Bytes;
    memcpy(out + at, t, n);
  }

  memset(ipad, 0, sizeof(ipad));
  memset(opad, 0, sizeof(opad));
  memset(u, 0, sizeof(u));
  memset(t, 0, sizeof(t));
  return true;
}

/* ------------------------------------------------------------------ ChaCha20 */

void chacha20_xor(const uint8_t key[32], const uint8_t nonce[kNonceBytes], uint32_t counter, uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || key == nullptr || nonce == nullptr) return;

  uint32_t state[16];
  state[0] = 0x61707865u;
  state[1] = 0x3320646eu;
  state[2] = 0x79622d32u;
  state[3] = 0x6b206574u;
  for (int i = 0; i < 8; ++i) state[4 + i] = le32(key + i * 4);
  state[12] = counter;
  state[13] = le32(nonce);
  state[14] = le32(nonce + 4);
  state[15] = le32(nonce + 8);

  uint8_t stream[64];
  size_t done = 0;

  while (done < len) {
    uint32_t x[16];
    memcpy(x, state, sizeof(x));

    for (int round = 0; round < 10; ++round) {
      // Column rounds.
      x[0] += x[4];  x[12] = rotl(x[12] ^ x[0], 16);
      x[8] += x[12]; x[4] = rotl(x[4] ^ x[8], 12);
      x[0] += x[4];  x[12] = rotl(x[12] ^ x[0], 8);
      x[8] += x[12]; x[4] = rotl(x[4] ^ x[8], 7);

      x[1] += x[5];  x[13] = rotl(x[13] ^ x[1], 16);
      x[9] += x[13]; x[5] = rotl(x[5] ^ x[9], 12);
      x[1] += x[5];  x[13] = rotl(x[13] ^ x[1], 8);
      x[9] += x[13]; x[5] = rotl(x[5] ^ x[9], 7);

      x[2] += x[6];  x[14] = rotl(x[14] ^ x[2], 16);
      x[10] += x[14]; x[6] = rotl(x[6] ^ x[10], 12);
      x[2] += x[6];  x[14] = rotl(x[14] ^ x[2], 8);
      x[10] += x[14]; x[6] = rotl(x[6] ^ x[10], 7);

      x[3] += x[7];  x[15] = rotl(x[15] ^ x[3], 16);
      x[11] += x[15]; x[7] = rotl(x[7] ^ x[11], 12);
      x[3] += x[7];  x[15] = rotl(x[15] ^ x[3], 8);
      x[11] += x[15]; x[7] = rotl(x[7] ^ x[11], 7);

      // Diagonal rounds.
      x[0] += x[5];  x[15] = rotl(x[15] ^ x[0], 16);
      x[10] += x[15]; x[5] = rotl(x[5] ^ x[10], 12);
      x[0] += x[5];  x[15] = rotl(x[15] ^ x[0], 8);
      x[10] += x[15]; x[5] = rotl(x[5] ^ x[10], 7);

      x[1] += x[6];  x[12] = rotl(x[12] ^ x[1], 16);
      x[11] += x[12]; x[6] = rotl(x[6] ^ x[11], 12);
      x[1] += x[6];  x[12] = rotl(x[12] ^ x[1], 8);
      x[11] += x[12]; x[6] = rotl(x[6] ^ x[11], 7);

      x[2] += x[7];  x[13] = rotl(x[13] ^ x[2], 16);
      x[8] += x[13]; x[7] = rotl(x[7] ^ x[8], 12);
      x[2] += x[7];  x[13] = rotl(x[13] ^ x[2], 8);
      x[8] += x[13]; x[7] = rotl(x[7] ^ x[8], 7);

      x[3] += x[4];  x[14] = rotl(x[14] ^ x[3], 16);
      x[9] += x[14]; x[4] = rotl(x[4] ^ x[9], 12);
      x[3] += x[4];  x[14] = rotl(x[14] ^ x[3], 8);
      x[9] += x[14]; x[4] = rotl(x[4] ^ x[9], 7);
    }

    for (int i = 0; i < 16; ++i) put32le(stream + i * 4, x[i] + state[i]);

    const size_t take = (len - done < 64) ? (len - done) : 64;
    for (size_t i = 0; i < take; ++i) data[done + i] = static_cast<uint8_t>(data[done + i] ^ stream[i]);

    done += take;
    ++state[12];
  }

  memset(stream, 0, sizeof(stream));
  memset(state, 0, sizeof(state));
}

/* ------------------------------------------------------------------ compare */

bool equal_bytes(const void* a, const void* b, size_t len) {
  const uint8_t* x = static_cast<const uint8_t*>(a);
  const uint8_t* y = static_cast<const uint8_t*>(b);
  if (x == nullptr || y == nullptr) return false;

  // `volatile` IS THE POINT: an ordinary accumulator lets the compiler stop at the first difference, and a stopwatch would
  // then say which byte was wrong. This one is read once and it has walked all of them.
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) diff = static_cast<uint8_t>(diff | static_cast<uint8_t>(x[i] ^ y[i]));
  return diff == 0;
}

/* --------------------------------------------------------------------- door */

bool door_random(uint8_t* out, uint32_t len) {
  if (out == nullptr || len == 0) return false;
  return pal::system().random_bytes(out, len);
}

bool door_key(const char* password, size_t password_len, const char* salt, size_t salt_len, uint32_t iterations,
              uint8_t* out, size_t out_len) {
  if (out == nullptr || out_len == 0) return false;
  if (salt_len > 0 && salt == nullptr) return false;
  if (password_len > 0 && password == nullptr) return false;

  // **One row of the meters and one line of the log per derivation** (0.190.258): PBKDF2 is the one call of this system
  // that is *meant* to be expensive, and a price nobody measures is a price that is wrong one day. The line carries the
  // three numbers of the old project's `[CRYPTO] key:` —the iterations, the bytes of salt and the milliseconds— so that the
  // number in the file can be chosen on the glass and not in a comment (docs/95).
  meter::Scope scope(meter::kCryptoKey);
  const int64_t began = static_cast<int64_t>(pal::system().millis());
  const bool ok = pbkdf2_sha256(password, password_len, salt, salt_len, iterations, out, out_len);
  const int64_t took = static_cast<int64_t>(pal::system().millis()) - began;
  log_printf(pal::kLogInfo, "CRYPTO", "key: %u iterations, %u bytes of salt, %lld ms",
             static_cast<unsigned>(iterations), static_cast<unsigned>(salt_len), static_cast<long long>(took));
  return ok;
}

bool door_seal(const uint8_t* key, size_t key_len, const char* text, size_t text_len, uint8_t* out, size_t out_len) {
  const size_t total = kNonceBytes + text_len + kTagBytes;
  if (key == nullptr || out == nullptr || key_len != kKeyBytes) return false;
  if (text == nullptr && text_len > 0) return false;
  if (out_len < total) return false;
  if (!pal::system().random_bytes(out, kNonceBytes)) return false;

  if (text_len > 0) memcpy(out + kNonceBytes, text, text_len);
  chacha20_xor(key, out, 0, out + kNonceBytes, text_len);

  uint8_t tag[kSha256Bytes];
  hmac_sha256(key + 32, 32, out, kNonceBytes + text_len, tag);
  memcpy(out + kNonceBytes + text_len, tag, kTagBytes);
  memset(tag, 0, sizeof(tag));
  return true;
}

int32_t door_open(const uint8_t* key, size_t key_len, const uint8_t* blob, size_t blob_len, uint8_t* out,
                  uint32_t cap) {
  if (key == nullptr || blob == nullptr || key_len != kKeyBytes) return -1;
  if (blob_len < kNonceBytes + kTagBytes) return -1;

  const size_t plain = blob_len - kNonceBytes - kTagBytes;
  if (out == nullptr || plain > cap) return -1;

  uint8_t tag[kSha256Bytes];
  hmac_sha256(key + 32, 32, blob, blob_len - kTagBytes, tag);
  const bool same = equal_bytes(tag, blob + blob_len - kTagBytes, kTagBytes);
  memset(tag, 0, sizeof(tag));
  if (!same) return -1;

  if (plain > 0) memcpy(out, blob + kNonceBytes, plain);
  chacha20_xor(key, blob, 0, out, plain);
  return static_cast<int32_t>(plain);
}

bool boot() {
  luabind::CryptoOps ops = {};
  ops.random_bytes = &door_random;
  ops.key = &door_key;
  ops.seal = &door_seal;
  ops.open = &door_open;
  luabind::set_crypto_ops(ops);
  return true;
}

}  // namespace crypto
}  // namespace kernel
