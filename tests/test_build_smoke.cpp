#include <cstdio>
#include <cstring>

#include "firefly/version.h"

int main()
{
    if (std::strlen(FIREFLY_VERSION_STRING) == 0) {
        std::fprintf(stderr, "version string is empty\n");
        return 1;
    }

    std::printf("Firefly OS %s — host toolchain OK\n", FIREFLY_VERSION_STRING);
    return 0;
}
