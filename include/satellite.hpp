/** @file
 * Define a struct (i.e. SatelliteId) to represent any possible satellite that
 * may came up in an Sp3 file. We use a 3-character identifier for this.
 */

#ifndef __SP3C_SATELLITE_FILE__
#define __SP3C_SATELLITE_FILE__

#include "datetime/calendar.hpp"
#include <cstring>

namespace dso::sp3_details {

/** Number of characters used to describe a given sattelite vehicle */
constexpr int SAT_ID_CHARS = 3;

/** Number of characters used to describe a given sattelite vehicle as a
 * null terminating string
 */
constexpr int SAT_ID_MAX_CHARS = SAT_ID_CHARS + 1;

/** @class Satellite ID as denoted in an Sp3 file */
struct SatelliteId {
  /** The id of the space vehicle (3chars plus the null terminating char) */
  char id[SAT_ID_MAX_CHARS] = {'\0'};

  /** @brief Constructor from a c-string.
   * This will only copy the first SAT_ID_CHARS (from the input string) to
   * the instance's id
   */
  explicit SatelliteId(const char *str = nullptr) noexcept {
    if (str)
      set_id(str);
  }

  /** @brief Set id from a c-string.
   * This will only copy the first SAT_ID_CHARS (from the input string) to
   * the instance's id
   */
  void set_id(const char *str) noexcept { std::memcpy(id, str, SAT_ID_CHARS); }

  /** @brief Compare satellite id's */
  bool operator==(const SatelliteId &s) const noexcept {
    return !std::strncmp(id, s.id, SAT_ID_CHARS);
  }

  /** @brief Compare satellite id's */
  bool operator!=(const SatelliteId &s) const noexcept {
    return !this->operator==(s);
  }

  /** @brief Cast the id to an std::string */
  std::string to_string() const noexcept {
    return std::string(id, SAT_ID_CHARS);
  }
};

/* we need atelliteId to be trivially_copy_assignable */
static_assert(std::is_trivially_copy_assignable_v<SatelliteId>);

} /* namespace dso::sp3_details */
#endif
