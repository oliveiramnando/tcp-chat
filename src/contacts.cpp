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
    string hex(id.size() * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), id.data(), id.size());

    hex.resize(id.size() * 2);

    return hex;
}

bool hexToId(const string& hex, PeerId& id)
{
    if (hex.size() != id.size() * 2)
    {
        return false;
    }

    PeerId decoded{};
    size_t decodedLen = 0;
    const char* hexEnd = nullptr;

    if (sodium_hex2bin(decoded.data(), decoded.size(), 
                   hex.data(), hex.size(),
                   nullptr, &decodedLen, &hexEnd) != 0)
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


optional<PeerId> findByName(const Contacts& c, const string& name)
{
    for (const auto& [id, contactName] : c)
    {
        if (contactName == name)
        {
            return id;
        }
    }
    return nullopt;
}


bool addContact(Contacts& c, const PeerId& id, const string& name, string& error)
{
    // check if name is invalid here
    if (!isValidName(name))
    {
        error = "Invalid Name: " + name + "";
        return false;
    }

    // reject if name alr exists; or an id is already saved by another name
    for (const auto& [contactId, contactName] : c)
    {
        if (id == contactId && name != contactName)
        {
            error = "ID is already saved under a different name as '" + contactName + "'";
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
    c.clear();

    error_code ec;
    if (!filesystem::exists(path, ec))
    {
        return !ec; // no file yet, not an error
    }

    ifstream inFile(path);
    if (!inFile)
    {
        return false;
    }

    string line;
    size_t lineNumber = 0;
    while (getline(inFile, line))
    {
        ++lineNumber;
        const string where = "line " + to_string(lineNumber) + ": ";

        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        size_t spaceIdx = line.find(" ");
        if (spaceIdx == string::npos)
        {
            warnings.push_back(where + "expected '<hex id> < name'");
            continue;
        }

        string stringId = line.substr(0, spaceIdx);
        string name = line.substr(spaceIdx + 1);

        PeerId id;
        if (!hexToId(stringId, id))
        {
            warnings.push_back(where + "Invalid ID");
            continue;
        }

        string error;
        if (!addContact(c, id, name, error))
        {
            warnings.push_back(where + error);
        }
    }

    return !inFile.bad();
}


bool saveContacts(const string& path, const Contacts& c)
{
    const string tmpPath = path + ".tmp";
    error_code ec;

    {
        ofstream outFile(tmpPath, ios::trunc);
        if (!outFile)
        {
            return false;
        }

        filesystem::permissions(tmpPath, filesystem::perms::owner_read | filesystem::perms::owner_write, filesystem::perm_options::replace, ec);
        if (ec)
        {
            filesystem::remove(tmpPath, ec);
            return false;
        }

        for (const auto& [id, name] : c)
        {
            outFile << idToHex(id) << ' ' << name << '\n';
        }

        outFile.close();
        if (outFile.fail())
        {
            filesystem::remove(tmpPath, ec);
            return false;
        }
    }

    filesystem::rename(tmpPath, path, ec);
    if (ec)
    {
        filesystem::remove(tmpPath, ec);
        return false;
    }

    return true; 
}


