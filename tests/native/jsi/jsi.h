// Only the abstract storage interface is needed by the platform-independent tests.
// Production builds use React Native's JSI header and implementation.
#pragma once
#include <cstddef>
#include <cstdint>
namespace facebook::jsi {
class MutableBuffer {
 public:
  virtual ~MutableBuffer() = default;
  virtual size_t size() const = 0;
  virtual uint8_t *data() = 0;
};
}
