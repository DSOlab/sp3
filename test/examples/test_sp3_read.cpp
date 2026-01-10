#include "datetime/datetime_write.hpp"
#include "sp3.hpp"
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

  try {
    Sp3c sp3(argv[1]);

    SatelliteId sv("L27");
    Sp3DataBlock block;

    if (sp3.num_sats() == 1) {
      printf(
          "Sp3 file only includes one satellite; extracting records for %s\n",
          sp3.sattellite_vector()[0].id);
      sv.set_id(sp3.sattellite_vector()[0].id);
    } else if (!sp3.has_sv(sv)) {
      printf("Satellite %s not included in sp3 file\n", sv.id);
      return 0;
    }

    // let's try reading the records; note that -1 denotes EOF
    std::size_t rec_count = 0;
    char buf[64];
    for (auto it = sp3.begin(); it != sp3.end(); ++it) {
      /* get the Sp3SvDataBlock for the satellite (svb) */
      auto svb = it->sat_block(sv);
      if (svb == it->blocks_.end()) {
        fprintf(stderr, "Error. Failed to  find entry for satellite at %s\n",
                to_char<YMDFormat::YYYYMMDD, HMSFormat::HHMMSSF>(it->t(), buf));
        return 5;
      }
      printf("%s %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f %14.6f\n",
             to_char<YMDFormat::YYYYMMDD, HMSFormat::HHMMSSF>(it->t(), buf),
             svb->state[0], svb->state[1], svb->state[2], svb->state[3],
             svb->state[4], svb->state[5], svb->state[6], svb->state[7]);
      ++rec_count;
    }

    printf("Num of records read: %6lu\n", rec_count);
  } catch (std::exception &e) {
    fprintf(stderr,
            "[ERROR] Exception thrown; probably no info, but here is what: %s "
            "(traceback: %s)\n",
            e.what(), __func__);
    return 3;
  }

  return 0;
}
