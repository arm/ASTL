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

  virtual astl_status_code                              GetProperties(astl_target_properties_t* target) = 0;
  virtual size_t                                        GetCounterCount() const                         = 0;
  virtual const std::vector<std::unique_ptr<ICounter>>& GetCounters() const                             = 0;

  virtual astl_status_code ConfigureCounterCollection(astl_collection_parameters_t const* collection_params,
                                                      std::span<ICounter*>                counters) = 0;

  virtual astl_status_code ReadImmediate() = 0;

  virtual astl_status_code StartCollection() = 0;

  virtual astl_status_code PauseCollection() = 0;

  virtual astl_status_code ResumeCollection() = 0;

  virtual astl_status_code StopCollection() = 0;

  virtual std::expected<uint32_t, astl_status_code> GetCounterSampleCount(const ICounter* counter) const = 0;
};

/**
 * @brief A partial implementation of the ITarget interface that holds data to fill a astl_target_properties_t struct
 */
class Target : public ITarget {
 public:
  Target()                         = default;
  ~Target() override               = default;
  Target(const Target&)            = default;
  Target& operator=(const Target&) = default;
  Target(Target&&)                 = default;
  Target& operator=(Target&&)      = default;

  astl_status_code                              GetProperties(astl_target_properties_t* target) override;
  size_t                                        GetCounterCount() const override { return _counters.size(); }
  const std::vector<std::unique_ptr<ICounter>>& GetCounters() const override { return _counters; }

 private:
  Target*                                _parent = nullptr;
  std::string                            _name;
  std::string                            _description;
  std::vector<std::unique_ptr<ICounter>> _counters;
};

}  // namespace astl

#endif  // ASTL_API_TARGET_H_
