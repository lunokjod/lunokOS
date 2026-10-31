// Pull-style JSON helpers.
//
// There is no document tree here on purpose: a manifest is read to get four keys,
// and building a tree to look at four keys costs RAM we do not have (docs/75).
#ifndef LUNOK_JSON_JSON_H
#define LUNOK_JSON_JSON_H

#include <stddef.h>
#include <stdint.h>

namespace lunok {
namespace json {

// Finds "key" in the object in text and copies its string value into out.
// The escapes \n, \t, \r, \" and \\ are translated; anything else is copied as
// is. Returns false if the key is absent, is not a string, or does not fit.
bool get_string(const char* text, size_t len, const char* key, char* out, size_t out_cap);

// One item of a string array, and how many fit. The arrays a manifest carries are
// short lists —the boards an app runs on— and are read into fixed storage that the
// caller owns (docs/75).
const size_t kStringItemCap = 24;

// Finds "key" and copies the strings of the array under it into out, up to max of
// them, and says how many there were. False when the key is absent, is not an
// array, is not closed, or does not fit: a list that is cut would be a list that
// lies (docs/40).
bool get_strings(const char* text, size_t len, const char* key,
                 char out[][kStringItemCap], size_t max, uint32_t& count);

// Same for a number with no fractional part.
bool get_int(const char* text, size_t len, const char* key, long& out);

}  // namespace json
}  // namespace lunok

#endif  // LUNOK_JSON_JSON_H
