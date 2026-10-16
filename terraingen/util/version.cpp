#include "terraingen/util/version.h"

namespace terraingen {

Version GetVersion() {
    return Version{TERRAINGEN_VERSION_MAJOR, TERRAINGEN_VERSION_MINOR, TERRAINGEN_VERSION_PATCH};
}

int GetApiVersion() {
    return TERRAINGEN_API_VERSION;
}

}  // namespace terraingen
