
#ifndef CONTACTS_HPP
#define CONTACTS_HPP

#include <string>

#include "net.hpp"

std::string idToHex(const PeerId& id);
bool hexToId(const std::string& hex, PeerId& id);


#endif


