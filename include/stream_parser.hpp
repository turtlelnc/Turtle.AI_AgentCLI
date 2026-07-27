#ifndef STREAM_PARSER_HPP
#define STREAM_PARSER_HPP

#include "http_client.hpp"

#include <string>
#include <vector>

namespace opencode {

ChatResponse parseStreamingEvents(
    const std::vector<std::string>& events,
    const std::string& provider_type
);

} // namespace opencode

#endif // STREAM_PARSER_HPP
