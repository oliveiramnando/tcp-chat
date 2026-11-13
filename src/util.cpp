#include "util.hpp"
#include <cctype>

using namespace std;

// Profile names become folder names, so allow only safe characters.
bool isValidName(const string& name)
{
    if (name.empty() || name.size() > 32)
    {
        return false;
    }
    for (char c : name)
    {
        if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
        {
            return false;
        }
    }
    return true;
}
