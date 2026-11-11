#include "doctest.h"
#include "net.hpp"

#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace std;

// Helper: a PeerId filled with one repeated byte, so different IDs are easy to tell apart.
static PeerId makeId(unsigned char fill)
{
    PeerId id{};
    id.fill(fill);
    return id;
}

// ---------- buildBody / parseBody ----------

TEST_CASE("buildBody and parseBody round trip")
{
    PeerId id = makeId(0xAB);
    vector<unsigned char> payload = {1, 2, 3, 4, 5};

    vector<unsigned char> body = buildBody(id, payload);
    CHECK(body.size() == id.size() + payload.size());

    PeerId parsedId{};
    vector<unsigned char> parsedPayload;
    REQUIRE(parseBody(string(body.begin(), body.end()), parsedId, parsedPayload));
    CHECK(parsedId == id);
    CHECK(parsedPayload == payload);
}

TEST_CASE("buildBody and parseBody round trip with an empty payload")
{
    PeerId id = makeId(0x01);
    vector<unsigned char> body = buildBody(id, {});
    CHECK(body.size() == id.size());

    PeerId parsedId{};
    vector<unsigned char> parsedPayload = {9, 9};   // must be replaced, not appended to
    REQUIRE(parseBody(string(body.begin(), body.end()), parsedId, parsedPayload));
    CHECK(parsedId == id);
    CHECK(parsedPayload.empty());
}

TEST_CASE("parseBody rejects a body shorter than an ID")
{
    PeerId id{};
    vector<unsigned char> payload;
    CHECK_FALSE(parseBody(string(31, 'x'), id, payload));
    CHECK_FALSE(parseBody(string(), id, payload));
}

// ---------- parseUserList ----------

TEST_CASE("parseUserList accepts an empty list")
{
    set<PeerId> ids = {makeId(0x77)};   // leftover entry must be cleared
    REQUIRE(parseUserList(string(), ids));
    CHECK(ids.empty());
}

TEST_CASE("parseUserList reads three IDs")
{
    PeerId a = makeId(0x01), b = makeId(0x02), c = makeId(0x03);

    string payload;
    for (const PeerId& id : {a, b, c})
    {
        payload.append(reinterpret_cast<const char*>(id.data()), id.size());
    }

    set<PeerId> ids;
    REQUIRE(parseUserList(payload, ids));
    CHECK(ids.size() == 3);
    CHECK(ids.contains(a));
    CHECK(ids.contains(b));
    CHECK(ids.contains(c));
}

TEST_CASE("parseUserList rejects a size that isn't a multiple of 32")
{
    set<PeerId> ids;
    CHECK_FALSE(parseUserList(string(31, 'x'), ids));
    CHECK_FALSE(parseUserList(string(33, 'x'), ids));
    CHECK_FALSE(parseUserList(string(65, 'x'), ids));
}


