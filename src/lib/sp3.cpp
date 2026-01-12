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
 *
 *  @param[in] line An Epoch Header Record to be resolved
 *  @param[out] t The epoch resolved from the input line
 *  @return Anything other than 0 denotes an error
 */
int dso::Sp3c::resolve_epoch_line(const char *line,
                                  dso::datetime<dso::nanoseconds> &t) noexcept {

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

  /* check the stream; baybe EOF is already set by the previous call to the
   * function */
  if (!istream_) {
    if (istream_.eof())
      return -1;
    return 1;
  }

  /* get the next line (should be start of block) */
  istream_.getline(line, MAX_RECORD_CHARS);

  /* current epoch (of block) */
  dso::datetime<dso::nanoseconds> t;
  if (resolve_epoch_line(line, t)) {
    fprintf(stderr,
            "[ERROR] Failed resolving Sp3 start-of-block line: \"%s\"; "
            "(traceback: %s)\n",
            line, __func__);
    return 1;
  }

  // keep on reading records .....
  dso::sp3_details::SatelliteId satid;
  dso::Sp3Flag flag;
  double arr[8], sarr[8];
  int error = 0;

  while (istream_.getline(line, MAX_RECORD_CHARS) && (!error)) {
    /* position line */
    if (line[0] == 'P') {
      /* consume position line; resolve it (if sats match) */
      error = get_next_position(line, satid, arr, sarr, flag, sat);
      if (!error) {
        auto it = datablock.sat_block(satid);
        if (it == datablock.blocks_.end()) {
          /* entry for new satellite (mark absent velocity for now) */
          flag.set(dso::Sp3Event::bad_abscent_velocity |
                   dso::Sp3Event::bad_abscent_clock_rate);
          datablock.blocks_.emplace_back(satid, flag, arr, sarr);
        } else {
          it->flag.clear(dso::Sp3Event::bad_abscent_position);
          it->flag.clear(dso::Sp3Event::bad_abscent_clock);
          it->update_position(flag, arr, sarr);
        }
        datablock.t_ = t;
      }
    } else if (line[0] == 'V') {
      error = get_next_velocity(line, satid, arr + 4, sarr + 4, flag, sat);
      if (!error) {
        auto it = datablock.sat_block(satid);
        if (it == datablock.blocks_.end()) {
          /* entry for new satellite (mark absent position for now) */
          flag.set(dso::Sp3Event::bad_abscent_position |
                   dso::Sp3Event::bad_abscent_clock);
          datablock.blocks_.emplace_back(satid, flag, arr, sarr);
        } else {
          it->flag.clear(dso::Sp3Event::bad_abscent_velocity);
          it->flag.clear(dso::Sp3Event::bad_abscent_clock_rate);
          it->update_velocity(flag, arr + 4, sarr + 4);
        }
        datablock.t_ = t;
      }
    } else if (!std::strncmp(line, "EP", 2)) {
      fprintf(stderr, "[DEBUG] Ingoring Position Correlation Records ...\n");
    } else if (!std::strncmp(line, "EV", 2)) {
      fprintf(stderr, "[DEBUG] Ingoring Velocity Correlation Records ...\n");
    } else if (!std::strncmp(line, "EOF", 3)) {
      /* Note! we are reading one more line here (actually holds nothing) so
       * that the isteram_ will be set to eof; then just break. Next time the
       * function is called, it will see that the stream is on eof state and
       * return -1. */
      istream_.getline(line, MAX_RECORD_CHARS);
      break;
    } else {
      return 150;
    }
    /* check first char of next line */
    char c = istream_.peek();
    if (c == '*') {
      /* got new date line, i.e. reached next block; stop */
      break;
    }
  }

  return error;
}

/** Read in and resolve an Sp3c/d Velocity and ClockRate-of-Change Record. The
 *  function expects that the next line to be read off from the input stream
 *  is a Velocity and ClockRate-of-Change Record line.
 *
 *  @param[out] A 3character satellite id as recorded in the Sp3 file
 *  @param[out] v_xyz An array of size >= 3, which at output will contain:
 *  - xv  X-component of satelite velocity, in dm/sec
 *  - yv  y-component of satelite velocity, in dm/sec
 *  - zv  Z-component of satelite velocity, in dm/sec
 *  - cv  Clock rate-of-change in 10**-4 microseconds/second
 *  @param[out] v_xyz_std An array of size >= 3, which at output will contain:
 *  - X-component std. deviation in 10**-4 mm/sec
 *  - Y-component std. deviation in 10**-4 mm/sec
 *  - Z-component std. deviation in 10**-4 mm/sec
 *  - Clock std. deviation in 10**-4 psec/sec
 *  @param[out] flag An Sp3Flag instance denoting the status of the resolved
 *              fields. The flag is NOT reset (aka input flags will not be
 *              touched). Any flags to be added, only affect position and
 * clock rate-of-change records (aka bad_abscent_velocity,
 *              bad_abscent_clock_rate, has_vel_stddev, has_clk_rate_stdev).
 *  @param[in] wsat If provided, then only resolve the data line if the given
 *              SatelliteId wsat matches the one recorded in the line. If the
 *              SatelliteId was not matched, the events bad_abscent_velocity
 *              and bad_abscent_clock_rate are set, and an non-zero integer is
 *              returned.
 *  @return Anything other than 0 denotes an error (note tha error codes must
 *          be >0 and <10)
 */
int dso::Sp3c::get_next_velocity(const char *line, SatelliteId &sat,
                                 double *v_xyzc, double *v_xyzc_std,
                                 Sp3Flag &flag,
                                 const SatelliteId *wsat) noexcept {
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

  v_xyzc[0] = dvec[0]; // dm/s
  v_xyzc[1] = dvec[1];
  v_xyzc[2] = dvec[2];
  v_xyzc[3] = dvec[3]; // 10**-4 microseconds/second

  /* check/set flags */
  if (v_xyzc[0] == 0e0 || (v_xyzc[1] == 0e0 || v_xyzc[2] == 0e0))
    flag.set(Sp3Event::bad_abscent_velocity);
  else
    flag.clear(Sp3Event::bad_abscent_velocity);

  if (v_xyzc[3] >= SP3_MISSING_CLK_VALUE)
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
      v_xyzc_std[0] = std::pow(fpb_pos_, nn); // 10**-4 mm/sec
      ++has_pos_stddev;
    }
    if (*(line + 64) != ' ' || *(line + 65) != ' ') {
      int nn = std::strtol(line + 64, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      v_xyzc_std[1] = std::pow(fpb_pos_, nn);
      ++has_pos_stddev;
    }
    if (*(line + 67) != ' ' || *(line + 68) != ' ') {
      int nn = std::strtol(line + 67, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      v_xyzc_std[2] = std::pow(fpb_pos_, nn);
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
      v_xyzc_std[3] = std::pow(fpb_clk_, nn); // 10**-4 psec/sec
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
 *  @param[out] xyzc An array of size >= 4, which at output will contain:
 *  - X-component of satelite position, in km
 *  - y-component of satelite position, in km
 *  - Z-component of satelite position, in km
 *  - Clock correction in microsec
 *  @param[out] xyzc_std An array of size >= 4, which at output will contain:
 *  - X-component std. deviation in mm
 *  - Y-component std. deviation in mm
 *  - Z-component std. deviation in mm
 *  - Clock std. deviation in psec
 *  @param[out] flag An Sp3Flag instance denoting the status of the resolved
 *              fields. Note that the flag will be reset at the function call
 *  @param[in] wsat If provided, then only resolve the data line if the given
 *              SatelliteId wsat matches the one recorded in the line. If the
 *              SatelliteId was not matched, the events bad_abscent_position
 *              and bad_abscent_clock are set. A non-zero integer is returned.
 *  @return Anything other than 0 denotes an error (note tha error codes must
 *          be >0 and <10)
 */
int dso::Sp3c::get_next_position(const char *line, SatelliteId &sat,
                                 double *xyzc, double *xyzc_std, Sp3Flag &flag,
                                 const SatelliteId *wsat) noexcept {
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

  xyzc[0] = dvec[0];
  xyzc[1] = dvec[1];
  xyzc[2] = dvec[2];

  if (xyzc[0] == 0e0 || xyzc[1] == 0e0 || xyzc[2] == 0e0)
    flag.set(Sp3Event::bad_abscent_position);
  else
    flag.clear(Sp3Event::bad_abscent_position);

  xyzc[3] = dvec[3];
  if (xyzc[3] >= SP3_MISSING_CLK_VALUE)
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
      xyzc_std[0] = std::pow(fpb_pos_, nn);
      ++has_pos_stddev;
    }
    if (*(line + 64) != ' ' || *(line + 65) != ' ') {
      int nn = std::strtol(line + 64, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      xyzc_std[1] = std::pow(fpb_pos_, nn);
      ++has_pos_stddev;
    }
    if (*(line + 67) != ' ' || *(line + 68) != ' ') {
      int nn = std::strtol(line + 67, &end, 10);
      if (!nn || errno == ERANGE) {
        errno = 0;
        return 6;
      }
      xyzc_std[2] = std::pow(fpb_pos_, nn);
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
      xyzc_std[3] = std::pow(fpb_clk_, nn);
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
      end_of_head_(0) {
  int j;
  if ((j = read_header())) {
    if (istream_.is_open())
      istream_.close();
    throw std::runtime_error("[ERROR] Failed to read Sp3 header; Error Code: " +
                             std::to_string(j));
  }
  std::memset(cmempool_, '\0', sp3_details::MEMPOOL_SIZE_CHAR);
}
