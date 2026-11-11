#include "doctest.h"
#include "util.hpp"

#include <string>

using namespace std;

TEST_CASE("isValidProfileName accepts safe names")
{
    CHECK(isValidProfileName("alice"));
    CHECK(isValidProfileName("Bob_2"));
    CHECK(isValidProfileName("test-user"));
    CHECK(isValidProfileName(string(32, 'a')));   // exactly the limit
}

TEST_CASE("isValidProfileName rejects unsafe names")
{
    CHECK_FALSE(isValidProfileName("../x"));      // path traversal
    CHECK_FALSE(isValidProfileName("a/b"));
    CHECK_FALSE(isValidProfileName(".."));
    CHECK_FALSE(isValidProfileName(""));          // empty
    CHECK_FALSE(isValidProfileName("a b"));       // space
    CHECK_FALSE(isValidProfileName(string(33, 'a')));   // one over the limit
    CHECK_FALSE(isValidProfileName("caf\xc3\xa9"));     // non-ASCII (é)
}
