// Hossein Moein
// October 30, 2019
/*
Copyright (c) 2019-2026, Hossein Moein
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of Hossein Moein and/or the DataFrame nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Hossein Moein BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <DataFrame/DataFrameStatsVisitors.h>
#include <DataFrame/Vectors/VectorPtrView.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <limits>
#include <numeric>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

// ----------------------------------------------------------------------------

namespace hmdf
{

// One pass simple linear regression
//
template<typename T, typename I = unsigned long,
         typename =
             typename std::enable_if<supports_arithmetic<T>::value, T>::type>
struct SLRegressionVisitor  {

public:

    DEFINE_VISIT_BASIC_TYPES_2

    inline void operator() (const index_type &idx,
                            const value_type &x, const value_type &y)  {

        if (skip_nan_ && (is_nan__(x) || is_nan__(y)))  return;

        s_xy_ += (x_stats_.get_mean() - x) *
                 (y_stats_.get_mean() - y) *
                 value_type(n_) / value_type(n_ + 1);

        x_stats_(idx, x);
        y_stats_(idx, y);
        n_ += 1;
    }
    PASS_DATA_ONE_BY_ONE_2

    inline void pre ()  {

        n_ = 0;
        s_xy_ = 0;
        x_stats_.pre();
        y_stats_.pre();
    }
    inline void post ()  {  }

    inline size_type get_count () const { return (n_); }
    inline result_type get_slope () const  {

        // Sum of the squares of the difference between each x and
        // the mean x value.
        //
        const value_type    s_xx =
            x_stats_.get_variance() * value_type(n_ - 1);

        return (s_xy_ / s_xx);
    }
    inline result_type get_intercept () const  {

        return (y_stats_.get_mean() - get_slope() * x_stats_.get_mean());
    }
    inline result_type get_corr () const  {

        const value_type    t = x_stats_.get_std() * y_stats_.get_std();

        return (s_xy_ / (value_type(n_ - 1) * t));
    }

    explicit SLRegressionVisitor(bool skipnan = true)
        : x_stats_(skipnan), y_stats_(skipnan), skip_nan_(skipnan)  {   }

private:

    size_type                               n_ { 0 };

    // Sum of the product of the difference between x and its mean and
    // the difference between y and its mean.
    //
    value_type                              s_xy_ { 0 };
    StatsVisitor<value_type, index_type>    x_stats_ {  };
    StatsVisitor<value_type, index_type>    y_stats_ {  };
    const bool                              skip_nan_;
};

// ----------------------------------------------------------------------------

template<std::size_t K,
         typename T, typename I = unsigned long, std::size_t A = 0>
struct  KMeansVisitor  {

public:

    DEFINE_VISIT_BASIC_TYPES

    using result_type = std::array<value_type, K>;
    using cluster_type = std::array<VectorPtrView<value_type, A>, K>;
    using distance_func =
        std::function<double(const value_type &x, const value_type &y)>;

private:

    const size_type iter_num_;
    const bool      cc_;
    distance_func   dfunc_;
    result_type     result_ { };    // K Means
    cluster_type    clusters_ { };  // K Clusters

    template<typename H>
    inline void calc_k_means_(const H &column_begin, size_type col_s)  {

        std::random_device                          rd;
        std::mt19937                                gen(rd());
        std::uniform_int_distribution<size_type>    rd_gen(0, col_s - 1);

        // Pick centroids as random points from the col.
        for (auto &k_mean : result_)  {
            const value_type    &value = *(column_begin + rd_gen(gen));

            if (is_nan__(value))  continue;
            k_mean = value;
        }

        std::vector<size_type,
                    typename allocator_declare<size_type, A>::type>
                        assignments(col_s, 0);

        for (size_type iter = 0; iter < iter_num_; ++iter) {
            result_type             new_means { value_type() };
            std::array<double, K>   counts { 0.0 };

            // Find assignments.
            for (size_type point = 0; point < col_s; ++point) {
                const value_type    &value = *(column_begin + point);

                if (is_nan__(value))  continue;

                double      best_distance = std::numeric_limits<double>::max();
                size_type   best_cluster = 0;

                for (size_type cluster = 0; cluster < K; ++cluster) {
                    const double    distance = dfunc_(value, result_[cluster]);

                    if (distance < best_distance) {
                        best_distance = distance;
                        best_cluster = cluster;
                    }
                }
                assignments[point] = best_cluster;

                // Sum up and count points for each cluster.
                //
                const size_type cluster = assignments[point];

                new_means[cluster] = new_means[cluster] + value;
                counts[cluster] += 1.0;
            }

            bool    done = true;

            // Divide sums by counts to get new centroids.
            //
            for (size_type cluster = 0; cluster < K; ++cluster) {
                // Turn 0/0 into 0/1 to avoid zero division.
                const double        count =
                    std::max<double>(1.0, counts[cluster]);
                const value_type    value = new_means[cluster] / count;
                value_type          &result = result_[cluster];

                if (dfunc_(value, result) > 0.0000001)  {
                    done = false;
                    result = value;
                }
            }

            if (done)  break;
        }
    }

    // Using the calculated means, separate the given column into clusters
    //
    template<typename H>
    inline void
    calc_clusters_(const H &column_begin, size_type col_s)  {

        cluster_type    clusters;

        for (size_type i = 0; i < K; ++i)  {
            clusters[i].reserve(col_s / K + 2);
            clusters[i].push_back(const_cast<value_type *>(&(result_[i])));
        }

        for (size_type j = 0; j < col_s; ++j)  {
            const value_type    &value = *(column_begin + j);

            if (is_nan__(value))  continue;

            double      min_dist = std::numeric_limits<double>::max();
            size_type   min_idx;

            for (size_type i = 0; i < K; ++i)  {
                const double    dist = dfunc_(value, result_[i]);

                if (dist < min_dist)  {
                    min_dist = dist;
                    min_idx = i;
                }
            }
            clusters[min_idx].push_back(const_cast<value_type *>(&value));
        }

        clusters_.swap(clusters);
        return;
    }

public:

    template<typename IV, typename H>
    inline void
    operator() (const IV &idx_begin, const IV &idx_end,
                const H &column_begin, const H &column_end)  {

        GET_COL_SIZE

        calc_k_means_(column_begin, col_s);
        if (cc_)
            calc_clusters_(column_begin, col_s);
    }

    inline void pre ()  { for (auto &iter : clusters_) iter.clear();  }
    inline void post ()  {  }
    inline const result_type &get_result () const  { return (result_); }
    inline result_type &get_result ()  { return (result_); }
    inline const cluster_type &get_clusters () const  { return (clusters_); }
    inline cluster_type &get_clusters ()  { return (clusters_); }

    explicit
    KMeansVisitor(
        size_type num_of_iter,
        bool calc_clusters = true,
        distance_func f =
            [](const value_type &x, const value_type &y) -> double  {
                return ((x - y) * (x - y));
            })
        : iter_num_(num_of_iter), cc_(calc_clusters), dfunc_(f)  {   }
};

// ----------------------------------------------------------------------------

template<typename T, typename I = unsigned long, std::size_t A = 0>
struct  AffinityPropVisitor  {

public:

    DEFINE_VISIT_BASIC_TYPES

    template<typename U>
    using vec_t = std::vector<U, typename allocator_declare<U, A>::type>;

    using result_type = VectorPtrView<value_type, A>;
    using cluster_type = vec_t<VectorPtrView<value_type, A>>;
    using distance_func =
        std::function<double(const value_type &x, const value_type &y)>;

private:

    const size_type iter_num_;
    distance_func   dfunc_;
    const double    dfactor_;
    result_type     result_ { };  // Centers

    template<typename H>
    inline vec_t<double>
    get_similarity_(const H &column_begin, size_type csize)  {

        vec_t<double>   simil((csize * (csize + 1)) / 2, 0.0);
        double          min_dist = std::numeric_limits<double>::max();

        // Compute similarity between distinct data points i and j
        for (size_type i = 0; i < csize - 1; ++i)  {
            const value_type    &i_val = *(column_begin + i);

            for (size_type j = i + 1; j < csize; ++j)  {
                const double    dist = -dfunc_(i_val, *(column_begin + j));

                simil[(i * csize) + j - ((i * (i + 1)) >> 1)] = dist;
                if (dist < min_dist)  min_dist = dist;
            }
        }

        // Assign min to diagonals
        for (size_type i = 0; i < csize; ++i)
            simil[(i * csize) + i - ((i * (i + 1)) >> 1)] = min_dist;

        return (simil);
    }

    inline void
    get_avail_and_respon(const vec_t<double> &simil,
                         size_type csize,
                         vec_t<double> &avail,
                         vec_t<double> &respon)  {

        avail.resize(csize * csize, 0.0);
        respon.resize(csize * csize, 0.0);

        for (size_type m = 0; m < iter_num_; ++m)  {
            // Update responsibility
            for (size_type i = 0; i < csize; ++i)  {
                for (size_type j = 0; j < csize; ++j)  {
                    double  max_diff = -std::numeric_limits<double>::max();

                    for (size_type jj = 0; jj < csize; ++jj)  {
                        if (jj ^ j)   {
                            const double    value =
                                simil[(i * csize) + jj - ((i * (i + 1)) >> 1)] +
                                avail[jj * csize + i];

                            if (value > max_diff)
                                max_diff = value;
                        }
                    }

                    respon[j * csize + i] =
                        (1.0 - dfactor_) *
                        (simil[(i * csize) + j - ((i * (i + 1)) >> 1)] -
                        max_diff) +
                        dfactor_ * respon[j * csize + i];
                }
            }

            // Update availability
            // Do diagonals first
            for (size_type i = 0; i < csize; ++i)  {
                const size_type s1 = i * csize;
                double          sum = 0.0;

                for (size_type ii = 0; ii < csize; ++ii)
                    if (ii ^ i)
                        sum += std::max(0.0, respon[s1 + ii]);

                avail[s1 + i] =
                    (1.0 - dfactor_) * sum + dfactor_ * avail[s1 + i];
            }
            for (size_type i = 0; i < csize; ++i)  {
                for (size_type j = 0; j < csize; ++j)  {
                    if (i ^ j)  {  // Not equal
                        const size_type s1 = j * csize;
                        double          sum = 0.0;
                        const size_type max_i_j = std::max(i, j);
                        const size_type min_i_j = std::min(i, j);

                        for (size_type ii = 0; ii < min_i_j; ++ii)
                            sum += std::max(0.0, respon[s1 + ii]);
                        for (size_type ii = min_i_j + 1; ii < max_i_j; ++ii)
                            sum += std::max(0.0, respon[s1 + ii]);
                        for (size_type ii = max_i_j + 1; ii < csize; ++ii)
                            sum += std::max(0.0, respon[s1 + ii]);

                        avail[s1 + i] =
                            (1.0 - dfactor_) *
                            std::min(0.0, respon[s1 + j] + sum) + dfactor_ *
                            avail[s1 + i];
                    }
                }
            }
        }

        return;
    }

public:

    template<typename IV, typename H>
    inline void
    operator() (const IV &idx_begin, const IV &idx_end,
                const H &column_begin, const H &column_end)  {

        GET_COL_SIZE
        const vec_t<double> simil =
            std::move(get_similarity_(column_begin, col_s));
        vec_t<double>       avail;
        vec_t<double>       respon;

        get_avail_and_respon(simil, col_s, avail, respon);

        result_.reserve(std::min(col_s / 100, size_type(16)));
        for (size_type i = 0; i < col_s; ++i)  {
            if (respon[i * col_s + i] + avail[i * col_s + i] > 0.0)
                result_.push_back(
                    const_cast<value_type *>(&*(column_begin + i)));
        }
    }

    // Using the calculated means, separate the given column into clusters
    //
    template<typename IV, typename H>
    inline cluster_type
    get_clusters(const IV &idx_begin,
                 const IV &idx_end,
                 const H &column_begin,
                 const H &column_end)  {

        GET_COL_SIZE
        const size_type centers_size = result_.size();
        cluster_type    clusters;

        if (centers_size > 0)  {
            clusters.resize(centers_size);
            for (size_type i = 0; i < centers_size; ++i)
                clusters[i].reserve(col_s / centers_size);

            for (size_type j = 0; j < col_s; ++j)  {
                double              min_dist =
                    std::numeric_limits<double>::max();
                size_type           min_idx;
                const value_type    &j_val = *(column_begin + j);

                for (size_type i = 0; i < centers_size; ++i)  {
                    const double    dist = dfunc_(j_val, result_[i]);

                    if (dist < min_dist)  {
                        min_dist = dist;
                        min_idx = i;
                    }
                }
                clusters[min_idx].push_back(const_cast<value_type *>(&j_val));
            }
        }

        return (clusters);
    }

    DEFINE_PRE_POST
    DEFINE_RESULT

    explicit
    AffinityPropVisitor(
        size_type num_of_iter,
        distance_func f =
            [](const value_type &x, const value_type &y) -> double {
                return ((x - y) * (x - y));
            },
        double damping_factor = 0.9)
        : iter_num_(num_of_iter), dfunc_(f), dfactor_(damping_factor)  {   }
};

// ----------------------------------------------------------------------------

template<typename T, typename I = unsigned long, std::size_t A = 0,
         typename =
             typename std::enable_if<supports_arithmetic<T>::value, T>::type>
struct FastFourierTransVisitor {

public:

    DEFINE_VISIT_BASIC_TYPES

    template<typename U>
    using vec_t = std::vector<U, typename allocator_declare<U, A>::type>;
    using result_type =
        typename std::conditional<is_complex<T>::value,
                                  vec_t<T>,
                                  vec_t<std::complex<T>>>::type;
    using real_t = typename result_type::value_type::value_type;

private:

    using cplx_t = typename result_type::value_type;

    static inline result_type convolve_(result_type xvec, result_type yvec)  {

        transform_(xvec, false);
        transform_(yvec, false);

        std::transform(xvec.begin(), xvec.end(),
                       yvec.begin(), xvec.begin(),
                       std::multiplies<cplx_t>());

        transform_(xvec, true);

        const real_t    col_s  = real_t(xvec.size());

        std::transform(xvec.begin(), xvec.end(),
                       xvec.begin(),
                       [col_s] (const cplx_t &v) -> cplx_t {
                           return (v / col_s);
                       });
        return (xvec);
    }

    static inline size_type reverse_bits_(size_type val, size_type width) {

        size_type   result { 0 };

        for (size_type i = 0; i < width; i++, val >>= 1)
            result = (result << 1) | (val & 1U);
        return (result);
    }

    static inline void fft_radix2_(result_type &column, bool reverse) {

        const size_type col_s { column.size() };
        size_type       levels { 0 };  // Compute levels = floor(log2(col_s))

        for (size_type i = col_s; i > 1; i >>= 1)
            levels += 1;

        // Trigonometric table
        //
        const size_type half_col_s { col_s / 2 };
        const real_t    two_pi
            { (reverse ? real_t(2) : -real_t(2)) * real_t(M_PI) };
        result_type     exp_table (half_col_s);

        for (size_type i = 0; i < half_col_s; i++)
            exp_table[i] =
                std::polar(real_t(1), two_pi * real_t(i) / real_t(col_s));

        // Bit-reversed addressing permutation
        //
        for (size_type i = 0; i < col_s; i++) {
            const size_type rb { reverse_bits_(i, levels) };

            if (rb > i)  std::swap(column[i], column[rb]);
        }

        // Cooley-Tukey decimation-in-time radix-2 FFT
        //
        for (size_type s = 2; s <= col_s; s *= 2) {
            const size_type half_size { s / 2 };
            const size_type table_step { col_s / s };

            for (size_type i = 0; i < col_s; i += s) {
                for (size_type j = i, k = 0; j < i + half_size;
                     j++, k += table_step) {
                    const cplx_t    temp
                        { column[j + half_size] * exp_table[k] };

                    column[j + half_size] = column[j] - temp;
                    column[j] += temp;
                }
            }
        }
    }

    static inline void fft_bluestein_(result_type &column, bool reverse) {

        const size_type col_s { column.size() };

        // Trigonometric table
        //
        result_type     exp_table (col_s);
        const size_type col_s_2 { col_s * 2 };
        const real_t    pi { reverse ? real_t(M_PI) : -real_t(M_PI) };

        for (size_type i = 0; i < col_s; i++) {
            const real_t    sq = real_t((i * i) % col_s_2);

            exp_table[i] = std::polar(real_t(1), pi * sq / real_t(col_s));
        }

        // Find a power of 2 convolution length m such that m >= col_s * 2 + 1
        //
        size_type   m { 1 };

        while (m / 2 <= col_s)   m *= 2;

        // Temporary vectors and preprocessing
        //
        result_type xvec (m, cplx_t(0, 0));

        for (size_type i = 0; i < col_s; i++)
            xvec[i] = column[i] * exp_table[i];

        result_type yvec(m, cplx_t(0, 0));

        yvec[0] = exp_table[0];
        for (size_type i = 1; i < col_s; i++)
            yvec[i] = yvec[m - i] = std::conj(exp_table[i]);

        // Convolution
        //
        const result_type   conv (convolve_(std::move(xvec), std::move(yvec)));

        // Postprocessing
        //
        std::transform(exp_table.begin(), exp_table.end(),
                       conv.begin(), column.begin(),
                       std::multiplies<cplx_t>());
    }

    static inline void transform_(result_type &column, bool reverse) {

        const size_type col_s { column.size() };

        if (col_s == 0)
            return;
        if ((col_s & (col_s - 1)) == 0)  // Is power of 2
            fft_radix2_(column, reverse);
        else  // More complicated algorithm for arbitrary sizes
            fft_bluestein_(column, reverse);
    }

    static inline void itransform_(result_type &column)  {

        // Conjugate the complex numbers
        //
        std::transform(column.begin(), column.end(),
                       column.begin(),
                       [] (const cplx_t &v) -> cplx_t {
                           return (std::conj(v));
                       });

        const size_type col_s { column.size() };

        // Forward fft
        //
        if ((col_s & (col_s - 1)) == 0)  // Is power of 2
            fft_radix2_(column, false);
        else  // More complicated algorithm for arbitrary sizes
            fft_bluestein_(column, false);

        // Conjugate the complex numbers again
        //
        std::transform(column.begin(), column.end(),
                       column.begin(),
                       [] (const cplx_t &v) -> cplx_t {
                           return (std::conj(v));
                       });

        // Scale the numbers
        //
        std::transform(column.begin(), column.end(),
                       column.begin(),
                       [col_s] (const cplx_t &v) -> cplx_t {
                           return (v / real_t(col_s));
                       });
    }

public:

    template <typename K, typename H>
    inline void
    operator() (const K &idx_begin, const K &idx_end,
                const H &column_begin, const H &column_end)  {

        GET_COL_SIZE
        result_type result (col_s);

        if constexpr (is_complex<T>::value)  {
            std::transform(column_begin, column_end,
                           result.begin(),
                           [] (T v) -> cplx_t { return (v); });
        }
        else  {
            std::transform(column_begin, column_end,
                           result.begin(),
                           [] (T v) -> cplx_t {
                               return (std::complex<T>(v, 0));
                           });
        }

        if (inverse_)
            itransform_(result);
        else
            transform_(result, false);
        result_.swap(result);
    }

    inline void pre ()  {

        result_.clear();
        magnitude_.clear();
        angle_.clear();
    }
    inline void post ()  {  }

    DEFINE_RESULT
    inline const vec_t<real_t> &
    get_magnitude() const  {

        return (const_cast<FastFourierTransVisitor<T, I> *>
                    (this)->get_magnitude());
    }
    inline vec_t<real_t> &
    get_magnitude()  {

        if (magnitude_.empty())  {
            magnitude_.reserve(result_.size());
            for (const auto &citer : result_)
                magnitude_.push_back(std::sqrt(std::norm(citer)));
        }
        return (magnitude_);
    }
    inline const vec_t<real_t> &
    get_angle() const  {

        return (const_cast<FastFourierTransVisitor<T, I> *>
                    (this)->get_angle());
    }
    inline vec_t<real_t> &
    get_angle()  {

        if (angle_.empty())  {
            angle_.reserve(result_.size());
            for (const auto &citer : result_)
                angle_.push_back(std::arg(citer));
        }
        return (angle_);
    }

    explicit
    FastFourierTransVisitor(bool inverse = false) : inverse_(inverse)  {   }

private:

    const bool      inverse_;
    result_type     result_ {  };
    vec_t<real_t>   magnitude_ {  };
    vec_t<real_t>   angle_ {  };
};

template<typename T, typename I = unsigned long, std::size_t A = 0>
using fft_v = FastFourierTransVisitor<T, I, A>;

// ----------------------------------------------------------------------------

template<typename T, typename I = unsigned long, std::size_t A = 0,
         typename =
             typename std::enable_if<supports_arithmetic<T>::value, T>::type>
struct  EntropyVisitor  {

    DEFINE_VISIT_BASIC_TYPES_3

    template <typename K, typename H>
    inline void
    operator() (const K &idx_begin, const K &idx_end,
                const H &column_begin, const H &column_end)  {

        if (roll_count_ == 0)  return;

        GET_COL_SIZE

        SimpleRollAdopter<SumVisitor<T, I>, T, I, A>  sum_v(SumVisitor<T, I>(),
                                                            roll_count_);

        sum_v.pre();
        sum_v (idx_begin, idx_end, column_begin, column_end);
        sum_v.post();

        result_type result = std::move(sum_v.get_result());

        for (size_type i = 0; i < col_s; ++i)  {
            const value_type    val = *(column_begin + i) / result[i];

            result[i] = -val * std::log(val) / std::log(log_base_);
        }

        sum_v.pre();
        sum_v (idx_begin + (roll_count_ - 1), idx_end,
               result.begin() + (roll_count_ - 1), result.end());
        sum_v.post();

        for (size_type i = 0; i < roll_count_ - 1; ++i)
            result[i] = get_nan<value_type>();
        for (size_type i = 0; i < sum_v.get_result().size(); ++i)
            result[i + roll_count_ - 1] = sum_v.get_result()[i];

        result_.swap(result);
    }

    DEFINE_PRE_POST
    DEFINE_RESULT

    explicit
    EntropyVisitor(size_type roll_count, value_type log_base = 2)
        : roll_count_(roll_count), log_base_(log_base)  {   }

private:

    const size_type     roll_count_;
    const value_type    log_base_;
    result_type         result_ { };
};

template<typename T, typename I = unsigned long, std::size_t A = 0>
using ent_v = EntropyVisitor<T, I, A>;

// ----------------------------------------------------------------------------

template<typename T, typename I = unsigned long, std::size_t A = 0>
struct  ImpurityVisitor  {

    DEFINE_VISIT_BASIC_TYPES

    using result_type =
        std::vector<double, typename allocator_declare<double, A>::type>;

    template <typename K, typename H>
    inline void
    operator() (const K &idx_begin, const K &idx_end,
                const H &column_begin, const H &column_end)  {

        GET_COL_SIZE

        if (roll_count_ == 0 || roll_count_ > col_s)  return;

        map_t   table (roll_count_ / 2 + 1);

        for (size_type i = 0; i < roll_count_; ++i)  {
            auto    ret = table.insert(std::pair(*(column_begin + i), 0));

            ret.first->second += 1.0;
        }

        result_type result;

        result.reserve(col_s);
        for (size_type i = 1; i < col_s; ++i)  {
            double  sum = 0;

            if (imt_ == impurity_type::gini_index)  {
                for (const auto &citer : table)  {
                    const auto  prob = citer.second / double(roll_count_);

                    sum += prob * prob;
                }
                sum = 1.0 - sum;
            }
            else  {  // impurity_type::info_entropy
                for (const auto &citer : table)  {
                    const auto  prob = citer.second / double(roll_count_);

                    sum += prob * std::log2(prob);
                }
                sum = -sum;
            }
            result.push_back(sum);

            const size_type roll_end = i + roll_count_;

            if (roll_end > col_s)  break;

            auto    find_ret = table.find(*(column_begin + (i - 1)));

            find_ret->second -= 1.0;  // It must find it -- no need to check
            if (find_ret->second == 0)
                table.erase(find_ret);

            auto    insert_ret =
                table.insert(std::pair(*(column_begin + (roll_end - 1)), 0));

            insert_ret.first->second += 1.0;
        }

        result_.swap(result);
    }

    DEFINE_PRE_POST
    DEFINE_RESULT

    ImpurityVisitor(size_type roll_count, impurity_type it)
        : roll_count_(roll_count), imt_(it)  {   }

private:

    using map_t = std::unordered_map<
        T, double,
        std::hash<T>,
        std::equal_to<T>,
        typename allocator_declare<std::pair<const T, double>, A>::type>;

    result_type         result_ { };
    const size_type     roll_count_;
    const impurity_type imt_;
};

template<typename T, typename I = unsigned long, std::size_t A = 0>
using impu_v = ImpurityVisitor<T, I, A>;

// ----------------------------------------------------------------------------

template<typename T, typename I = unsigned long, std::size_t A = 0,
         typename =
             typename std::enable_if<supports_arithmetic<T>::value, T>::type>
struct SigmoidVisitor {

    DEFINE_VISIT_BASIC_TYPES_3

private:

    template <typename H>
    inline void logistic_(const H &column_begin, const H &column_end)  {

        for (auto citer = column_begin; citer < column_end; ++citer)
            result_.push_back(1.0 / (1.0 + std::exp(-(*citer))));
    }
    template <typename H>
    inline void algebraic_(const H &column_begin, const H &column_end)  {

        for (auto citer = column_begin; citer < column_end; ++citer)
            result_.push_back(1.0 / std::sqrt(1.0 + std::pow(*citer, 2.0)));
    }
    template <typename H>
    inline void hyperbolic_tan_(const H &column_begin, const H &column_end)  {

        for (auto citer = column_begin; citer < column_end; ++citer)
            result_.push_back(std::tanh(*citer));
    }
    template <typename H>
    inline void arc_tan_(const H &column_begin, const H &column_end)  {

        for (auto citer = column_begin; citer < column_end; ++citer)
            result_.push_back(std::atan(*citer));
    }
    template <typename H>
    inline void error_function_(const H &column_begin, const H &column_end)  {

        for (auto citer = column_begin; citer < column_end; ++citer)
            result_.push_back(std::erf(*citer));
    }
    template <typename H>
    inline void gudermannian_(const H &column_begin, const H &column_end)  {

        for (auto citer = column_begin; citer < column_end; ++citer)
            result_.push_back(std::atan(std::sinh(*citer)));
    }
    template <typename H>
    inline void smoothstep_(const H &column_begin, const H &column_end)  {

        for (auto citer = column_begin; citer < column_end; ++citer)  {
            if (*citer <= 0.0)
                result_.push_back(0.0);
            else if (*citer >= 1.0)
                result_.push_back(1.0);
            else
                result_.push_back(*citer * *citer * (3.0 - 2.0 * *citer));
        }
    }

public:

    template <typename K, typename H>
    inline void
    operator() (const K &, const K &,
                const H &column_begin, const H &column_end)  {

        result_.reserve(std::distance(column_begin, column_end));
        if (sigmoid_type_ == sigmoid_type::logistic)
            logistic_(column_begin, column_end);
        else if (sigmoid_type_ == sigmoid_type::algebraic)
            algebraic_(column_begin, column_end);
        else if (sigmoid_type_ == sigmoid_type::hyperbolic_tan)
            hyperbolic_tan_(column_begin, column_end);
        else if (sigmoid_type_ == sigmoid_type::arc_tan)
            arc_tan_(column_begin, column_end);
        else if (sigmoid_type_ == sigmoid_type::error_function)
            error_function_(column_begin, column_end);
        else if (sigmoid_type_ == sigmoid_type::gudermannian)
            gudermannian_(column_begin, column_end);
        else if (sigmoid_type_ == sigmoid_type::smoothstep)
            smoothstep_(column_begin, column_end);
    }

    DEFINE_PRE_POST
    DEFINE_RESULT

    explicit
    SigmoidVisitor(sigmoid_type st) : sigmoid_type_(st)  {   }

private:

    result_type         result_ {  }; // Sigmoids
    const sigmoid_type  sigmoid_type_;
};

template<typename T, typename I = unsigned long, std::size_t A = 0>
using sigm_v = SigmoidVisitor<T, I, A>;

// ----------------------------------------------------------------------------

template<typename T, typename I = unsigned long, std::size_t A = 0,
         typename =
             typename std::enable_if<supports_arithmetic<T>::value, T>::type>
struct RectifyVisitor {

    DEFINE_VISIT_BASIC_TYPES_3

public:

    template <typename K, typename H>
    inline void
    operator() (const K &idx_begin, const K &idx_end,
                const H &column_begin, const H &column_end)  {

        GET_COL_SIZE

        result_.reserve(col_s);
        if (rtype_ == rectify_type::ReLU)  {
            std::for_each(column_begin, column_end,
                          [this](const value_type &v) -> void  {
                              this->result_.push_back(std::max(T(0), v));
                          });
        }
        else if (rtype_ == rectify_type::param_ReLU)  {
            std::for_each(column_begin, column_end,
                          [this](const value_type &v) -> void  {
                              this->result_.push_back(
                                  std::max(v * this->param_, v));
                          });
        }
        else if (rtype_ == rectify_type::GeLU)  {
            std::for_each(column_begin, column_end,
                          [this](const value_type &v) -> void  {
                              this->result_.push_back(
                                  v * this->standard_normal_dist_(v));
                          });
        }
        else if (rtype_ == rectify_type::SiLU)  {
            sigm_v<T, I, A> sigm(sigmoid_type::logistic);

            sigm.pre();
            sigm(idx_begin, idx_end, column_begin, column_end);
            sigm.post();

            for (size_type i = 0; i < col_s; ++i)
                result_.push_back(*(column_begin + i) * sigm.get_result()[i]);
        }
        else if (rtype_ == rectify_type::softplus)  {
            std::for_each(column_begin, column_end,
                          [this](const value_type &v) -> void  {
                              this->result_.push_back(softp_(v, this->param_));
                          });
        }
        else if (rtype_ == rectify_type::elu)  {
            std::for_each(column_begin, column_end,
                          [this](const value_type &v) -> void   {
                              if (v > 0)
                                  this->result_.push_back(v);
                              else
                                  this->result_.push_back(
                                      this->param_ * (std::exp(v) - T(1)));
                          });
        }
        else if (rtype_ == rectify_type::mish)  {
            std::for_each(column_begin, column_end,
                          [this](const value_type &v) -> void  {
                              this->result_.push_back(
                                  v * std::tanh(softp_(v, this->param_)));
                          });
        }
        else if (rtype_ == rectify_type::metallic_mean)  {
            std::for_each(column_begin, column_end,
                          [this](const value_type &v) -> void  {
                              this->result_.push_back(
                                  (v + std::sqrt(v * v + T(4))) / T(2));
                          });
        }
    }

    DEFINE_PRE_POST
    DEFINE_RESULT

    explicit
    RectifyVisitor(rectify_type r_type, value_type param = 1)
        : param_(param), rtype_(r_type)  {   }

private:

    inline static value_type
    softp_(const value_type &v, const value_type &p)  {

        return(std::log(T(1) + std::exp(p * v)) / p);
    }
    inline static value_type
    standard_normal_dist_(const value_type &v)  {

        static constexpr value_type two = 2;
        static const     value_type sqrt_dbl_pi = std::sqrt(two * M_PI);

        return (std::exp(-(v * v) / two) / sqrt_dbl_pi);
    }

    result_type         result_ {  };
    const value_type    param_;
    const rectify_type  rtype_;
};

template<typename T, typename I = unsigned long, std::size_t A = 0>
using recf_v = RectifyVisitor<T, I, A>;

// ----------------------------------------------------------------------------

template<typename T, typename I = unsigned long, std::size_t A = 0,
         typename =
             typename std::enable_if<supports_arithmetic<T>::value, T>::type>
struct  PolicyLearningLossVisitor  {

    DEFINE_VISIT_BASIC_TYPES_3

    template <typename K, typename H>
    inline void
    operator() (const K & /*idx_begin*/, const K & /*idx_end*/,
                const H &action_prob_begin, const H &action_prob_end,
                const H &reward_begin, const H &reward_end)  {

        const size_type col_s =
            std::distance(action_prob_begin, action_prob_end);

        assert((col_s == size_type(std::distance(reward_begin, reward_end))));

        // Negative Log Likelihood
        //
        result_.reserve(col_s);
        std::transform(action_prob_begin, action_prob_end,
                       reward_begin,
                       std::back_inserter(result_),
                       [](const T &ap, const T &r) -> T  {
                           return (-std::log(ap) * r);
                       });
    }

    DEFINE_PRE_POST
    DEFINE_RESULT

    PolicyLearningLossVisitor() = default;

private:

    result_type result_ {  };
};

template<typename T, typename I = unsigned long, std::size_t A = 0>
using plloss_v = PolicyLearningLossVisitor<T, I, A>;

// -----------------------------------------------------------------------------

template<typename T, typename I = unsigned long,
         typename =
             typename std::enable_if<supports_arithmetic<T>::value, T>::type>
struct LossFunctionVisitor  {

public:

    DEFINE_VISIT_BASIC_TYPES_2

    template <typename K, typename H>
    inline void
    operator() (const K &idx_begin, const K &idx_end,
                const H &actual_begin, const H &actual_end,
                const H &model_begin, const H &model_end)  {

        const size_type col_s = std::distance(actual_begin, actual_end);

        assert((col_s == size_type(std::distance(model_begin, model_end))));

        if (lft_ == loss_function_type::kullback_leibler)  {
            result_ =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          return (a * std::log(a / m));
                                      });
        }
        else if (lft_ == loss_function_type::mean_abs_error)  {
            result_ =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          return (std::fabs(a - m));
                                      });
            result_ /= col_s;
        }
        else if (lft_ == loss_function_type::mean_sqr_error)  {
            result_ =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          const T   val = a - m;

                                          return (val * val);
                                      });
            result_ /= col_s;
        }
        else if (lft_ == loss_function_type::mean_sqr_log_error)  {
            result_ =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          const T   val = std::log(T(1) + a) -
                                                          std::log(T(1) + m);

                                          return (val * val);
                                      });
            result_ /= col_s;
        }
        else if (lft_ == loss_function_type::cross_entropy)  {
            result_ =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          return (a * std::log(m));
                                      });
            result_ = -(result_ / col_s);
        }
        else if (lft_ == loss_function_type::binary_cross_entropy)  {
            result_ =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          return (-(a * std::log(m)) +
                                                  (1 - a) * std::log(1 - m));
                                      });
            result_ /= col_s;
        }
        else if (lft_ == loss_function_type::categorical_hinge)  {
            const result_type   neg =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          return ((T(1) - a) * m);
                                      });
            const result_type   pos =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          return (a * m);
                                      });

            result_ = std::max(neg - pos + T(1), T(0));;
        }
        else if (lft_ == loss_function_type::cosine_similarity)  {
            DotProdVisitor<T, I>    dot_v;

            dot_v.pre();
            dot_v (idx_begin, idx_end,
                   actual_begin, actual_end, model_begin, model_end);
            dot_v.post();

            const result_type   dot_prod = dot_v.get_result();

            dot_v.pre();
            dot_v (idx_begin, idx_end,
                   actual_begin, actual_end, actual_begin, actual_end);
            dot_v.post();

            const result_type   a_mag = std::sqrt(dot_v.get_result());

            dot_v.pre();
            dot_v (idx_begin, idx_end,
                   model_begin, model_end, model_begin, model_end);
            dot_v.post();

            const result_type   m_mag = std::sqrt(dot_v.get_result());

            result_ = dot_prod / (a_mag * m_mag);
        }
        else if (lft_ == loss_function_type::log_cosh)  {
            result_ =
                std::transform_reduce(actual_begin, actual_end,
                                      model_begin, T(0), std::plus { },
                                      [](const T &a, const T &m) -> T  {
                                          return (std::log(std::cosh(m - a)));
                                      });
            result_ /= col_s;
        }
    }

    inline void pre ()  { result_ = 0; }
    inline void post ()  {  }

    inline result_type get_result() const  { return (result_); }

    explicit
    LossFunctionVisitor(loss_function_type lft) : lft_(lft)  {   }

private:

    result_type                 result_ { 0 };
    const loss_function_type    lft_;
};

template<typename T, typename I = unsigned long>
using loss_v = LossFunctionVisitor<T, I>;

} // namespace hmdf

// -----------------------------------------------------------------------------

// Local Variables:
// mode:C++
// tab-width:4
// c-basic-offset:4
// End:
