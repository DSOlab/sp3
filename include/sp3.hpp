/** @file
 * Define class to handle interaction with Sp3-C ephemerides files.
 *
 * References:
 * [1] Steve Hilla, The Extended Standard Product 3 Orbit Format
 * (SP3-c), 17 August 2010, https://files.igs.org/pub/data/format/sp3c.txt
 */

#ifndef __SP3C_IGS_FILE__
#define __SP3C_IGS_FILE__

#include "datetime/calendar.hpp"
#include "satellite.hpp"
#include "sp3flag.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>
#ifdef DEBUG
#include "datetime/datetime_write.hpp"
#endif

namespace dso {

namespace sp3_details {

/* number of chars in Sp3c mempool */
constexpr const int MEMPOOL_SIZE_CHAR = 32;
/* Offset in Sp3c mempool for coordinate system string */
constexpr const int CRD_SYS_OF = 0;
/* number of chars in coordinate system string (including null-terminating
 * char); Example: "ITR97", format: [A5] + '\0'
 */
constexpr const int CRD_SYS_SZ = 6;
/* Offset in Sp3c mempool for orbit type string */
constexpr const int ORB_TYP_OF = CRD_SYS_SZ;
/* number of chars in orbit type string (including null-terminating
 * char); Example: "FIT", format: [A3] + '\0'
 */
constexpr const int ORB_TYP_SZ = 4;
/* Offset in Sp3c mempool for agency string */
constexpr const int AGENCY_OF = CRD_SYS_SZ + ORB_TYP_SZ;
/* number of chars in agency string (including null-terminating
 * char); Example: "_NGS", format: [A4] + '\0'
 */
constexpr const int AGENCY_SZ = 5;
/* Offset in Sp3c mempool for time system string */
constexpr const int TME_SYS_OF = CRD_SYS_SZ + ORB_TYP_SZ + AGENCY_SZ;
/* number of chars in time system string (including null-terminating
 * char); Example: "GPS", format: [A3] + '\0'
 */
constexpr const int TME_SYS_SZ = 4;
/* the char in Sp3c mempool that holds iterators referencing the instance. */
constexpr const int ITERATOR_REF = MEMPOOL_SIZE_CHAR - 1;

/* (static checks) */
static_assert(ITERATOR_REF < MEMPOOL_SIZE_CHAR);
static_assert(ITERATOR_REF > TME_SYS_OF + TME_SYS_SZ);

/** @class Sp3DataBlock
 * Instances of this class, hold Sp3 data records for one block (aka one
 * epoch) and one satellite.
 */
struct Sp3SvDataBlock {
  Sp3Flag flag; /** flag for state */
  SatelliteId id_;
  double state[8];      /** [ X, Y, Z, clk, Vx, Vy, Vz, Vc ] */
  double state_sdev[8]; /** following state__ */

  Sp3SvDataBlock() : flag(), id_() {};

  Sp3SvDataBlock(SatelliteId inid, Sp3Flag inflag, const double *instate,
                 const double *instatedev) noexcept {
    std::memcpy(state, instate, sizeof(double) * 8);
    std::memcpy(state_sdev, instatedev, sizeof(double) * 8);
    flag = inflag;
    id_ = inid;
  }

  void update_position(Sp3Flag inflag, const double *inpos,
                       const double *inposdev) noexcept {
    std::memcpy(state, inpos, sizeof(double) * 4);
    std::memcpy(state_sdev, inposdev, sizeof(double) * 4);
    /* bitwise OR, aka turn ON every event that is on currently or in inflag */
    flag |= inflag;
  }

  void update_velocity(Sp3Flag inflag, const double *invel,
                       const double *inveldev) noexcept {
    std::memcpy(state + 4, invel, sizeof(double) * 4);
    std::memcpy(state_sdev + 4, inveldev, sizeof(double) * 4);
    flag |= inflag;
  }
}; /* struct Sp3SvDataBlock */

/* wee need Sp3SvDataBlock to be trivially_copy_assignable (we'll be sopying
 * using memmove)*/
static_assert(
    std::is_trivially_copy_assignable_v<dso::sp3_details::Sp3SvDataBlock>);

struct Sp3DataBlock {
  dso::datetime<dso::nanoseconds> t_{dso::datetime<dso::nanoseconds>::min()};
  std::vector<Sp3SvDataBlock> blocks_;

  void clear() noexcept {
    t_ = dso::datetime<dso::nanoseconds>::min();
    blocks_.clear();
  }

  const dso::datetime<dso::nanoseconds> &t() const noexcept { return t_; }

  std::vector<Sp3SvDataBlock>::iterator sat_block(SatelliteId id) noexcept {
    return std::find_if(
        blocks_.begin(), blocks_.end(),
        [=](const Sp3SvDataBlock &blc) { return id == blc.id_; });
  }
  std::vector<Sp3SvDataBlock>::const_iterator
  sat_block(SatelliteId id) const noexcept {
    return std::find_if(
        blocks_.cbegin(), blocks_.cend(),
        [=](const Sp3SvDataBlock &blc) { return id == blc.id_; });
  }
}; /* struct Sp3DataBlock */
} // namespace sp3_details

class Sp3c {
public:
  /** Let's not write this more than once. */
  typedef std::ifstream::pos_type pos_type;

  /** @brief Constructor from filename */
  explicit Sp3c(const char *fn);

  /** @brief Copy not allowed ! */
  Sp3c(const Sp3c &) = delete;

  /** @brief Assignment not allowed ! */
  Sp3c &operator=(const Sp3c &) = delete;

  /** @brief Move Constructor. */
  Sp3c(Sp3c &&a) noexcept(
      std::is_nothrow_move_constructible<std::ifstream>::value) = default;

  /** @brief Move assignment operator. */
  Sp3c &operator=(Sp3c &&a) noexcept(
      std::is_nothrow_move_assignable<std::ifstream>::value) = default;

  /** get the Sp3 interval */
  auto interval() const noexcept { return interval_; }

  /** get the number of epochs included in the file */
  auto num_epochs() const noexcept { return num_epochs_; }

  /** get the initial epoch (datetime) in the Sp3 file */
  auto start_epoch() const noexcept { return start_epoch_; }

  /** Rewind to the start of data blocks (i.e. just after the header) */
  void rewind() noexcept { istream_.seekg(end_of_head_, std::ios::beg); }

  /** @brief Time System/Scale as string (as reported in the Sp3). */
  [[nodiscard]]
  const char *time_sys() const noexcept {
    return &(cmempool_[sp3_details::TME_SYS_OF]);
  }

  /** @brief Checks if this instance is used/referenced by an iterator */
  [[nodiscard]]
  bool referenced_by_iterator() const noexcept {
    return cmempool_[sp3_details::ITERATOR_REF];
  }

  /** @brief Read the next data block and parse holding for a given SV
   * @param[in] satid The SV to collect records for
   * @param[out] block An Sp3DataBlock instance; if we encounter records
   *            for SV satid, block will be filled with the parsed values.
   *            Use the block's flag member to check which values where
   *            actually parsed (if any at all).
   *            Units are :
   *            * State Vector: [km] and [dm/sec]
   *            * Std. Deviations: [mm] and 10e-4[mm/sec]
   * @return -1: EOF encountered
   *          0: All ok
   *         >0: ERROR
   */
  [[nodiscard]]
  int get_next_block(sp3_details::Sp3DataBlock &block,
                     const sp3_details::SatelliteId *satid) noexcept;

  /** @brief Check if a given SV in included in the Sp3 (i.e. is included in
   *        the instance's sat_vec__ member).
   *
   * @note  It is assumed that the header of the file is already parsed; this
   *        should always be the case, since the file's header is parsed when
   *        it is constructed.
   *
   * @param[in] satid The satelliteid we are searching for, as recorded in Sp3
   *            (aka a 3-char id, as'G01', 'R27', etc)
   * @return True if satellite is included in the instance's sat_vec__; false
   *         otherwise.
   */
  bool has_sv(sp3_details::SatelliteId satid) const noexcept {
    using sp3_details::SatelliteId;
    return std::find_if(sat_vec_.cbegin(), sat_vec_.cend(),
                        [satid](const SatelliteId &s) { return s == satid; }) !=
           sat_vec_.cend();
  }

  /** @brief Number of satellites in sp3 file
   *
   * @note  It is assumed that the header of the file is already parsed; this
   *        should always be the case, since the file's header is parsed when
   *        it is constructed.
   */
  int num_sats() const noexcept { return sat_vec_.size(); }

  /** @brief Return the vector of satellites included in the sp3 file */
  std::vector<sp3_details::SatelliteId> sattellite_vector() const noexcept {
    return sat_vec_;
  }

  /** @brief Return the vector of satellites included in the sp3 file */
  std::vector<sp3_details::SatelliteId> &sattellite_vector() noexcept {
    return sat_vec_;
  }

private:
  /** @brief Read sp3c header; assign info */
  [[nodiscard]]
  int read_header() noexcept;

  /** @brief Resolve an Epoch Header Record line */
  [[nodiscard]]
  int resolve_epoch_line(const char *line,
                         dso::datetime<dso::nanoseconds> &t) noexcept;

  /** @brief Get and resolve the next Position and Clock Record */
  [[nodiscard]]
  int get_next_position(
      const char *line, sp3_details::SatelliteId &sat, double *pos,
      double *pos_stddev, Sp3Flag &flag,
      const sp3_details::SatelliteId *wsat = nullptr) noexcept;

  /** @brief Get and resolve the next Velocity and ClockRate-of-Change Record */
  [[nodiscard]]
  int get_next_velocity(
      const char *line, sp3_details::SatelliteId &sat, double *vel,
      double *vel_stddev, Sp3Flag &flag,
      const sp3_details::SatelliteId *wsat = nullptr) noexcept;

  /** @brief Set the flag "used/referenced by an iterator" */
  void referenced_by_iterator(char c) noexcept {
    cmempool_[sp3_details::ITERATOR_REF] = c;
  }

  /** @brief Time System/Scale as string (as reported in the Sp3). */
  char *time_sys() noexcept { return &(cmempool_[sp3_details::TME_SYS_OF]); }
  char *crd_system() noexcept { return &(cmempool_[sp3_details::CRD_SYS_OF]); }
  char *agency() noexcept { return &(cmempool_[sp3_details::AGENCY_OF]); }
  char *orbit_type() noexcept { return &(cmempool_[sp3_details::ORB_TYP_OF]); }

  /** The name of the file */
  std::string filename_;
  /** The infput (file) stream */
  std::ifstream istream_;
  /** the version 'c' or 'd' */
  char version_;
  /** Start epoch */
  dso::datetime<dso::nanoseconds> start_epoch_;
  /** Number of epochs in file */
  int num_epochs_,
      /** Number od SVs in file */
      num_sats_;
  char cmempool_[sp3_details::MEMPOOL_SIZE_CHAR];
  /** Epoch interval */
  dso::nanoseconds interval_;
  /** Mark the 'END OF HEADER' field */
  pos_type end_of_head_;
  /** Vector of satellite id's */
  std::vector<sp3_details::SatelliteId> sat_vec_;
  /** floating point base for position std. dev (mm or 10**-4 mm/sec) */
  double fpb_pos_,
      /** floating point base for clock std. dev (psec or 10**-4 psec/sec) */
      fpb_clk_;

  struct iterator {
    using value_type = sp3_details::Sp3DataBlock;
    using pointer = const sp3_details::Sp3DataBlock *;
    using reference = const sp3_details::Sp3DataBlock &;

    friend class Sp3c;

    /* end iterator */
    iterator() noexcept = default;

    /* iterator-like behaviour */
    reference operator*() const noexcept { return current_; }
    pointer operator->() const noexcept { return &current_; }

    iterator &operator++() {
      if (!sp3_)
        return *this;
      int error = sp3_->get_next_block(current_, nullptr);
      if (error) {
        sp3_->referenced_by_iterator('\0');
        sp3_ = nullptr;
        if (error > 0)
          throw std::runtime_error(
              "[ERROR] Failed advancing Sp3c::iterator!\n");
      }
      return *this;
    }

    /* @warning Only use to compare with end(). does not so anything
     * usefull otherwise!
     */
    friend bool operator==(const iterator &a, const iterator &b) noexcept {
      return a.sp3_ == b.sp3_;
    }

    /* @warning Only use to compare with end(). does not so anything
     * usefull otherwise!
     */
    friend bool operator!=(const iterator &a, const iterator &b) noexcept {
      return !(a == b);
    }

    ~iterator() noexcept {
      if (sp3_)
        sp3_->referenced_by_iterator('\0');
    }

    explicit iterator(Sp3c &sp3) : current_{}, sp3_(&sp3) {
      if (sp3_->referenced_by_iterator()) {
        throw std::runtime_error(
            "[ERROR] Cannot construct an iterator to Sp3 instance (" +
            sp3_->filename_ +
            "); another "
            "iterator already references it! (traceback: " +
            std::string(__func__) + ")\n");
      }
      /* Make begin() valid by reading the first block now */
      sp3_->referenced_by_iterator('1');
      sp3_->rewind();
      this->operator++();
    }

    iterator(const iterator &) = delete;
    iterator &operator=(const iterator &) = delete;
    iterator(iterator &&other) noexcept
        : current_(other.current_), sp3_(other.sp3_) {
      other.sp3_ = nullptr;
    }
    iterator &operator=(iterator &&other) {
      if (sp3_)
        sp3_->referenced_by_iterator('\0');
      if (other.sp3_) {
        sp3_ = other.sp3_;
        sp3_->referenced_by_iterator('1');
      } else {
        sp3_ = nullptr;
      }
      current_ = other.current_;
      return *this;
    }

  private:
    value_type current_;
    Sp3c *sp3_;

  }; /* class iterator */

public:
  /* lvalue-only begin/end (safe) */
  iterator begin() & noexcept { return iterator{*this}; }
  iterator end() & noexcept { return iterator{}; }

  /* forbid begin()/end() on temporaries
   * e.g. for (auto b : Sp3c("foo")) {}  // → dangling iterator
   *  or  std::move(mysp3).begin();
   */
  iterator begin() && = delete;
  iterator end() && = delete;
  iterator begin() const && = delete;
  iterator end() const && = delete;
}; /* class Sp3c */

} /* namespace dso */

#endif
