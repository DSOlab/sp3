#include "sp3.hpp"
#include <charconv>
#include <cstdio>
#include <stdexcept>
#ifdef DEBUG
#include "datetime/datetime_write.hpp"
#include <iostream>
#endif

using dso::sp3_details::SatelliteId;

namespace {
/* Max record characters (for a navigation data block) */
constexpr int MAX_RECORD_CHARS{128};

/* Bad or absent clock values areto be set to 999999.999999.  The six integer
 * nines are required, whereasthe fractional part nines are optional.
 */
constexpr double SP3_MISSING_CLK_VALUE{999999.e0};

/** @brief Check if given substring is empty (aka whitespace only).
 *  Substring to check is str with indexes [0,count)
 *  @param[in] str Start of string
 *  @param[in] count Number of chars to consider
 *  @return true id substring is whitespace only, false otherwise
 */
// bool substr_is_empty(const char *str, std::size_t count) noexcept {
//   std::size_t idx = 0;
//   while (idx < count && str[idx] == ' ')
//     ++idx;
//   return idx == count;
// }

const char *skipws(const char *str) noexcept {
  while (*str && *str == ' ')
    ++str;
  return str;
}
} /* anonymous namespace */

/** @brief Resolve an Epoch Header Record line
 *  @param[in] line An Epoch Header Record to be resolved
 *  @param[out] t The epoch resolved from the input line
 *  @return Anything other than 0 denotes an error
 */
int dso::Sp3c::resolve_epoch_line(dso::datetime<dso::nanoseconds> &t) noexcept {
  char line[MAX_RECORD_CHARS];

  istream_.getline(line, MAX_RECORD_CHARS);
  if (line[0] != '*' || line[1] != ' ') {
    fprintf(stderr, "ERROR. Failed resolving epoch line [%s] (%s)\n", line,
            __func__);
    return 2;
  }

  int date[5];
  int error = 0;
  const auto sz = std::strlen(line);
  const char *s1 = line + 1, *s2 = line + sz;
  for (int i = 0; i < 5; i++) {
    auto res = std::from_chars(skipws(s1), s2, date[i]);
    error += (res.ec != std::errc{});
    s1 = res.ptr;
  }

  if (error) {
    fprintf(stderr,
            "[ERROR] Failed resolving (integer) date from line: %s (traceback: "
            "%s)\n",
            line, __func__);
    return 1;
  }

  double fsec;
  auto res = std::from_chars(skipws(s1), s2, fsec, std::chars_format::fixed);
  error += (res.ec != std::errc{});

  if (error) {
    fprintf(stderr,
            "[ERROR] Failed resolving (sec of) date from line: %s (traceback: "
            "%s)\n",
            line, __func__);
    return 1;
  }

  t = dso::datetime<dso::nanoseconds>(
      dso::year(date[0]), dso::month(date[1]), dso::day_of_month(date[2]),
      dso::hours(date[3]), dso::minutes(date[4]),
      dso::nanoseconds(
          static_cast<long>(fsec * dso::nanoseconds::sec_factor<double>())));

  return 0;
}

int dso::Sp3c::get_next_block(
    dso::sp3_details::Sp3DataBlock &datablock,
    const dso::sp3_details::SatelliteId *sat) noexcept {
  char line[MAX_RECORD_CHARS];
  datablock.clear();

  /* possible following lines (three first chars):
   * 1. '*  ' i.e an epoch header
   * 2. 'PXX' i.e. a position & clock line, e.g. 'PG01 ....'
   * 3. 'EP ' i.e. position and clock correlation
   * 4. 'VXX' i.e. velocity line, e.g. 'VG01 ...'
   * 5. 'EV ' i.e. velocity correlation
   * 6. 'EOF' i.e. EOF
   */

  /* current epoch (of block) */
  dso::datetime<dso::nanoseconds> t;

  /* get current time; if -1 is returned, we reached EOF */
  int error = this->peak_next_data_block(t);
  if (error)
    return error;

  // keep on reading reacords .....
  bool keep_reading = true;
  char c;
  dso::sp3_details::SatelliteId satid;
  double state[8];      /** [ X, Y, Z, clk, Vx, Vy, Vz, Vc ] */
  double state_sdev[8]; /** following state__ */
  dso::Sp3Flag flag;

  do {
    c = istream_.peek();
    if (c == '*') {
      keep_reading = false;
      break;
    } else if (c == 'P') {
      /* position line; resolve it if sats match */
      error = get_next_position(satid, state[0], state[1], state[2], state[3],
                                state_sdev[0], state_sdev[1], state_sdev[2],
                                state_sdev[3], flag, sat);
      if (error > 0)
        return error;
      if (!error) {
        auto it = datablock.sat_block(satid);
        if (it == datablock.blocks_.end()) {
          /* entry for new satellite */
          datablock.blocks_.emplace_back(satid, flag, state, state_sdev);
        } else {
          it->update_position(flag, state, state_sdev);
        }
      }
    } else if (c == 'V') {
      error = get_next_velocity(satid, state[4], state[5], state[6], state[7],
                                state_sdev[4], state_sdev[5], state_sdev[6],
                                state_sdev[7], flag, sat);
      if (error > 0)
        return error;
      if (!error) {
        auto it = datablock.sat_block(satid);
        if (it == datablock.blocks_.end()) {
          /* entry for new satellite */
          datablock.blocks_.emplace_back(satid, flag, state, state_sdev);
        } else {
          it->update_velocity(flag, state, state_sdev);
        }
      }
    } else {
      istream_.getline(line, MAX_RECORD_CHARS);
      if (!std::strncmp(line, "EOF", 3)) {
        keep_reading = false;
        /*return -1;*/
        error = -1;
      } else if (!std::strncmp(line, "EP", 2)) {
        fprintf(stderr, "[DEBUG] Ingoring Position Correlation Records ...\n");
      } else if (!std::strncmp(line, "EV", 2)) {
        fprintf(stderr, "[DEBUG] Ingoring Velocity Correlation Records ...\n");
      } else {
        return 150;
      }
    }
  } while (keep_reading);

  return error;
}

/** Read in and resolve an Sp3c/d Velocity and ClockRate-of-Change Record. The
 *  function expects that the next line to be read off from the input stream
 *  is a Velocity and ClockRate-of-Change Record line.
 *
 *  @param[out] A 3character satellite id as recorded in the Sp3 file
 *  @param[out] xv  X-component of satelite velocity, in dm/sec
 *  @param[out] yv  y-component of satelite velocity, in dm/sec
 *  @param[out] zv  Z-component of satelite velocity, in dm/sec
 *  @param[out] cv  Clock rate-of-change in 10**-4 microseconds/second
 *  @param[out] xstdv X-component std. deviation in 10**-4 mm/sec
 *  @param[out] ystdv Y-component std. deviation in 10**-4 mm/sec
 *  @param[out] zstdv Z-component std. deviation in 10**-4 mm/sec
 *  @param[out] cstdv Clock std. deviation in 10**-4 psec/sec
 *  @param[out] flag An Sp3Flag instance denoting the status of the resolved
 *              fields. The flag is NOT reset (aka input flags will not be
 *              touched). Any flags to be added, only affect position and clock
 *              rate-of-change records (aka bad_abscent_velocity,
 *              bad_abscent_clock_rate, has_vel_stddev, has_clk_rate_stdev).
 *  @param[in] wsat If provided, then only resolve the data line if the given
 *              SatelliteId wsat matches the one recorded in the line. If the
 *              SatelliteId was not matched, the events bad_abscent_velocity
 *              and bad_abscent_clock_rate are set, and an non-zero integer is
 *              returned.
 *  @return Anything other than 0 denotes an error (note tha error codes must
 *          be >0 and <10)
 */
int dso::Sp3c::get_next_velocity(SatelliteId &sat, double &xv, double &yv,
                                 double &zv, double &cv, double &xstdv,
                                 double &ystdv, double &zstdv, double &cstdv,
                                 Sp3Flag &flag,
                                 const SatelliteId *wsat) noexcept {
  char line[MAX_RECORD_CHARS];

  istream_.getline(line, MAX_RECORD_CHARS);
  if (*line != 'V')
    return 1;

  std::memcpy(sat.id, line + 1, 3);

  if (wsat) {
    if (*wsat != sat) {
      flag.set(Sp3Event::bad_abscent_velocity |
               Sp3Event::bad_abscent_clock_rate);
      return 9;
    }
  }

  /* resolve the 4 floats (vel + clk_rate) */
  int error = 0;
  double dvec[4];
  const auto sz = std::strlen(line);
  const char *s1 = line + 4, *s2 = line + sz;
  for (int i = 0; i < 4; i++) {
    auto res =
        std::from_chars(skipws(s1), s2, dvec[i], std::chars_format::fixed);
    error += (res.ec != std::errc{});
    s1 = res.ptr;
  }

  if (error) {
    fprintf(stderr,
            "[ERROR] Failed resolving sat. velocity from line: %s (traceback: "
            "%s)\n",
            line, __func__);
    return 1;
  }

  xv = dvec[0]; // dm/s
  yv = dvec[1];
  zv = dvec[2];
  cv = dvec[3]; // 10**-4 microseconds/second

  /* check/set flags */
  if (xv == 0e0 || (yv == 0e0 || zv == 0e0))
    flag.set(Sp3Event::bad_abscent_velocity);
  else
    flag.clear(Sp3Event::bad_abscent_velocity);

  if (cv >= SP3_MISSING_CLK_VALUE)
    flag.set(Sp3Event::bad_abscent_clock_rate);
  else
    flag.clear(Sp3Event::bad_abscent_clock_rate);

  /* std deviations (if any) */
  char *end;
  errno = 0;
  int has_pos_stddev = false, has_clk_stddev = false;
  if (sz > 68) {
    if (*(line + 61) != ' ' || *(line + 62) != ' ') {
      int nn = std::strtol(line + 61, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      xstdv = std::pow(fpb_pos_, nn); // 10**-4 mm/sec
      ++has_pos_stddev;
    }
    if (*(line + 64) != ' ' || *(line + 65) != ' ') {
      int nn = std::strtol(line + 64, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      ystdv = std::pow(fpb_pos_, nn);
      ++has_pos_stddev;
    }
    if (*(line + 67) != ' ' || *(line + 68) != ' ') {
      int nn = std::strtol(line + 67, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      zstdv = std::pow(fpb_pos_, nn);
      ++has_pos_stddev;
    }
  }

  if (has_pos_stddev == 3)
    flag.set(Sp3Event::has_vel_stddev);

  if (std::strlen(line) > 71) {
    if (*(line + 70) != ' ' || *(line + 71) != ' ' || *(line + 72) != ' ') {
      int nn = std::strtol(line + 70, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      cstdv = std::pow(fpb_clk_, nn); // 10**-4 psec/sec
      ++has_clk_stddev;
    }
  }

  if (has_clk_stddev == 1)
    flag.set(Sp3Event::has_clk_rate_stdev);

  return 0;
}

/** Read in and resolve an Sp3c/d Position and Clock Record. The function
 *  expects that the next line to be read off from the input stream is a
 *  Position and Clock Record line.
 *
 *  @param[out] A 3character satellite id as recorded in the Sp3 file
 *  @param[out] xkm X-component of satelite position, in km
 *  @param[out] ykm y-component of satelite position, in km
 *  @param[out] zkm Z-component of satelite position, in km
 *  @param[out] clk Clock correction in microsec
 *  @param[out] xstdv X-component std. deviation in mm
 *  @param[out] ystdv Y-component std. deviation in mm
 *  @param[out] zstdv Z-component std. deviation in mm
 *  @param[out] cstdv Clock std. deviation in psec
 *  @param[out] flag An Sp3Flag instance denoting the status of the resolved
 *              fields. Note that the flag will be reset at the function call
 *  @param[in] wsat If provided, then only resolve the data line if the given
 *              SatelliteId wsat matches the one recorded in the line. If the
 *              SatelliteId was not matched, the events bad_abscent_position
 *              and bad_abscent_clock are set. A non-zero integer is returned.
 *  @return Anything other than 0 denotes an error (note tha error codes must
 *          be >0 and <10)
 */
int dso::Sp3c::get_next_position(SatelliteId &sat, double &xkm, double &ykm,
                                 double &zkm, double &clk, double &xstdv,
                                 double &ystdv, double &zstdv, double &cstdv,
                                 Sp3Flag &flag,
                                 const SatelliteId *wsat) noexcept {
  char line[MAX_RECORD_CHARS];

  istream_.getline(line, MAX_RECORD_CHARS);
  if (*line != 'P')
    return 1;

  std::memcpy(sat.id, line + 1, 3);

  if (wsat) {
    if (*wsat != sat) {
      flag.set(Sp3Event::bad_abscent_position | Sp3Event::bad_abscent_clock);
      return 9;
    }
  }

  /* resolve the 4 floats (pos + clk_bias) */
  int error = 0;
  double dvec[4];
  const auto sz = std::strlen(line);
  const char *s1 = line + 4, *s2 = line + sz;
  for (int i = 0; i < 4; i++) {
    auto res =
        std::from_chars(skipws(s1), s2, dvec[i], std::chars_format::fixed);
    error += (res.ec != std::errc{});
    s1 = res.ptr;
  }

  if (error) {
    fprintf(stderr,
            "[ERROR] Failed resolving sat. position from line: %s (traceback: "
            "%s)\n",
            line, __func__);
    return 1;
  }

  xkm = dvec[0];
  ykm = dvec[1];
  zkm = dvec[2];

  if (xkm == 0e0 || ykm == 0e0 || zkm == 0e0)
    flag.set(Sp3Event::bad_abscent_position);
  else
    flag.clear(Sp3Event::bad_abscent_position);

  clk = dvec[3];
  if (clk >= SP3_MISSING_CLK_VALUE)
    flag.set(Sp3Event::bad_abscent_clock);
  else
    flag.clear(Sp3Event::bad_abscent_clock);

  /* std deviations (if any) */
  char *end;
  errno = 0;
  int has_pos_stddev = false, has_clk_stddev = false;
  if (std::strlen(line) > 68) {
    if (*(line + 61) != ' ' || *(line + 62) != ' ') {
      int nn = std::strtol(line + 61, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      xstdv = std::pow(fpb_pos_, nn);
      ++has_pos_stddev;
    }
    if (*(line + 64) != ' ' || *(line + 65) != ' ') {
      int nn = std::strtol(line + 64, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      ystdv = std::pow(fpb_pos_, nn);
      ++has_pos_stddev;
    }
    if (*(line + 67) != ' ' || *(line + 68) != ' ') {
      int nn = std::strtol(line + 67, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      zstdv = std::pow(fpb_pos_, nn);
      ++has_pos_stddev;
    }
  }
  if (has_pos_stddev == 3)
    flag.set(Sp3Event::has_pos_stddev);

  if (std::strlen(line) > 71) {
    if (*(line + 70) != ' ' || *(line + 71) != ' ' || *(line + 72) != ' ') {
      int nn = std::strtol(line + 70, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      cstdv = std::pow(fpb_clk_, nn);
      ++has_clk_stddev;
    }
  }
  if (has_clk_stddev == 1)
    flag.set(Sp3Event::has_clk_stddev);

  if (line[74] == 'E')
    flag.set(Sp3Event::clock_event);
  if (line[75] == 'P')
    flag.set(Sp3Event::clock_prediction);
  if (line[78] == 'M')
    flag.set(Sp3Event::maneuver);
  if (line[79] == 'E')
    flag.set(Sp3Event::orbit_prediction);

  return 0;
}

/** @details Sp3c constructor, using a filename. The constructor will
 *           initialize (set) the _filename attribute and also (try to)
 *           open the input stream (i.e. _istream).
 *           If the file is successefuly opened, the constructor will read
 *           the header and assign info.
 *  @param[in] filename  The filename of the Sp3 file
 */
dso::Sp3c::Sp3c(const char *filename)
    : filename_(filename), istream_(filename, std::ios_base::in),
      /*__satsys(SATELLITE_SYSTEM::mixed),*/ end_of_head_(0) {
  int j;
  if ((j = read_header())) {
    if (istream_.is_open())
      istream_.close();
    throw std::runtime_error("[ERROR] Failed to read Sp3 header; Error Code: " +
                             std::to_string(j));
  }
}

int dso::Sp3c::peak_next_data_block(
    dso::datetime<dso::nanoseconds> &t) noexcept {
  char line[MAX_RECORD_CHARS];
  char c;
  int error = 0;

  if (!istream_.good())
    return 1;

  /* possible following lines (three first chars):
   * 1. '*  ' i.e an epoch header
   * 2. 'PXX' i.e. a position & clock line, e.g. 'PG01 ....'
   * 3. 'EP ' i.e. position and clock correlation
   * 4. 'VXX' i.e. velocity line, e.g. 'VG01 ...'
   * 5. 'EV ' i.e. velocity correlation
   * 6. 'EOF' i.e. EOF
   */
  const auto pos = istream_.tellg();

  // following line should be an epoch header or 'EOF'
  c = istream_.peek();
  if (c == '*') {
    if ((error = resolve_epoch_line(t))) {
      fprintf(stderr,
              "ERROR. Failed to resolve sp3 epoch line, error=%d (%s)\n", error,
              __func__);
      error += 10;
    }
  } else {
    istream_.getline(line, MAX_RECORD_CHARS);
    if (!std::strncmp(line, "EOF", 3)) {
      istream_.clear(); // clear EOF
      error = -1;
    } else {
      error = 100;
    }
  }

  return error;
}