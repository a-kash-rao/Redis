#pragma once

#include <stddef.h>

// wire format shared by the server and the client
//
// every message (both ways) is:  [len: u32][body: len bytes]
//
// request body:   [nstr: u32][len: u32][str1][len: u32][str2]...
//                 eg. "set k v" -> 3, 3,"set", 1,"k", 1,"v"
//
// response body:  one TLV value (Tag, then Length/Value depending on the tag)
//   TAG_NIL  [tag]
//   TAG_ERR  [tag][code: u32][len: u32][msg]
//   TAG_STR  [tag][len: u32][bytes]
//   TAG_INT  [tag][i64]
//   TAG_DBL  [tag][f64]
//   TAG_ARR  [tag][n: u32] followed by n nested TLV values
//
// integers are sent in host byte order (little-endian on x86/ARM),
// same as the book, to keep the code simple

const size_t k_max_msg = 32 << 20;      // 32MB, max size of one message
const size_t k_max_args = 200 * 1000;   // max number of strings in a request

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

// error codes for TAG_ERR
enum {
    ERR_UNKNOWN = 1,    // unknown command
    ERR_TOO_BIG = 2,    // response too big
    ERR_BAD_TYP = 3,    // wrong value type for this command
    ERR_BAD_ARG = 4,    // bad argument (eg. not a number)
};
