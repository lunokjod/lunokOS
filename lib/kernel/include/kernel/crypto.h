// **L'aritmètica que un secret necessita** (0.190.258): SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256 and ChaCha20, and the two
// calls that turn them into a sealed block —`seal` and `open`—, which is what the app `password` asks for and the only
// thing it asks for (docs/30, docs/45).
//
// WHY THIS IS C++ AND NOT A SCRIPT. A script can XOR two strings; what it cannot do is derive a key. PBKDF2 is *by design*
// the part that costs, and the same derivation that takes milliseconds here is thousands of times slower in bytecode —and
// a password file whose key took a hundred iterations is a password file that falls to a dictionary overnight—. So the
// arithmetic lives here and **the app keeps the policy**: which password, which salt, how expensive, and where the bytes go.
//
// WHY NOT mbedTLS, WHICH THE CHIP ALREADY CARRIES. Two reasons and both are practical: this code is walked by **the suite
// of a PC that has no ESP-IDF in it** (a door nobody can test on the host is a door nobody proves), and the block these
// four make is **the block the old project wrote** (`OLD/PaperOS/src/system/crypto.cpp`, walked against the test vectors
// of FIPS 180-4, RFC 4231, RFC 7914 and RFC 8439), so a vault saved by that system opens here. mbedTLS's answer for the
// same job is ChaCha20-Poly1305, a different tag over a different layout, and using it would have meant that the two
// systems could not read each other's file (docs/45).
//
// A CALLER HAS TO KNOW THREE THINGS:
//  - **Nothing here allocates.** Every buffer is the caller's and every size is a constant of the standard.
//  - **A key is used for one thing.** `key()` hands back 64 bytes: the first 32 are the cipher's and the last 32 the MAC's.
//    One key with two jobs is the classic way of turning two sound primitives into one weak one.
//  - **Comparing a tag is not `memcmp`.** `equal_bytes()` reads all of it, so the time it takes says nothing about which
//    byte was wrong.
#ifndef LUNOK_KERNEL_CRYPTO_H
#define LUNOK_KERNEL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace crypto {

// The working key of `key()`: 32 bytes for ChaCha20 and 32 for HMAC-SHA256.
static const size_t kKeyBytes = 64;
// A SHA-256 digest, and an HMAC one.
static const size_t kSha256Bytes = 32;
// The nonce of ChaCha20 (RFC 8439, 96 bits) and the tag this house keeps, which is the first half of an HMAC.
static const size_t kNonceBytes = 12;
static const size_t kTagBytes = 16;
// The most one block can carry: 16 KB is what a whole vault is worth and one write of the disk accepts (docs/70).
static const size_t kMaxPlainBytes = 16384;

// SHA-256 as a stream, because PBKDF2 hashes the same small blocks thousands of times and a one-shot function would copy
// them on every iteration. `update()` may be called with a length of zero and `finish()` only once.
class Sha256 {
 public:
  Sha256();
  void update(const void* data, size_t len);
  // @param out 32 bytes; the object is spent afterwards.
  void finish(uint8_t out[kSha256Bytes]);

 private:
  void compress(const uint8_t block[64]);

  uint32_t h_[8];
  uint8_t buf_[64];
  size_t fill_;
  uint64_t len_;
};

void sha256(const void* data, size_t len, uint8_t out[kSha256Bytes]);

// HMAC-SHA256 (RFC 2104): 32 bytes out, and the key may be of any length.
void hmac_sha256(const void* key, size_t key_len, const void* msg, size_t msg_len, uint8_t out[kSha256Bytes]);

// PBKDF2-HMAC-SHA256 (RFC 8018). False when an argument is impossible, which is a null buffer that has a length.
bool pbkdf2_sha256(const void* password, size_t password_len, const void* salt, size_t salt_len, uint32_t iterations,
                   uint8_t* out, size_t out_len);

// ChaCha20 XOR, in place (RFC 8439: a 32-bit counter and a 96-bit nonce). Sealing and opening are the same call, and that
// is the point: the counter is the position in the stream and nothing else.
void chacha20_xor(const uint8_t key[32], const uint8_t nonce[kNonceBytes], uint32_t counter, uint8_t* data, size_t len);

// Are these the same bytes, in a time that says nothing about WHERE they differ?
bool equal_bytes(const void* a, const void* b, size_t len);

// ------------------------------------------------------------------ the door
//
// The four things a secret file needs and nothing else. `seal` writes `nonce(12) | ciphertext | tag(16)` into `out`, which
// needs `kNonceBytes + text_len + kTagBytes`, and the nonce comes from the band's own entropy on every call: sealing the
// same text twice must not produce the same bytes, because a stream cipher that repeats a nonce with a key repeats its
// stream and that hands the plain text away. `open` answers the length of the plain text, or -1 when the tag does not
// match —a wrong key and a changed file are the same answer on purpose: telling them apart would tell whoever has the file
// whether the password was close—. Neither of them ever says anything about the key in the log.

bool door_random(uint8_t* out, uint32_t len);
// The derivation is the one call of this system that is expensive on purpose, so it goes to the log with its iterations,
// its salt and the milliseconds it took —the old `[CRYPTO] key: … ms` line— and it has a row of the meters (docs/95).
bool door_key(const char* password, size_t password_len, const char* salt, size_t salt_len, uint32_t iterations,
              uint8_t* out, size_t out_len);
bool door_seal(const uint8_t* key, size_t key_len, const char* text, size_t text_len, uint8_t* out, size_t out_len);
int32_t door_open(const uint8_t* key, size_t key_len, const uint8_t* blob, size_t blob_len, uint8_t* out, uint32_t cap);

// Installs the four in the interpreter, so that Lua has `crypto.random`, `crypto.key`, `crypto.seal` and `crypto.open`
// (docs/30). It is pure software and every band has it: there is no board that cannot seal a block, and the entropy is the
// PAL's `random_bytes()`, which a band with no other source answers with its mixer (docs/50).
bool boot();

}  // namespace crypto
}  // namespace kernel

#endif  // LUNOK_KERNEL_CRYPTO_H
