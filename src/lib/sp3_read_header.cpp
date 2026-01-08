#include "sp3.hpp"
#include <cstdio>

/// No header line can have more than 80 chars. However, there are cases when
/// they  exceed this limit, just a bit ...
constexpr int MAX_HEADER_CHARS{85};

/// Max header lines.
constexpr int MAX_HEADER_LINES{1000};

/// Read a, Sp3c file header and assign vital information.
/// The function will read all header lines, stoping after the line:
/// @return  Anything other than 0 denotes an error.
int dso::Sp3c::read_header() noexcept {
  char line[MAX_HEADER_CHARS];
  char *str_end;
  int dummy_it = 0;

  // The stream should be open by now!
  if (!istream_.is_open())
    return 1;

  // Go to the top of the file.
  istream_.seekg(0);

  // Read the first line. Error codes 10-19
  // ------------------------------------------------------------
  istream_.getline(line, MAX_HEADER_CHARS);
  if (*line != '#')
    return 10; // validate version
  version_ = *(line + 1);
  if (version_ != 'c' && version_ != 'd')
    return 10;
  int year = std::strtol(line + 3, &str_end, 10); // read year
  if (!year || errno == ERANGE) {
    errno = 0;
    return 11;
  }
  int month = std::strtol(line + 8, &str_end, 10); // read month
  if (!month || errno == ERANGE) {
    errno = 0;
    return 12;
  }
  int dom = std::strtol(line + 11, &str_end, 10); // read day of month
  if (!dom || errno == ERANGE) {
    errno = 0;
    return 13;
  }
  int hour = std::strtol(line + 14, &str_end, 10); // read hour of day
  if (errno == ERANGE || str_end == line + 14) {
    errno = 0;
    return 14;
  }
  int minute = std::strtol(line + 17, &str_end, 10); // read hour of day
  if (errno == ERANGE || str_end == line + 17) {
    errno = 0;
    return 15;
  }
  double sec = std::strtod(line + 20, &str_end); // read seconds

  num_epochs_ = std::strtol(line + 32, &str_end, 10); // read number of epochs
  if (!num_epochs_ || errno == ERANGE) {
    errno = 0;
    return 16;
  }
  std::memcpy(crd_sys_, line + 46, 5);
  std::memcpy(orb_type_, line + 52, 3);
  std::memcpy(agency_, line + 56, 4);

  // all done for first line, construct the reference date
  start_epoch_ = dso::datetime<dso::nanoseconds>(
      dso::year(year), dso::month(month), dso::day_of_month(dom),
      dso::hours(hour), dso::minutes(minute),
      dso::nanoseconds(
          static_cast<long>(sec * dso::nanoseconds::sec_factor<double>())));

  // Read the second line. Error codes [20,30)
  // ------------------------------------------------------------
  istream_.getline(line, MAX_HEADER_CHARS);
  if (*line != '#' || line[1] != '#')
    return 20;
  int gwk = std::strtol(line + 3, &str_end, 10);
  if (!gwk || errno == ERANGE) {
    errno = 0;
    return 21;
  }
  sec = std::strtod(line + 8, &str_end);
  // validate start epoch (#1)
  dso::nanoseconds sw;
  auto gwk1 = start_epoch_.gps_wsow(sw);
  if ((gwk1.as_underlying_type() != gwk) ||
      (std::abs(dso::to_fractional_seconds(sw).seconds() - sec) > 1e-9)) {
    fprintf(stderr, "[ERROR] Failed to validate start date (traceback: %s)\n",
            __func__);
    fprintf(stderr,
            "[ERROR] Computed GPST is (%ld, %.12f), read is (%d, %.12f) diff "
            "is %.1e [sec] (traceback: %s)\n",
            gwk1.as_underlying_type(), dso::to_fractional_seconds(sw).seconds(),
            gwk, sec, std::abs(dso::to_fractional_seconds(sw).seconds() - sec),
            __func__);
    return 22;
  }
  sec = std::strtod(line + 24, &str_end);
  interval_ = dso::nanoseconds(
      static_cast<long>(sec * dso::nanoseconds::sec_factor<double>()));
  int mjd = std::strtol(line + 39, &str_end, 10);
  if (!mjd || errno == ERANGE) {
    errno = 0;
    return 23;
  }
  sec = std::strtod(line + 45, &str_end);
  sec += mjd;
  if (sec != start_epoch_.fmjd()) {
    fprintf(stderr, "[ERROR] Failed to validate start date (traceback: %s)\n",
            __func__);
    return 24;
  }

  // Read the satellite ID lines; they must be at least 5, but there is no max
  // limitation for the Sp3d files. Each satellite accuracy line starts with
  // '++'
  // Error code [30,40]
  // ------------------------------------------------------------
  istream_.getline(line, MAX_HEADER_CHARS);
  if (*line != '+' || line[1] != ' ')
    return 30;
  num_sats_ = std::strtol(line + 3, &str_end, 10);
  if (!num_sats_ || errno == ERANGE) {
    errno = 0;
    return 31;
  }
  int csat = 0, cidx = 9, lines_read = 0;
  sat_vec_.reserve(num_sats_);
  while (csat < num_sats_) {
    sat_vec_.emplace_back(line + cidx);
    cidx += 3;
    if (cidx >= 60 && csat < num_sats_) {
      istream_.getline(line, MAX_HEADER_CHARS);
      cidx = 9;
      ++lines_read;
    }
    ++csat;
  }
  while (lines_read < 5) {
    istream_.getline(line, MAX_HEADER_CHARS);
    ++lines_read;
  }

  // Read the satellite accuracy lines; they must be at least 5, but there is
  // no max limitation for the Sp3d files. Each satellite id line starts with '+
  // ' Error code [40,50]
  // ------------------------------------------------------------
  istream_.getline(line, MAX_HEADER_CHARS);
  if (*line != '+' || line[1] != '+') {
    fprintf(stderr,
            "[ERROR] Expected sat. accuracy line, starting with \'++\'; found "
            "line \'%s\' (traceback: %s)\n",
            line, __func__);
    return 40;
  }
  while (++dummy_it < MAX_HEADER_LINES && !std::strncmp(line, "++", 2)) {
    istream_.getline(line, MAX_HEADER_CHARS);
    char c = istream_.peek();
    if (c != '+')
      break;
  }
  if (dummy_it >= MAX_HEADER_LINES) {
    return 41;
  }

  // two lines follow, starting with '%c'; collect the system time
  // Error code [50,60]
  // ------------------------------------------------------------
  istream_.getline(line, MAX_HEADER_CHARS);
  if (*line != '%' || line[1] != 'c')
    return 50;
  /*time_sys__ = std::string(line + 9, 3);*/
  std::memcpy(time_sys_, line + 9, 3);
  istream_.getline(line, MAX_HEADER_CHARS);
  if (*line != '%' || line[1] != 'c')
    return 51;

  // two lines follow, starting with '%f'
  // Error code [60,70]
  // ------------------------------------------------------------
  /*
  for (int i = 0; i < 2; i++) {
    __istream.getline(line, MAX_HEADER_CHARS);
    if (*line != '%' || line[1] != 'f')
      return 60;
  }*/
  istream_.getline(line, MAX_HEADER_CHARS);
  if (*line != '%' || line[1] != 'f')
    return 60;
  fpb_pos_ = fpb_clk_ = 0e0;
  fpb_pos_ = std::strtod(line + 3, &str_end);
  fpb_clk_ = std::strtod(line + 14, &str_end);
  if (str_end == line + 14 || (fpb_pos_ == 0e0 || fpb_clk_ == 0e0)) {
    errno = 0;
    return 61;
  }
  istream_.getline(line, MAX_HEADER_CHARS);
  if (*line != '%' || line[1] != 'f')
    return 65;

  // two lines follow, starting with '%i'
  // Error code [70,80]
  // ------------------------------------------------------------
  for (int i = 0; i < 2; i++) {
    istream_.getline(line, MAX_HEADER_CHARS);
    if (*line != '%' || line[1] != 'i')
      return 70;
  }

  // read any remaining comment lines, starting with '/*'
  // Error code [80,90]
  // ------------------------------------------------------------
  char c = istream_.peek();
  while (c == '/') {
    istream_.getline(line, MAX_HEADER_CHARS);
    if (line[1] != '*')
      return 80;
    c = istream_.peek();
    if (++dummy_it > MAX_HEADER_LINES)
      return 81;
  }

  // Mark the end of header
  end_of_head_ = istream_.tellg();

  // All done !
  return 0;
}
