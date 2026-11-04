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

// **Why a list of strings was not read** (0.88.11): a caller that says something to a person has to be able to say
// which of these it was —a manifest that names nine boards is not a manifest that names none, and telling the person
// the wrong one is a lie about their own file— (docs/40, docs/75).
enum class Strings : uint8_t {
  kOk,
  kAbsent,       // there is no such key
  kNotArray,     // the key is there and its value is not a list
  kUnclosed,     // the list, or an item of it, never closes
  kEmptyItem,    // an item is empty and names nothing
  kItemTooLong,  // an item does not fit in kStringItemCap
  kTooMany,      // the list is longer than the caller's storage
};

// Finds "key" and copies the strings of the array under it into out, up to max of them, and says how many were copied.
// **The list is walked whole even when it does not fit**: `total`, when the caller wants it, is how many the list has,
// which is what tells "it does not say" from "it says more than this system carries" (docs/40, docs/75).
Strings get_strings(const char* text, size_t len, const char* key, char out[][kStringItemCap], size_t max,
                    uint32_t& count, uint32_t* total = nullptr);

// Same for a number with no fractional part.
bool get_int(const char* text, size_t len, const char* key, long& out);

}  // namespace json
}  // namespace lunok

#endif  // LUNOK_JSON_JSON_H
