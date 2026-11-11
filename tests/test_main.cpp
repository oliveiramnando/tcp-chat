#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include <sodium.h>

int main(int argc, char** argv)
{
    if (sodium_init() < 0)
    {
        return 1;
    }

    doctest::Context context(argc, argv);
    return context.run();
}

