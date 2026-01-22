#pragma once
#include <orc_types.h>
// Adjust path to core/include/project.h as needed
#include "../../core/include/project.h"

namespace orc::public_api {
    inline orc::Project* toCore(OrcProjectHandle h) {
        return reinterpret_cast<orc::Project*>(h);
    }
    inline OrcProjectHandle toHandle(orc::Project* p) {
        return reinterpret_cast<OrcProjectHandle>(p);
    }
}
