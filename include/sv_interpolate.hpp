#ifndef __SV_SP3_INTERPOLATION_HPP__
#define __SV_SP3_INTERPOLATION_HPP__

#include "datetime/datetime_write.hpp"
#include "sp3.hpp"
#include <cassert>
#include <stdexcept>
#include <utility> // std::declval
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
  using IntType = dso::nanoseconds::underlying_type;
  /** SV to interpolate */
  sp3_details::SatelliteId svid_;
  /** Sp3 instance providing data values */
  Sp3c *sp3_{nullptr};
  /* Sp3c::iterator it_; this will not compile, iterator is private within Sp3
   */
  using iterator_t = decltype(std::declval<Sp3c &>().begin());
  iterator_t it_;
  /* epochs (corresponding to data_) */
  dso::datetime<dso::nanoseconds> *t_;
  /* data */
  sp3_details::Sp3SvDataBlock *data_;
  /* num of (current) data points */
  int pts_{0}, buffer_pts_{0};

  /* left shift and return the index (in t_ and data_) that now contains junk
   * and the entry should be added at.*/
  int left_shift(int nplaces = 1) noexcept {
    const int k = pts_ - nplaces;
    assert(k >= 0 && k < buffer_pts_);
    /* left shift data nplaces places, so that data_[nplaces] becomes data_[0]
     */
    std::memmove(data_, data_ + nplaces, k * sizeof(*data_));
    std::memmove(t_, t_ + nplaces, k * sizeof(*t_));
    return k;
  }

  std::size_t buffer_pts() const noexcept {
    const IntType interval = sp3_->interval().as_underlying_type();
    const IntType window =
        dso::nanoseconds(dso::cast_to<dso::seconds, dso::nanoseconds>(
                             dso::seconds(WINDOW_SEC)))
            .as_underlying_type();
    std::size_t wnpts = window / interval + 1;
    return 2 * wnpts;
  }

  int allocate() noexcept {
    const auto N = buffer_pts();
    data_ = new sp3_details::Sp3SvDataBlock[N];
    t_ = new dso::datetime<dso::nanoseconds>[N];
    buffer_pts_ = N;
    return (data_ != nullptr && t_ != nullptr);
  }

  enum class HUNT_DIRECTION : char { BACK, FORWARD, OK, ERROR };
  [[nodiscard]]
  HUNT_DIRECTION
  deduce_range(const dso::datetime<dso::nanoseconds> &t) const noexcept {
    if (t_[0] > t) {
      /* the data we have do not match, we should be searching for previous data
       */
      if (sp3_->start_epoch() > t)
        return HUNT_DIRECTION::ERROR;
      return HUNT_DIRECTION::BACK;
    }
    if (t_[0] < t) {
      /* subtract one 'interval' from the begining epoch; if we are now
       * outside the range, then this means that there are no previous data
       * that we need to collect. if not, then we should move backwards. */
      dso::seconds neg_interval =
          dso::seconds(sp3_->interval().as_underlying_type() * -1);
      auto tleft = t_[0].add_seconds(neg_interval);

      if (t.diff<dso::DateTimeDifferenceType::FractionalSeconds>(tleft)
              .seconds() > WINDOW_SEC) {
        /* we seem to be ok on the left! let's do the same on the right */
        const int right_idx = pts_ - 1;
        auto tright = t_[right_idx].add_seconds<dso::nanoseconds>(
            dso::nanoseconds(sp3_->interval()));
        if (tright.diff<dso::DateTimeDifferenceType::FractionalSeconds>(t)
                .seconds() > WINDOW_SEC) {
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
    t_[k] = it_->t();
    data_[k] = *(it_->sat_block(svid_));
    ++pts_;
    return 0;
  }

  int initial_feed(const dso::datetime<dso::nanoseconds> &t) noexcept {
    pts_ = 0;
    const auto stop_t =
        t.add_seconds<dso::seconds>(dso::seconds((WINDOW_SEC) * -1));
    while ((it_ != sp3_->end()) && (it_->t() < stop_t)) {
      ++it_;
    }

    if (it_ == sp3_->end()) {
      char buf[64];
      fprintf(stderr,
              "[ERROR] Failed locating a matching time interval to "
              "interpolate at %s (traceback: %s)\n",
              dso::to_char<dso::YMDFormat::YYYYMMDD, dso::HMSFormat::HHMMSSF>(
                  t, buf),
              __func__);
      return 1;
    }

    while (it_ != sp3_->end()) {
      t_[pts_] = it_->t();
      data_[pts_] = *(it_->sat_block(svid_));
      ++pts_;
      const auto nextt = it_->t().add_seconds(sp3_->interval());
      if (nextt.diff<dso::DateTimeDifferenceType::FractionalSeconds>(t)
              .seconds() > WINDOW_SEC)
        break;
      ++it_;
    }
    /* pts_ should not be 0 */
    return (!pts_);
  }

  [[nodiscard]]
  int get_range(const dso::datetime<dso::nanoseconds> &t) noexcept {
    auto direction = deduce_range(t);
    switch (direction) {
    case HUNT_DIRECTION::OK:
      return 0;
    case HUNT_DIRECTION::FORWARD: {
      int error = feed();
      if (error)
        return error;
      return get_range(t);
    }
    case HUNT_DIRECTION::BACK: {
      it_.try_rewind();
      int error = initial_feed(t);
      if (error)
        return error;
      return get_range(t);
    }
    case HUNT_DIRECTION::ERROR:
      return 1;
    }
    return 100;
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
    return barycentric_interpolation(t, y, t_, data_, pts_);
  }

  Sp3ForwardInterpolator(const char *sp3fn, sp3_details::SatelliteId sid)
      : svid_(sid), sp3_(new Sp3c(sp3fn)), it_(sp3_->begin()), t_(nullptr),
        data_(nullptr), pts_(0), buffer_pts_(buffer_pts()) {
    allocate();
  }

  ~Sp3ForwardInterpolator() noexcept {
    if (sp3_)
      delete sp3_;
    if (t_)
      delete[] t_;
    if (data_)
      delete[] data_;
  }
}; /*Sp3ForwardInterpolator*/

} /* namespace dso */

#endif
