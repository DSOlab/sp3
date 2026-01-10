#ifndef __SV_SP3_INTERPOLATION_HPP__
#define __SV_SP3_INTERPOLATION_HPP__

#include "datetime/datetime_write.hpp"
#include "sp3.hpp"
#include <cassert>
#include <stdexcept>
#ifdef DEBUG
#include <chrono>
#endif

namespace dso {
int barycentric_interpolation(const datetime<nanoseconds> &t,
                              sp3_details::Sp3SvDataBlock &y,
                              const datetime<nanoseconds> *tarr,
                              const sp3_details::Sp3SvDataBlock *yarr, int npts,
                              double *cws = nullptr) noexcept;

template <int WINDOW_SEC, int MIN_PTS> class Sp3ForwardInterpolator {
private:
  /** SV to interpolate */
  sp3::SatelliteId svid_;
  /** Sp3 instance providing data values */
  Sp3c *sp3_{nullptr};
  Sp3c::iterator it_;
  /* epochs (corresponding to data_) */
  dso::datetime<dso::nanoseconds> *t_;
  /* data */
  Sp3SvDataBlock *data_;
  /* num of (current) data points */
  int pts_{0}, buffer_pts_{0};

  /* left shift and return the index (in t_ and data_) that now contains junk
   * and the entry should be added at.*/
  int left_shift(int nplaces = 1) noexcept {
    const int n = nplaces;
    const int k = buffer_pts_ - n;
    /* left shift data nplaces places, so that data_[nplaces] becomes data_[0]
     */
    constexpr const int md = sizeof(Sp3SvDataBlock);
    std::memmove(data_, data_ + n * md, k * md);
    constexpr const int mt = sizeof(dso::datetime<dso::nanoseconds>);
    std::memmove(t_, t_ + n * mt, k * mt);
    return k;
  }

  std::size_t buffer_pts() const noexcept {
    assert(std::abs(static_cast<int>(sp3_->interval() * 10e0) -
                    sp3_->interval * 10e0) < 1e-12);
    std::size_t wnpts =
        (WINDOW_SEC * 10) / static_cast<int>(sp3_->interval() * 10e0) + 1;
    return 2 * wnpts;
  }

  [[nodiscard]]
  int allocate() noexcept {
    const auto N = buffer_pts_;
    data_ = new Sp3SvDataBlock[N];
    t_ = new dso::datetime<dso::nanoseconds>[N];
    return (data_ != nullptr && t_ != nullptr);
  }

  enum class HUNT_DIRECTION : char { BACK, FORWARD, OK, ERROR };
  [[nodiscard]]
  HUNT_DIRECTION
  deduce_range(const dso::datetime<dso::nanoseconds> &t) const noexcept {
    if (t_[0] > t) {
      /* the data we have do not match, we should be searching for previous data
       */
      return HUNT_DIRECTION::BACK;
    }
    if (t_[0] < t) {
      /* subtract one 'interval' from the begining epoch; if we are now outside
       * the range, then this means that there are now previous data that we
       * need to collect. if not, then we should move backwards. */
      auto tleft = t_[0].add_seconds<dso::nanoseconds>(
          dso::nanoseconds(sp3_->interval().as_underlying_type() * -1));
      if (t.diff<dso::DateTimeDifferenceType::FractionalSeconds>(tleft) >
          WINDOW_SEC) {
        /* we seem to be ok on the left! let's do the same on the right */
        const int right_idx = pts_ - 1;
        auto tright = t_[right_idx].add_seconds<dso::nanoseconds>(
            dso::nanoseconds(sp3_->interval()));
        if (tright.diff<dso::DateTimeDifferenceType::FractionalSeconds>(t) >
            WINDOW_SEC) {
          /* we are ok! collecting one more data point would be outside range
           * !*/
          return HUNT_DIRECTION::OK;
        } else {
          /* we should collect at least one more data point on the right! */
          return HUNT_DIRECTION::FORWARD;
        }
      } else {
        return HUNT_DIRECTION::BACK;
      }
    }
    return HUNT_DIRECTION::ERROR;
  }

  /* TODO add error checks here !!*/
  [[nodiscard]]
  int feed() noexcept {
    ++it_;
    /* left shift and get the index to add new data/entries at */
    const int k = left_shift();
    --pts_;
    /* append new data */
    t_[k] = it->t();
    data_[k] = *(it->sat_block(svid_));
    ++pts_;
  };

  int initial_feed(const dso::datetime<dso::nanoseconds> &t) noexcept {
    pts_ = 0;
    const auto stop_t =
        t.add_seconds<dso::seconds>(dso::seconds((WINDOW_SEC) * -1));
    while (it != sp3_->end()) {
      if (t < stop_t.add_seconds(dso::seconds(1)))
        ++it;
    }

    if (it == sp3_end()) {
      char buf[64];
      fprintf(stderr,
              "[ERROR] Failed locating a matching time interval to "
              "interpolate at %s (traceback: %s)\n",
              dso::to_char<dso::YMDFormat::YYYYMMDD, dso::HMSFormat::HHMMSSF>(
                  t, buf),
              __func__);
      return -1;
    }

    while (it != sp3_->end()) {
      t_[pts_] = it->t();
      data_[pts_] = *(it->sat_block(svid_));
      ++pts_;
      const auto nextt = it->t().add_seconds(sp3_->interval());
      if (nextt.diff<dso::DateTimeDifferenceType::FractionalSeconds>(t)
              .seconds() > WINDOW_SEC)
        break;
    }
    return 0;
  }

  [[nodiscard]]
  int get_range(const dso::datetime<dso::nanoseconds> &t) noexcept {
    auto direction = deduce_range(t);
    switch (direction) {
    case HUNT_DIRECTION::OK:
      return 0;
    case HUNT_DIRECTION::FORWARD:
      int error = feed();
      if (error)
        return error;
      return get_range(t);
    case HUNT_DIRECTION::BACK:
      it_ = sp3_->begin();
      return get_range(t);
    case HUNT_DIRECTION::ERROR:
      return error;
    }
  }

public:
  int interpolate(const dso::datetime<dso::nanoseconds> t,
                  sp3_details::Sp3SvDataBlock &y) noexcept {
    /* is this the first time ? */
    if (!pts_) {
      if (initial_feed(t)) {
        {
          char buf[64];
          fprintf(
              stderr,
              "[ERROR] Failed locating a matching time interval to "
              "interpolate at %s (traceback: %s)\n",
              dso::to_char<dso::YMDFormat::YYYYMMDD, dso::HMSFormat::HHMMSSF>(
                  t, buf),
              __func__);
        }
      }
    }
    /* first get/load the correct range for (buffered) data_ and t_ */
    if (get_range(t)) {
      {
        char buf[64];
        fprintf(stderr,
                "[ERROR] Failed locating a matching time interval to "
                "interpolate at %s (traceback: %s)\n",
                dso::to_char<dso::YMDFormat::YYYYMMDD, dso::HMSFormat::HHMMSSF>(
                    t, buf),
                __func__);
      }
      return 1;
    }
    /* got it! now interpolate */
    return barycentric_interpolation(t, sp3_details::Sp3SvDataBlock & y, t_,
                                     data_, pts_);
  }

  dso::datetime<dso::nanoseconds> *t_;
  /* data */
  Sp3SvDataBlock *data_;
  /* num of (current) data points */
  int pts_{0}, buffer_pts_{0};

  Sp3ForwardInterpolator(const char *sp3fn, sp3::SatelliteId sid)
      : svid_(sid), sp3_(new Sp3c(sp3fn)), it_(sp3_->begin()), t_(nullptr),
        data_(nullptr), pts_(0), buffer_pts_(buffer_pts()) {
    allocate();
  }

  ~Sp3ForwardInterpolator() noexcept {
    if (sp3)
      delete sp3;
    if (t_)
      delete[] t;
    if (data_)
      delete[] data;
  }
}; /*Sp3ForwardInterpolator*/

} /* namespace dso */

#endif
