
#ifndef CONTACTS_HPP
#define CONTACTS_HPP

#include <string>
#include <map>

#include "net.hpp"

using Contacts = std::map<PeerId, std::string>;

std::string idToHex(const PeerId& id);
bool hexToId(const std::string& hex, PeerId& id);

std::optional<PeerId> findByName(const Contacts& c, const std::string& name, std::string& error);

#endif


