#include <cstdio>
#include <cstring>

#include "attadipa/version.h"

int main()
{
    if (std::strlen(ATTADIPA_VERSION_STRING) == 0) {
        std::fprintf(stderr, "version string is empty\n");
        return 1;
    }

    std::printf("Attadipa %s — host toolchain OK\n", ATTADIPA_VERSION_STRING);
    return 0;
}
