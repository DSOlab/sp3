#include "sv_interpolate.hpp"
#include <cmath>
#include <cstdio>

constexpr const double TOL = 5e0;

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
int dso::barycentric_interpolation(double x, double &y, double &dy,
                                   const double *__restrict__ xx,
                                   const double *__restrict__ yy,
                                   int array_size, int mm, int from_index,
                                   double *cws, double *dws) noexcept {
  const double *xpts = xx + from_index;
  const double *ypts = yy + from_index;

  if (from_index + mm > array_size) {
    fprintf(stderr,
            "[ERROR] Not enough data points to perform interpolation "
            "(traceback: %s)\n",
            __func__);
    return 1;
  }

  /* If x coincides with a node, return exact value
   * (tolerance helps when x comes from computation)
   */
  const double eps = TOL * std::numeric_limits<double>::epsilon();
  for (int j = 0; j < mm; ++j) {
    double dx = x - xpts[j];
    if (std::abs(dx) <= eps * (1.0 + std::abs(xpts[j]))) {
      y = ypts[j];
      return 0;
    }
  }

  /* Allocate workspace if needed */
  double *c;
  c = (cws == nullptr) ? new double[mm] : cws;

  /* compute w[0] to w[mm-1] */
  double *__restrict__ w = c;
  for (int j = 0; j < mm; j++) {
    w[j] = 1e0;
    for (int k = 0; k < j; k++) {
      w[j] *= (xpts[j] - xpts[k]);
    }
    for (int k = j + 1; k < mm; k++) {
      w[j] *= (xpts[j] - xpts[k]);
    }
    w[j] = 1e0 / w[j];
  }

  /* barycentric formula */
  double A = 0e0, B = 0e0;
  for (int j = 0; j < mm; j++) {
    A += w[j] * ypts[j] / (x - xpts[j]);
    B += w[j] / (x - xpts[j]);
  }

  y = A / B;
  dy = 0e0;
  return 0;
}