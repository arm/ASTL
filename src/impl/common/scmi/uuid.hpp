#ifndef ASTL_SCMI_UUID_HPP_
#define ASTL_SCMI_UUID_HPP_

#include <algorithm>
#include <string>

namespace astl::scmi::spec {

using Uuid = std::string;

/**
 * @brief normalize a UUID string to lowercase, without hyphens, without leading 0x and leading trailing spaces.
 */
inline auto GetNormalizedUuid(std::string const& input_uuid) -> Uuid {
  Uuid uuid = input_uuid;
  // remove leading/trailing spaces
  constexpr char const* whitespace = " \t\n\r\f\v";
  uuid.erase(0, uuid.find_first_not_of(whitespace));
  uuid.erase(uuid.find_last_not_of(whitespace) + 1);
  // remove leading 0x if present
  if (uuid.rfind("0x", 0) == 0 || uuid.rfind("0X", 0) == 0) {
    uuid = uuid.substr(2);
  }
  // remove hyphens
  uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
  // convert to lowercase
  std::transform(uuid.begin(), uuid.end(), uuid.begin(), ::tolower);
  return uuid;
};

}  // namespace astl::scmi::spec

#endif  // ASTL_SCMI_UUID_HPP_
