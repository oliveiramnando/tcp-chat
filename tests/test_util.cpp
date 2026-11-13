#include "doctest.h"
#include "../src/util.hpp"

#include <string>

using namespace std;

TEST_CASE("isValidName accepts safe names")
{
    CHECK(isValidName("alice"));
    CHECK(isValidName("Bob_2"));
    CHECK(isValidName("test-user"));
    CHECK(isValidName(string(32, 'a')));   // exactly the limit
}

TEST_CASE("isValidProfileName rejects unsafe names")
{
    CHECK_FALSE(isValidName("../x"));      // path traversal
    CHECK_FALSE(isValidName("a/b"));
    CHECK_FALSE(isValidName(".."));
    CHECK_FALSE(isValidName(""));          // empty
    CHECK_FALSE(isValidName("a b"));       // space
    CHECK_FALSE(isValidName(string(33, 'a')));   // one over the limit
    CHECK_FALSE(isValidName("caf\xc3\xa9"));     // non-ASCII (é)
}
