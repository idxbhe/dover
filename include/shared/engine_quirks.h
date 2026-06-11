#pragma once

#include <string>

namespace dover::shared {

struct EngineQuirks {
    bool dx9_force_backbuffer_render = false;
    bool dx9_disable_state_blocks = false;
};

const EngineQuirks& GetEngineQuirks();
void InitializeEngineQuirks();

} // namespace dover::shared
