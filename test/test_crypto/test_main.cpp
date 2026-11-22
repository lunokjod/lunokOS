// **L'aritmètica d'un secret, contra la seva pròpia norma**: SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256 i ChaCha20, i el
// bloc que `crypto.seal()` fa amb elles (0.190.258, docs/30, docs/45).
//
// A hash that only agrees with itself is a hash that disagrees with the rest of the world the day somebody reads the file
// with a computer, and on the glass nobody notices. So every one of the four is walked with **the published test vector of
// its document**, with the section named next to the numbers, and the two properties the vault rests on are walked at the
// end: a sealed block comes back with the same key, and a blob with one changed byte is REFUSED instead of decrypted into
// rubbish.
//
// The vectors are the paper project's (`OLD/PaperOS/test/native/tests_crypto.cpp`), and that is the point: the block these
// four make is the block its vaults carry, so this suite is also the proof that a vault saved there opens here (docs/45).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

#include <unity.h>

#include "kernel/crypto.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

using namespace kernel::crypto;

std::string hex_of(const uint8_t* p, size_t n) {
  static const char* const kDigits = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < n; ++i) {
    out.push_back(kDigits[p[i] >> 4]);
    out.push_back(kDigits[p[i] & 0x0F]);
  }
  return out;
}

}  // namespace

// **SHA-256, FIPS 180-4 appendix B**: `abc`, the empty message —the case where a padding bug shows up first— and two
// blocks exactly, which is 56 bytes and leaves no room for the length in the same block.
void test_sha256_is_the_one_of_fips_180_4(void) {
  uint8_t out[kSha256Bytes];

  sha256("abc", 3, out);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                           hex_of(out, sizeof(out)).c_str());

  sha256("", 0, out);
  TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                           hex_of(out, sizeof(out)).c_str());

  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  sha256(two, strlen(two), out);
  TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                           hex_of(out, sizeof(out)).c_str());
}

// **FIPS 180-4 B.3: a million `a`s, fed one byte per call.** It is the only vector that proves the streaming half —the block
// that is waiting and the count that grows past a million—, and it is what PBKDF2 hashes thousands of times.
void test_sha256_walks_a_million_bytes_one_at_a_time(void) {
  Sha256 stream;
  for (int i = 0; i < 1000000; ++i) stream.update("a", 1);

  uint8_t out[kSha256Bytes];
  stream.finish(out);
  TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                           hex_of(out, sizeof(out)).c_str());
}

// **HMAC-SHA256, RFC 4231 cases 1, 2 and 6**: a key of padding bytes, a key that is text and shorter than a block, and the
// 131-byte key which is the branch that hashes the key first. Without the third, HMAC is right for every key of a normal
// size and wrong for the ones a file could carry.
void test_hmac_sha256_is_the_one_of_rfc_4231(void) {
  uint8_t out[kSha256Bytes];
  uint8_t key[131];

  memset(key, 0x0b, 20);
  hmac_sha256(key, 20, "Hi There", 8, out);
  TEST_ASSERT_EQUAL_STRING("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                           hex_of(out, sizeof(out)).c_str());

  hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, out);
  TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                           hex_of(out, sizeof(out)).c_str());

  memset(key, 0xaa, sizeof(key));
  const char* long_message = "Test Using Larger Than Block-Size Key - Hash Key First";
  hmac_sha256(key, sizeof(key), long_message, strlen(long_message), out);
  TEST_ASSERT_EQUAL_STRING("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
                           hex_of(out, sizeof(out)).c_str());
}

// **PBKDF2-HMAC-SHA256**: the three prices of the RFC 7914 set (one iteration, two, four thousand) and then **sixty-four
// bytes**, which is the size this house uses —the second block is a different `i` in the same function, and a derivation
// that only ever asked for 32 would never walk it.
void test_pbkdf2_is_the_one_the_rest_of_the_world_computes(void) {
  uint8_t out[64];

  pbkdf2_sha256("password", 8, "salt", 4, 1, out, 32);
  TEST_ASSERT_EQUAL_STRING("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
                           hex_of(out, 32).c_str());

  pbkdf2_sha256("password", 8, "salt", 4, 2, out, 32);
  TEST_ASSERT_EQUAL_STRING("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
                           hex_of(out, 32).c_str());

  pbkdf2_sha256("password", 8, "salt", 4, 4096, out, 32);
  TEST_ASSERT_EQUAL_STRING("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
                           hex_of(out, 32).c_str());

  pbkdf2_sha256("password", 8, "salt", 4, 4096, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
                           hex_of(out, 32).c_str());
  TEST_ASSERT_EQUAL_STRING("f7ad98c1b458ce3fd74ca35beba3cda7b8d1038d6a87071b918f837405f3fe77",
                           hex_of(out + 32, 32).c_str());

  // A price of zero is refused: it is not a derivation, it is a password in the clear with a hash of decoration.
  TEST_ASSERT_FALSE(pbkdf2_sha256("p", 1, "s", 1, 0, out, 32));
}

// **ChaCha20, RFC 8439 2.3.2**: the block function. The key is 00..1f, the nonce is `00 00 00 09 00 00 00 4a 00 00 00 00`
// and the counter is 1, so the keystream is the ciphertext of sixty-four zeros.
void test_chacha20_is_the_keystream_of_rfc_8439(void) {
  uint8_t key[32];
  for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);

  uint8_t nonce[kNonceBytes];
  memset(nonce, 0, sizeof(nonce));
  nonce[3] = 0x09;
  nonce[7] = 0x4a;

  uint8_t block[64];
  memset(block, 0, sizeof(block));
  chacha20_xor(key, nonce, 1, block, sizeof(block));

  TEST_ASSERT_EQUAL_STRING(
      "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
      "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e",
      hex_of(block, sizeof(block)).c_str());
}

// **RFC 8439 2.4.2**: it is the vector that catches the mistake this family of code makes once —ChaCha20 reads its key and
// its nonce as LITTLE-endian words and SHA-256 reads its numbers big-endian, and reading the key the wrong way round
// produces a stream that looks perfect and agrees with nobody.
void test_chacha20_encrypts_the_sunscreen_sentence_of_rfc_8439(void) {
  uint8_t key[32];
  for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);

  uint8_t nonce[kNonceBytes];
  memset(nonce, 0, sizeof(nonce));
  nonce[7] = 0x4a;

  const char* plain =
      "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
  uint8_t buf[160];
  memcpy(buf, plain, strlen(plain));

  chacha20_xor(key, nonce, 1, buf, strlen(plain));
  TEST_ASSERT_EQUAL_STRING(
      "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
      "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
      "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
      "5af90bbf74a35be6b40b8eedf2785e42874d",
      hex_of(buf, strlen(plain)).c_str());

  chacha20_xor(key, nonce, 1, buf, strlen(plain));
  TEST_ASSERT_EQUAL_MEMORY(plain, buf, strlen(plain));
}

// **THE TWO PROPERTIES THE VAULT RESTS ON**, walked through the door and not through the primitives: a block comes back with
// the same key, a byte of the ciphertext changed is refused, and another key is refused too. The nonce is new on every seal,
// which is why the same text twice is two different blobs.
void test_a_sealed_block_comes_back_and_a_changed_one_does_not(void) {
  uint8_t key[kKeyBytes];
  for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<uint8_t>(i * 7 + 3);

  const char* plain = "title=Github\nuser=someone\npassword=hunter2\n";
  const size_t plain_len = strlen(plain);
  const size_t block_len = kNonceBytes + plain_len + kTagBytes;

  uint8_t blob[kNonceBytes + 128 + kTagBytes];
  uint8_t twice[kNonceBytes + 128 + kTagBytes];
  TEST_ASSERT_TRUE(door_seal(key, sizeof(key), plain, plain_len, blob, sizeof(blob)));
  TEST_ASSERT_TRUE(door_seal(key, sizeof(key), plain, plain_len, twice, sizeof(twice)));

  // The ciphertext is not the plain text —if it were, nothing is sealed— and two seals of one text are two blobs.
  TEST_ASSERT_TRUE(memcmp(blob + kNonceBytes, plain, plain_len) != 0);
  TEST_ASSERT_TRUE(memcmp(blob, twice, block_len) != 0);

  uint8_t back[128];
  const int32_t got = door_open(key, sizeof(key), blob, block_len, back, sizeof(back));
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(plain_len), got);
  TEST_ASSERT_EQUAL_MEMORY(plain, back, plain_len);

  blob[kNonceBytes + 3] ^= 0x01;
  TEST_ASSERT_EQUAL_INT32(-1, door_open(key, sizeof(key), blob, block_len, back, sizeof(back)));
  blob[kNonceBytes + 3] ^= 0x01;

  key[40] ^= 0xFF;
  TEST_ASSERT_EQUAL_INT32(-1, door_open(key, sizeof(key), blob, block_len, back, sizeof(back)));
}

// A tag is compared over all of its bytes, a length of zero is the same nothing, and a blob too short to be a block is
// refused instead of being opened into nothing.
void test_equal_bytes_and_the_edges_of_a_block(void) {
  const uint8_t a[] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint8_t b[] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint8_t c[] = {1, 2, 3, 4, 5, 6, 7, 9};

  TEST_ASSERT_TRUE(equal_bytes(a, b, sizeof(a)));
  TEST_ASSERT_FALSE(equal_bytes(a, c, sizeof(a)));
  TEST_ASSERT_TRUE(equal_bytes(a, c, 0));

  uint8_t key[kKeyBytes];
  memset(key, 0x5a, sizeof(key));
  uint8_t out[32];
  TEST_ASSERT_EQUAL_INT32(-1, door_open(key, sizeof(key), a, sizeof(a), out, sizeof(out)));

  // And a key that is not the 64 bytes `crypto.key()` makes is refused by both doors.
  uint8_t short_key[32];
  memset(short_key, 0x11, sizeof(short_key));
  uint8_t blob[kNonceBytes + 8 + kTagBytes];
  TEST_ASSERT_FALSE(door_seal(short_key, sizeof(short_key), "12345678", 8, blob, sizeof(blob)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sha256_is_the_one_of_fips_180_4);
  RUN_TEST(test_sha256_walks_a_million_bytes_one_at_a_time);
  RUN_TEST(test_hmac_sha256_is_the_one_of_rfc_4231);
  RUN_TEST(test_pbkdf2_is_the_one_the_rest_of_the_world_computes);
  RUN_TEST(test_chacha20_is_the_keystream_of_rfc_8439);
  RUN_TEST(test_chacha20_encrypts_the_sunscreen_sentence_of_rfc_8439);
  RUN_TEST(test_a_sealed_block_comes_back_and_a_changed_one_does_not);
  RUN_TEST(test_equal_bytes_and_the_edges_of_a_block);
  return UNITY_END();
}
