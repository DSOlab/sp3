#ifndef __SV_SP3_INTERPOLATION_HPP__
#define __SV_SP3_INTERPOLATION_HPP__

#include "sp3.hpp"
#include <stdexcept>
#ifdef DEBUG
#include <chrono>
#endif

namespace dso {

namespace sp3 {
int neville_interpolation(double x, double &y, double &dy, const double *xx,
                          const double *yy, int array_size, int mm,
                          int from_index = 0, double *cws = nullptr,
                          double *dws = nullptr) noexcept;

int neville_interpolation3(double t, double *estimates, double *destimates,
                           const double *__restrict__ tt,
                           const double *__restrict__ xx,
                           const double *__restrict__ yy,
                           const double *__restrict__ zz, int array_size,
                           int mm, int from_index, double *workspace) noexcept;
} /* namespace sp3 */

constexpr int MIN_INTERPOLATION_PTS = 4;

constexpr const dso::milliseconds three_min_in_millisec{
    (3 * 60 + 1) * dso::milliseconds::sec_factor<long>()};

template <int WINDOW_SEC, int MIN_PTS> class Sp3ForwardInterpolator {
private:
  /** SV to interpolate */
  sp3::SatelliteId svid;
  /** Sp3 instance providing data values */
  Sp3c *sp3{nullptr};
}; /*Sp3ForwardInterpolator*/

class SvInterpolator {
private:
  /** SV to interpolate */
  sp3::SatelliteId svid;
  /** num of data points (blocks) available for SV; read from Sp3 */
  int num_dpts{0};
  /** Sp3 instance providing data values */
  Sp3c *sp3{nullptr};
  /** last index of data used in the interpolation */
  int last_index{0};
  /** interval to use in interpolation, aka use points up to max_millisec
   * away from requested epoch to perform the interpolation
   */
  dso::milliseconds max_millisec{three_min_in_millisec};
  /** minimum number of points on each side to perform interpolation */
  int min_dpts_on_each_side{2};
  /** data points/blocks to be collected from the Sp3 */
  Sp3DataBlock *data{nullptr};
  /** time, x, y and z data arrays used in interpolation */
  double *txyz{nullptr};
  /** workspace arena (allocate once) used in interpolation */
  double *workspace{nullptr};

  /** @brief Compute workspace arena size
   *
   * This function will compute the maximum number of data points to be used
   * in the interpolation, based on the options available (aka max_millisec)
   * and the data interval of the Sp3.
   * We are considering points up to max_millisec on the right and points
   * up to max_millisec on the left.
   *
   * @return Maximum number of points around a central point, with time tags
   *         less than max_millisec apart
   */
  int compute_workspace_size() noexcept;

  /** Fill in the data array using an sp3 instance (aka collect SV blocks
   * from Sp3)
   */
  int feed_from_sp3() noexcept;

  /** Return the index of the data block in the data array, so that
   *  bloc[i].t <= t < block[i+1].t
   */
  int index_hunt(const dso::datetime<dso::nanoseconds> &t) noexcept {
    // quick .....
    if (last_index < num_dpts - 2) {
      if (data[last_index].t <= t && data[last_index + 1].t > t) {
        return last_index;
      } else if (data[last_index + 1].t <= t && data[last_index + 2].t > t) {
        return (++last_index);
      }
    }

    int start_index = (data[last_index].t <= t) ? last_index : 0;
    auto it = std::lower_bound(
        data + start_index, data + num_dpts, t,
        [](const Sp3DataBlock &block,
           const dso::datetime<dso::nanoseconds> &tt) { return block.t < tt; });
    return (last_index = static_cast<int>(it - data));
  }

public:
  SvInterpolator(sp3::SatelliteId sid) noexcept : svid(sid) {};

  /** Constructor from a SatelliteId and an Sp3c instance; this function
   *  will:
   *  1. call the feed_from_sp3() function, to allocate needed memory
   *     and read in the satellite blocks off from the sp3 file
   *  2. allocate enough workspace (a memory arena) for the t, x, y, and
   *     z arrays (for later calls to interpolate at) and also the
   *     c, d arrays that are needed for neville interpolation
   */
  SvInterpolator(
      sp3::SatelliteId sid, Sp3c &sp3obj,
      dso::milliseconds max_allowed_millisec = three_min_in_millisec);

  /** @brief Destructor (free memmory) */
  ~SvInterpolator() noexcept {
    if (data && num_dpts)
      delete[] data;
    if (txyz)
      delete[] txyz;
    if (workspace)
      delete[] workspace;
    num_dpts = 0;
  }

  const dso::datetime<dso::nanoseconds> *last_block_date() const noexcept {
    return (num_dpts) ? &(data[num_dpts - 1].t) : nullptr;
  }

  int num_data_points() const noexcept { return num_dpts; }

  int interpolate_at(dso::datetime<dso::nanoseconds> t, double *pos,
                     double *erpos, double *vel = nullptr,
                     double *ervel = nullptr) noexcept;
}; /* class SvInterpolator */

} /* namespace dso */

#endif
