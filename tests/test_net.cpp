#include "doctest.h"
#include "net.hpp"

TEST_CASE("buildBody and parseBody round trip")
{
    PeerId id{};
    id.fill(0xAB);
    std::vector<unsigned char> payload = {1, 2, 3};

    std::vector<unsigned char> body = buildBody(id, payload);

    PeerId parsedId{};
    std::vector<unsigned char> parsedPayload;
    CHECK(parseBody(std::string(body.begin(), body.end()), parsedId, parsedPayload));
    CHECK(parsedId == id);
    CHECK(parsedPayload == payload);
}

TEST_CASE("parseBody rejects a body shorter than an ID")
{
    PeerId id{};
    std::vector<unsigned char> payload;
    CHECK_FALSE(parseBody(std::string(31, 'x'), id, payload));
}

