#ifndef ASTL_API_TARGET_H_
#define ASTL_API_TARGET_H_

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "astl/astl.h"
#include "counter.hpp"

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
  virtual auto GetCounterCount() const -> size_t                                         = 0;
  virtual auto GetCounters() const -> const std::vector<std::unique_ptr<ICounter>>&      = 0;

  virtual CollectorType GetCollectorType() const = 0;
};

/**
 * @brief A partial implementation of the ITarget interface that holds data to fill a astl_target_properties_t struct
 */
class Target : public astl::ITarget {
 public:
  Target() = default;
  Target(std::string name, std::string description, CollectorType collector_type, Target* parent = nullptr);
  ~Target() override               = default;
  Target(const Target&)            = default;
  Target& operator=(const Target&) = default;
  Target(Target&&)                 = default;
  Target& operator=(Target&&)      = default;

  auto GetProperties(astl_target_properties_t* target) const -> astl_status_code override;
  auto Name() const -> std::string const& override;
  auto GetCollectorType() const -> CollectorType override;
  auto GetParent() const -> const Target*;
  auto GetCounterCount() const -> size_t override;
  auto GetCounters() const -> const std::vector<std::unique_ptr<ICounter>>& override;

 private:
  std::string                            _name;
  std::string                            _description;
  CollectorType                          _collector_type{CollectorType::UNKNOWN};
  Target*                                _parent{nullptr};
  std::vector<std::unique_ptr<ICounter>> _counters;
};

}  // namespace astl

#endif  // ASTL_API_TARGET_H_
