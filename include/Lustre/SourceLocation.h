#pragma once

#include <cstdint>
#include <string>

namespace Lustre {

struct SourceLocation {
    std::string   FilePath;
    std::uint32_t Line{1};
    std::uint32_t Column{1};
};

} // namespace Lustre
