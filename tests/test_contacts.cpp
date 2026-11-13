#include "doctest.h"
#include "contacts.hpp"

#include <sodium.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace
{

PeerId makeId(unsigned char fill)
{
    PeerId id{};
    id.fill(fill);
    return id;
}

// A contacts file path in the system temp folder, deleted (with its .tmp) when the test ends.
struct TempContactsFile
{
    string path;

    TempContactsFile()
    {
        unsigned char random[8];
        randombytes_buf(random, sizeof(random));
        char hex[sizeof(random) * 2 + 1];
        sodium_bin2hex(hex, sizeof(hex), random, sizeof(random));
        path = (fs::temp_directory_path() / ("tcpchat_contacts_test_" + string(hex))).string();
    }

    ~TempContactsFile()
    {
        error_code ec;
        fs::remove(path, ec);
        fs::remove(path + ".tmp", ec);
    }

    // Overwrites the file with exactly this text.
    void write(const string& text) const
    {
        ofstream out(path, ios::binary | ios::trunc);
        out << text;
    }
};

} // namespace

// ---------- idToHex / hexToId ----------

TEST_CASE("idToHex gives 64 lowercase hex characters")
{
    string hex = idToHex(makeId(0xAB));
    CHECK(hex.size() == 64);
    CHECK(hex.find_first_not_of("ab") == string::npos);
    CHECK(hex.substr(0, 4) == "abab");
}

TEST_CASE("idToHex and hexToId round trip")
{
    PeerId original{};
    randombytes_buf(original.data(), original.size());

    PeerId decoded{};
    REQUIRE(hexToId(idToHex(original), decoded));
    CHECK(decoded == original);
}

TEST_CASE("hexToId accepts uppercase hex")
{
    PeerId decoded{};
    REQUIRE(hexToId(string(64, 'F'), decoded));
    CHECK(decoded == makeId(0xFF));
}

TEST_CASE("hexToId rejects anything that isn't exactly 64 hex characters")
{
    const string good = idToHex(makeId(0x12));
    PeerId id = makeId(0x77);

    CHECK_FALSE(hexToId("", id));
    CHECK_FALSE(hexToId(good.substr(1), id));            // 63 characters
    CHECK_FALSE(hexToId(good + "00", id));               // 66 characters
    CHECK_FALSE(hexToId("zz" + good.substr(2), id));     // not hex
    CHECK_FALSE(hexToId(good.substr(0, 32) + " " + good.substr(33), id));   // space inside

    CHECK(id == makeId(0x77));   // a failed decode leaves the output untouched
}

// ---------- findByName ----------

TEST_CASE("findByName finds saved names only")
{
    Contacts c = {{makeId(0x01), "bob"}, {makeId(0x02), "carol"}};

    auto found = findByName(c, "carol");
    REQUIRE(found.has_value());
    CHECK(*found == makeId(0x02));

    CHECK_FALSE(findByName(c, "dave").has_value());
    CHECK_FALSE(findByName(c, "Bob").has_value());     // names are case-sensitive
    CHECK_FALSE(findByName(Contacts{}, "bob").has_value());
}

// ---------- addContact ----------

TEST_CASE("addContact enforces the contact rules")
{
    Contacts c;
    string error;

    REQUIRE(addContact(c, makeId(0x01), "bob", error));
    CHECK(c.size() == 1);

    SUBCASE("a second contact with a different ID and name is fine")
    {
        CHECK(addContact(c, makeId(0x02), "carol", error));
        CHECK(c.size() == 2);
    }

    SUBCASE("adding the same ID with the same name again changes nothing")
    {
        CHECK(addContact(c, makeId(0x01), "bob", error));
        CHECK(c.size() == 1);
    }

    SUBCASE("a name already used by another ID is rejected")
    {
        error.clear();
        CHECK_FALSE(addContact(c, makeId(0x02), "bob", error));
        CHECK_FALSE(error.empty());
        CHECK(c.size() == 1);
    }

    SUBCASE("an ID already saved under another name is rejected")
    {
        error.clear();
        CHECK_FALSE(addContact(c, makeId(0x01), "robert", error));
        CHECK_FALSE(error.empty());
        CHECK(c.at(makeId(0x01)) == "bob");
    }

    SUBCASE("invalid names are rejected")
    {
        for (const string& bad : {string(""), string("a b"), string("../x"), string(33, 'a')})
        {
            CAPTURE(bad);
            error.clear();
            CHECK_FALSE(addContact(c, makeId(0x02), bad, error));
            CHECK_FALSE(error.empty());
        }
        CHECK(c.size() == 1);
    }
}

// ---------- saveContacts / loadContacts ----------

TEST_CASE("loadContacts treats a missing file as an empty list")
{
    TempContactsFile file;   // never written
    Contacts c = {{makeId(0x09), "stale"}};
    vector<string> warnings;

    REQUIRE(loadContacts(file.path, c, warnings));
    CHECK(c.empty());        // previous contents are cleared
    CHECK(warnings.empty());
}

TEST_CASE("saveContacts then loadContacts gives back the same contacts")
{
    TempContactsFile file;
    Contacts saved = {{makeId(0x01), "bob"}, {makeId(0x02), "carol"}, {makeId(0x03), "dave_2"}};

    REQUIRE(saveContacts(file.path, saved));

    Contacts loaded;
    vector<string> warnings;
    REQUIRE(loadContacts(file.path, loaded, warnings));
    CHECK(loaded == saved);
    CHECK(warnings.empty());
}

TEST_CASE("saveContacts writes the documented format")
{
    TempContactsFile file;
    REQUIRE(saveContacts(file.path, {{makeId(0xAB), "bob"}}));

    ifstream in(file.path);
    string line;
    REQUIRE(getline(in, line));
    CHECK(line == idToHex(makeId(0xAB)) + " bob");
    CHECK_FALSE(getline(in, line));   // exactly one line
}

TEST_CASE("saveContacts makes the file owner-only and leaves no .tmp behind")
{
    TempContactsFile file;
    REQUIRE(saveContacts(file.path, {{makeId(0x01), "bob"}}));

    fs::perms p = fs::status(file.path).permissions();
    CHECK((p & fs::perms::owner_read) != fs::perms::none);
    CHECK((p & fs::perms::owner_write) != fs::perms::none);
    CHECK((p & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);

    CHECK_FALSE(fs::exists(file.path + ".tmp"));
}

TEST_CASE("saveContacts replaces the whole file")
{
    TempContactsFile file;
    REQUIRE(saveContacts(file.path, {{makeId(0x01), "bob"}, {makeId(0x02), "carol"}}));
    REQUIRE(saveContacts(file.path, {{makeId(0x02), "carol"}}));   // bob removed

    Contacts loaded;
    vector<string> warnings;
    REQUIRE(loadContacts(file.path, loaded, warnings));
    CHECK(loaded == Contacts{{makeId(0x02), "carol"}});
}

TEST_CASE("saveContacts fails cleanly when the folder doesn't exist")
{
    TempContactsFile file;
    string badPath = file.path + "_missing_dir/contacts";

    CHECK_FALSE(saveContacts(badPath, {{makeId(0x01), "bob"}}));
    CHECK_FALSE(fs::exists(badPath));
}

TEST_CASE("loadContacts skips comments, blank lines and Windows line endings")
{
    TempContactsFile file;
    file.write("# my contacts\n"
               "\n" +
               idToHex(makeId(0x01)) + " bob\r\n" +
               "   \n" +                                   // whitespace only: not blank, so a warning
               idToHex(makeId(0x02)) + " carol\n");

    Contacts c;
    vector<string> warnings;
    REQUIRE(loadContacts(file.path, c, warnings));
    CHECK(c == Contacts{{makeId(0x01), "bob"}, {makeId(0x02), "carol"}});
    CHECK(warnings.size() == 1);
}

TEST_CASE("loadContacts keeps good lines and warns about each bad one")
{
    TempContactsFile file;
    const string bob = idToHex(makeId(0x01));
    const string carol = idToHex(makeId(0x02));

    file.write(bob + " bob\n" +                       // 1: good
               "no_space_here\n" +                    // 2: no separator
               "nothex " + "dave\n" +                 // 3: bad ID
               bob.substr(1) + " short\n" +           // 4: ID too short
               carol + " ../evil\n" +                 // 5: invalid name
               bob + " bobby\n" +                     // 6: ID already saved as bob
               carol + " bob\n" +                     // 7: name already used
               carol + " carol\n");                   // 8: good

    Contacts c;
    vector<string> warnings;
    REQUIRE(loadContacts(file.path, c, warnings));

    CHECK(c == Contacts{{makeId(0x01), "bob"}, {makeId(0x02), "carol"}});
    REQUIRE(warnings.size() == 6);

    // Every warning names its line, so the user can find it.
    const vector<string> expectedLines = {"line 2:", "line 3:", "line 4:", "line 5:", "line 6:", "line 7:"};
    for (size_t i = 0; i < warnings.size(); ++i)
    {
        CAPTURE(warnings[i]);
        CHECK(warnings[i].rfind(expectedLines[i], 0) == 0);
    }
}
