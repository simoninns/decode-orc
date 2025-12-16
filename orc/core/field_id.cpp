#include "field_id.h"
#include <sstream>

namespace orc {

std::string FieldID::to_string() const {
    if (!is_valid()) {
        return "FieldID::INVALID";
    }
    std::ostringstream oss;
    oss << "FieldID(" << value_ << ")";
    return oss.str();
}

} // namespace orc
