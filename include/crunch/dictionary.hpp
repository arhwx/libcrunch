#pragma once

#include <crunch/error.hpp>
#include <crunch/format.hpp>

#include <cstddef>

namespace crunch {

// inputs without the magic are raw content dictionaries (5)
error parse_dictionary(const std::byte *data, std::size_t size,
                       dictionary &dict);

} // namespace crunch
