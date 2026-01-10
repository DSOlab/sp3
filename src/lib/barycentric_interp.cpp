#include "sv_interpolate.hpp"
#include <cmath>
#include <cstdio>

/** @brief Barycentric interpolation algorithm
 *
 * @see Jean-Paul Berrut and Lloyd N. Trefethen, Barycentric Lagrange
 * Interpolation, Society for Industrial and Applied Mathematics, Vol. 46,
 * No. 3, pp. 501–517, DOI. 10.1137/S0036144502417715
 * https://epubs.siam.org/doi/epdf/10.1137/S0036144502417715
 *
 * @param[in]  x The point to interpolate at
 * @param[out] y Value of interpolating polynomial at x
 * @param[out] dy Error indication for computed y value; NOT USED, always 0
 * @param[in] xx The x-axis data points of size array_size
 * @param[in] yy The y-axis data points (values at xx's) of size array_size
 * @param[in] array_size The size of xx and yy arrays
 * @param[in] mm Number of data points to use for the interpolation; the
 *            interval will be xx[from_index,...,from_index+mm-1]
 * @param[in] cws (optional) An array of size mm; if not given it will be
 *            allocated/freed during function execution
 * @param[in] dws NOT USED
 * @return Always returns an int, following the convention:
 *         0: success
 *         1: not enough points to perform interpolation
 *        >1: computation error
 */
// TODO need to handle flags!!!
int dso::barycentric_interpolation(const dso::datetime<dso::nanoseconds> &t,
                                   dso::sp3_details::Sp3SvDataBlock &y,
                                   const dso::datetime<dso::nanoseconds> *tarr,
                                   const dso::sp3_details::Sp3SvDataBlock *yarr,
                                   int npts, double *cws) noexcept {

  /* If x (aka t here) coincides with a node, return exact value
   */
  const double eps = std::numeric_limits<double>::epsilon();
  for (int j = 0; j < npts; ++j) {
    double dx = t.diff<dso::DateTimeDifferenceType::FractionalSeconds>(tarr[j])
                    .seconds();
    if (std::abs(dx) <= eps) {
      y = ty[j];
      return 0;
    }
  }

  /* Allocate workspace if needed */
  double *c;
  c = (cws == nullptr) ? new double[npts] : cws;

  /* compute w[0] to w[mm-1] (common for all components) */
  double *__restrict__ w = c;
  for (int j = 0; j < npts; j++) {
    w[j] = 1e0;
    for (int k = 0; k < j; k++) {
      // w[j] *= (xpts[j] - xpts[k]);
      w[j] *= tarr[j]
                  .diff<dso::DateTimeDifferenceType::FractionalSeconds>(tarr[k])
                  .seconds();
    }
    for (int k = j + 1; k < npts; k++) {
      // w[j] *= (xpts[j] - xpts[k]);
      w[j] *= tarr[j]
                  .diff<dso::DateTimeDifferenceType::FractionalSeconds>(tarr[k])
                  .seconds();
    }
    w[j] = 1e0 / w[j];
  }

  /* interpated values, set to zero */
  std::memset(y.state, 0, sizeof(double) * 8);
  std::memset(y.state_sdev, 0, sizeof(double) * 8);

  /* barycentric formula */
  double A = 0e0, B = 0e0;
  for (int j = 0; j < npts; j++) {
    const double h =
        t.diff<dso::DateTimeDifferenceType::FractionalSeconds>(tarr[j])
            .seconds();
    B += w[j] / h;
    /* compute nominator sum directly in place for all components: [ X, Y, Z,
     * clk, Vx, Vy, Vz, Vc ]*/
    for (int si = 0; si < 8; si++) {
      y.state[si] += w[j] * yarr[j].state[si] / h;
    }
  }

  /* barycentric niterpolation: y <- A / B; */
  for (int si = 0; si < 8; si++) {
    y.state[si] /= B;
  }

  return 0;
}