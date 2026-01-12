#include "datetime/datetime_write.hpp"
#include "sp3.hpp"
#include "sv_interpolate.hpp"
#include <cstdio>
#include <stdexcept>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

using namespace dso;
using dso::sp3_details::SatelliteId;
using dso::sp3_details::Sp3DataBlock;
using dso::sp3_details::Sp3SvDataBlock;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <SP3c FILE>\n", argv[0]);
    return 1;
  }

  SatelliteId sv("L27");
  {
    Sp3c sp3(argv[1]);

    if (sp3.num_sats() == 1) {
      printf(
          "Sp3 file only includes one satellite; extracting records for %s\n",
          sp3.sattellite_vector()[0].id);
      sv.set_id(sp3.sattellite_vector()[0].id);
    } else if (!sp3.has_sv(sv)) {
      printf("Satellite %s not included in sp3 file\n", sv.id);
      return 0;
    }
  }

  Sp3ForwardInterpolator<310, 10> intp(argv[1], sv);

  /*
  *  2023 12 26  8  6  0.00000000
  PL39  -2125.446512   3125.510637   6725.232434 999999.999999
  VL39 -66681.281622  -2990.678681 -19662.375495 999999.999999
  */
  Sp3SvDataBlock blk;
  const datetime<nanoseconds> t(year(2023), month(12), day_of_month(26),
                                hours(8), minutes(6), nanoseconds(0));
  intp.interpolate(t, blk);

  char buf[64];
  printf("%s %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f\n",
         to_char<YMDFormat::YYYYMMDD, HMSFormat::HHMMSSF>(t, buf), blk.state[0],
         blk.state[1], blk.state[2], blk.state[3], blk.state[4], blk.state[5],
         blk.state[6], blk.state[7]);
  printf("%s %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f\n",
         "2023 12 26  8  6  0.00000000", -2125.446512, 3125.510637, 6725.232434,
         999999.999999, -66681.281622, -2990.678681, -19662.375495,
         999999.999999);

  return 0;
}
