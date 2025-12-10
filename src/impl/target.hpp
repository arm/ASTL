#ifndef ASTL_API_TARGET_H_
#define ASTL_API_TARGET_H_

#include <optional>
#include <string>

#include "astl/astl.h"
#include "common/capabilities.hpp"

namespace astl {

/**
 * @brief The C++ interface representing an astl_target_handle_t
 */
struct ITarget {
  virtual ~ITarget()                 = default;
  ITarget()                          = default;
  ITarget(const ITarget&)            = default;
  ITarget& operator=(const ITarget&) = default;
  ITarget(ITarget&&)                 = default;
  ITarget& operator=(ITarget&&)      = default;

  virtual auto GetProperties(astl_target_properties_t* target) const -> astl_status_code = 0;
  virtual auto Name() const -> std::string const&                                        = 0;

  virtual auto GetCollectorType() const -> CollectorType = 0;
};

/**
 * @brief A partial implementation of the ITarget interface that holds data to fill a astl_target_properties_t struct
 */
class Target : public astl::ITarget {
 public:
  Target() = default;
  Target(std::string name, std::string description, CollectorType collector_type, Target* parent = nullptr,
         std::optional<std::string> uuid = std::nullopt);
  ~Target() override               = default;
  Target(const Target&)            = default;
  Target& operator=(const Target&) = default;
  Target(Target&&)                 = default;
  Target& operator=(Target&&)      = default;

  auto GetProperties(astl_target_properties_t* target) const -> astl_status_code override;
  auto Name() const -> std::string const& override;
  auto GetCollectorType() const -> CollectorType override;
  auto GetParent() const -> const Target*;
  auto GetUuid() const -> const std::optional<std::string>& { return _uuid; }

 private:
  std::string                _name;
  std::string                _description;
  CollectorType              _collector_type{CollectorType::UNKNOWN};
  Target*                    _parent{nullptr};
  std::optional<std::string> _uuid;
};

}  // namespace astl

#endif  // ASTL_API_TARGET_H_
