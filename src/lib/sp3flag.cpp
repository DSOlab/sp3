#include "sp3flag.hpp"

/// Create a copy of the input Sp3FlagWrapper e1 with the e2 event turned on
dso::sp3_details::Sp3FlagWrapper dso::operator|(dso::sp3_details::Sp3FlagWrapper e1,
                                        dso::Sp3Event e2) noexcept {
  dso::sp3_details::Sp3FlagWrapper wf;
  wf.bits_ = e1.bits_;
  wf.bits_ |= (1 << static_cast<dso::sp3_details::uitype>(e2));
  return wf;
}

/// Create a Sp3FlagWrapper instance with (only) the events e1 and e2 set
dso::sp3_details::Sp3FlagWrapper dso::operator|(dso::Sp3Event e1,
                                        dso::Sp3Event e2) noexcept {
  dso::sp3_details::Sp3FlagWrapper wf;
  wf.bits_ |= (1 << static_cast<dso::sp3_details::uitype>(e1));
  wf.bits_ |= (1 << static_cast<dso::sp3_details::uitype>(e2));
  return wf;
}

/// First reset all Sp3Events (aka all are turned off). Then turn on the
/// following flags:
/// * Sp3Event::bad_abscent_position,
/// * Sp3Event::bad_abscent_clock,
/// * Sp3Event::bad_abscent_velocity
/// * Sp3Event::bad_abscent_clock_rate
void dso::Sp3Flag::set_defaults() noexcept {
  this->reset();
  set(Sp3Event::bad_abscent_position | Sp3Event::bad_abscent_clock |
      Sp3Event::bad_abscent_velocity | Sp3Event::bad_abscent_clock_rate);
  return;
}
