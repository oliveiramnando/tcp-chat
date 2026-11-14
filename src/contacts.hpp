
#ifndef CONTACTS_HPP
#define CONTACTS_HPP

#include <string>
#include <map>
#include <vector>
#include <optional>

#include "net.hpp"

using Contacts = std::map<PeerId, std::string>;
constexpr char CONTACTS_FILE[] = "contacts";

std::string idToHex(const PeerId& id);
bool hexToId(const std::string& hex, PeerId& id);

std::string displayName(const Contacts& c, const PeerId& id);

std::optional<PeerId> findByName(const Contacts& c, const std::string& name);

bool addContact(Contacts& c, const PeerId& id, const std::string& name, std::string& error);

bool loadContacts(const std::string& path, Contacts& c, std::vector<std::string>& warnings);
bool saveContacts(const std::string& path, const Contacts& c);

#endif


