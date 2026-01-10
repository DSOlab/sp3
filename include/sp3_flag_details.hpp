/** @file
 * Two things are defined in thsi file:
 * 1. An Sp3Event enum class, that can designate any event recorded in an
 *    Sp3 file
 * 2. An Sp3FlagWrapper class, that can assist the handling of multiple
 *    Sp3Event's
 *
 * Both of the two definitions will help in the Sp3Flag class, defined in
 * (yet) another file.
 */

#ifndef __SP3C_IGS_FLAG_DETAILS__
#define __SP3C_IGS_FLAG_DETAILS__

#include <cstdint>
#include <limits>

namespace dso {

namespace sp3_details {
/** @brief Underlying type for Sp3Event Enum class */
using uitype = uint_fast16_t;

/** @class Sp3FlagWrapper
 * This is just an Sp3Flag instance, with limited use; it mainly acts as
 * a temporary Sp3Flag for easy interactions between Sp3Event's and an
 * actual Sp3Flag.
 */
struct Sp3FlagWrapper {
  uitype bits_{0};
}; /* struct Sp3FlagWrapper */

} // namespace sp3_details

/** @enum Sp3Event Describe an event that can be recorded in an Sp3 file. A
 * record may be marked with multiple (or none) Sp3Event's.
 */
enum class Sp3Event : sp3_details::uitype {
  /** Bad or absent positional values are to be set to 0.000000 */
  bad_abscent_position = 0,

  /** Bad or absent clock values are set to _999999.999999.  The six integer
   * nines are required, whereas the fractional part nines are optional.
   */
  bad_abscent_clock,

  /** Column 75 is the Clock Event Flag (either 'E' or blank).  An 'E' flag is
   * used to denote a discontinuity in the satellite clock correction (this
   * might be caused by a clock swap on the satellite). The discontinuity is
   * understood to have occurred sometime between the previous epoch and
   * current epoch, or at the current epoch. A blank means either no event
   * occurred, or it is unknown whether any event occurred.
   */
  clock_event,

  /** Column 76 is theClock Correction Prediction Flag (either 'P' or blank).
   * A 'P' flagindicates that the satellite clock correction at this epoch is
   * predicted.A blank means that the clock correction is observed.
   */
  clock_prediction,

  /** Column 79 is theorbit Maneuver Flag (either 'M' or blank).  An 'M' flag
   * indicates thatsometime between the previous epoch and the current epoch,
   * or at the currentepoch, an orbit maneuver took place for this satellite.
   * As an example, if a certain maneuver lasted 50 minutes (a satellite
   * changing orbital planes)then these M-flags could conceivably appear at
   * five separate 15-minute orbitepochs. If the maneuver started at 11h 14m
   * and lasted to 12h 04m, M-flagswould appear for the epochs 11:15, 11:30,
   * 11:45, 12:00 and 12:15.  Amaneuver is loosely defined as any planned or
   * humanly-detectable thrusterfiring that changes the orbit of a satellite.
   * A blank means either nomaneuver occurred, or it is unknown whether any
   * maneuver occurred.
   */
  maneuver,

  /** Column 80 is the Orbit Prediction Flag (either 'P' or blank).  A 'P'
   * flag indicates that the satellite position at this epoch is predicted. A
   * blank means thatthe satellite position is observed.
   */
  orbit_prediction,

  /** Record has valid position std. deviation records */
  has_pos_stddev,

  /** Reocord has valid clock std. deviation records */
  has_clk_stddev,

  /** Bad or absent velocity (positional) values are to be set to 0.000000 */
  bad_abscent_velocity,

  /** Bad or absent clock rate values */
  bad_abscent_clock_rate,

  /** Record has valid position std. deviation records */
  has_vel_stddev,

  /** Record has valid clock std. deviation records */
  has_clk_rate_stdev
}; /* enum (class) Sp3Event */

static_assert(std::numeric_limits<sp3_details::uitype>::digits >
              static_cast<sp3_details::uitype>(Sp3Event::has_clk_rate_stdev));

/** @brief set two events (aka turn them 'on') in a Sp3FlagWrapper
 *
 * What we want here, is a functionality of:
 * Sp3Flag flag;
 * flag.set(Sp3Event::bad_abscent_position|Sp3Event::bad_abscent_clock|
 *          Sp3Event::clock_event|Sp3Event::has_clk_rate_stdev);
 * This here is a first step ....
 * @see  Sp3FlagWrapper operator|(Sp3FlagWrapper e1, Sp3Event e2)
 *
 * @param[in] e1 Event to turn 'on' aka set
 * @param[in] e2 Event to turn 'on' aka set
 * @return An Sp3FlagWrapper that only has the two bits e1 and e2 on.
 *            Everything else is set to 0. This function acts on a bit
 *            level.
 */
sp3_details::Sp3FlagWrapper operator|(Sp3Event e1, Sp3Event e2) noexcept;

/** @brief Concatenate an Sp3FlagWrapper and an Sp3Event.
 *
 * The function will copy the input Sp3FlagWrapper and set on the
 * e2 event (aka set the e2 bit 'on').
 * What we want here, is a functionality of:
 * Sp3Flag flag;
 * flag.set(Sp3Event::bad_abscent_position|Sp3Event::bad_abscent_clock|
 *          Sp3Event::clock_event|Sp3Event::has_clk_rate_stdev);
 * This here is the final step ....
 * @see Sp3FlagWrapper operator|(Sp3Event e1, Sp3Event e2)
 *
 * @param[in] e1 An Sp3FlagWrapper
 * @param[in] e2 An event to turn on in the resulting Sp3FlagWrapper
 * @return an Sp3FlagWrapper with every 'on' bit of e1 turned on and
 *            the e2 bit turned on.
 */
sp3_details::Sp3FlagWrapper operator|(sp3_details::Sp3FlagWrapper e1,
                                      Sp3Event e2) noexcept;

} /* namespace dso */

#endif
