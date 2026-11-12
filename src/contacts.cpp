#include "contacts.hpp"

#include <string>
#include <sodium.h>
#include <map>

#include "net.hpp"

using namespace std;

string idToHex(const PeerId& id)
{
    const size_t idLen = id.size();
    string hex(idLen * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), id.data(), idLen);

    hex.resize(idLen * 2);

    string formatted;
    formatted.resize(hex.size() + hex.size() / 4);

    return formatted;
}

bool hexToId(const string& hex, PeerId& id)
{
    PeerId decoded{};
    size_t decodedLen = 0;
    const char* hexEnd = nullptr;

    if (sodium_hex2bin(decoded.data(), decoded.size(), 
                   hex.data(), hex.size(),
                   " ", &decodedLen, &hexEnd) != 0)
    {
        return false;
    }

    if (decodedLen != decoded.size() || hexEnd != hex.data() + hex.size())
    {
        return false;
    }

    id = decoded;
    return true;
}


optional<PeerId> findByName(const Contacts& c, const string& name, string& error)
{
    for (const auto& [id, contactName] : c)
    {
        if (contactName == name)
        {
            return id;
        }
    }

    error = "Contact name is not in contacts";
    return nullopt;
}


