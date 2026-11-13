#include "contacts.hpp"

#include <string>
#include <sodium.h>
#include <map>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "net.hpp"
#include "util.hpp"

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


bool addContact(Contacts& c, const PeerId& id, const string& name, string& error)
{
    // check if name is invalid here
    if (!isValidName(name))
    {
        error = "Invalid Name";
        return false;
    }

    // reject if name alr exists; or an id is already saved by another name
    for (const auto& [contactId, contactName] : c)
    {
        if (id == contactId && name != contactName)
        {
            error = "ID is already saved under a different name";
            return false;
        }
        if (id != contactId && name == contactName)
        {
            error = "Name exists for a different contact";
            return false;
        }
    }
    
    c[id] = name;
    return true;
}


bool loadContacts(const string& path, Contacts& c, vector<string>& warnings)
{
    const filesystem::path filePath = filesystem::path(path) / "contacts.txt";

    if (!filesystem::exists(filePath))
    {
        return true;
    }

    ifstream inFile(filePath);
    if (!inFile.is_open())
    {
        return false;
    }

    string line;
    while (getline(inFile, line))
    {
        size_t colonIdx = line.find(" : ");
        if (colonIdx == string::npos)
        {
            cerr << "Client sender does not have a name" << endl;
            continue;
        }

        string stringId = line.substr(0, colonIdx);
        string name = line.substr(colonIdx + 3);

        PeerId id;
        if (!hexToId(stringId, id))
        {
            return false;
        }

        c[id] = name;
    }


    return true;
}


bool saveContacts(const string& path, const Contacts& c)
{
    const filesystem::path filePath = filesystem::path(path) / "contacts.txt";

    ofstream outFile(filePath);
    if (!outFile.is_open())
    {
        return false;
    }

    for (const auto& [contactId, contactName] : c)
    {
        string id = idToHex(contactId);
        outFile << id << " : " << contactName << "\n";

        if (!outFile)
        {
            return false;
        }
    }
    
    outFile.close();

    return !outFile.fail();
}


