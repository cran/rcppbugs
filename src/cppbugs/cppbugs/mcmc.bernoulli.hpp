///////////////////////////////////////////////////////////////////////////
// Copyright (C) 2011 Whit Armstrong                                     //
//                                                                       //
// This program is free software: you can redistribute it and/or modify  //
// it under the terms of the GNU General Public License as published by  //
// the Free Software Foundation, either version 3 of the License, or     //
// (at your option) any later version.                                   //
//                                                                       //
// This program is distributed in the hope that it will be useful,       //
// but WITHOUT ANY WARRANTY; without even the implied warranty of        //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         //
// GNU General Public License for more details.                          //
//                                                                       //
// You should have received a copy of the GNU General Public License     //
// along with this program.  If not, see <http://www.gnu.org/licenses/>. //
///////////////////////////////////////////////////////////////////////////

#ifndef MCMC_BERNOULLI_HPP
#define MCMC_BERNOULLI_HPP


#include <cmath>
#include <armadillo>
#include <cppbugs/mcmc.stochastic.hpp>

namespace cppbugs {

  template <typename T,typename U>
  class BernoulliLikelihiood : public Likelihiood {
    const T& x_;
    const U& p_;
  public:
    BernoulliLikelihiood(const T& x, const U& p): x_(x), p_(p) {}
    inline double calc() const {
      return bernoulli_logp(x_,p_);
    }
  };

  template<typename T>
  class Bernoulli : public DynamicStochastic<T> {

    template<typename U>
    void bernoulli_jump(RngBase& rng, U& value, const double scale) {
      double jump_probability = 1.0 - pow(0.5,scale);
      arma::uvec flips = arma::find(arma::randu<arma::vec>(value.n_elem) < jump_probability);
      for(unsigned int i = 0; i < flips.n_elem; i++) {
        value[ flips[i] ] = value[ flips[i] ] ? 0 : 1;
      }
    }

    void bernoulli_jump(RngBase& rng, int& value, const double scale) {
      double jump_probability = 1.0 - pow(0.5,scale);
      if(rng.uniform() < jump_probability) {
        value = value ? 0 : 1;
      }
    }

  public:
    Bernoulli(T& value): DynamicStochastic<T>(value) {}

    /* original
    void jump(RngBase& rng) {
      double jump_probability = 1.0 - pow(0.5,DynamicStochastic<T>::scale_);
      arma::uvec flips = arma::find(arma::randu<arma::vec>(DynamicStochastic<T>::value.n_elem) < jump_probability);
      for(unsigned int i = 0; i < flips.n_elem; i++) {
        DynamicStochastic<T>::value[ flips[i] ] = DynamicStochastic<T>::value[ flips[i] ] ? 0 : 1;
      }
    }
    */

    void jump(RngBase& rng) {
      bernoulli_jump(rng, DynamicStochastic<T>::value, DynamicStochastic<T>::scale_);
    }

    template<typename U>
    Bernoulli<T>& dbern(const U& p) {
      Stochastic::likelihood_functor = new BernoulliLikelihiood<T,U>(DynamicStochastic<T>::value,p);
      return *this;
    }
  };

  template<typename T>
  class ObservedBernoulli : public Observed<T> {
  public:
    ObservedBernoulli(const T& value): Observed<T>(value) {}

    template<typename U>
    ObservedBernoulli<T>& dbern(const U& p) {
      Stochastic::likelihood_functor = new BernoulliLikelihiood<T,U>(Observed<T>::value,p);
      return *this;
    }
  };

  // template<>
  // class Bernoulli <arma::ivec> : public Stochastic<arma::ivec> {
  // public:
  //   Bernoulli(const arma::ivec& value, const bool observed=false): Stochastic<arma::ivec>(value,observed) {}

  //   void jump(RngBase& rng) {
  //     double jump_probability = 1.0 - pow(0.5,Stochastic<arma::ivec>::scale_);
  //     arma::uvec flips = find(arma::randu<arma::vec>(Stochastic<arma::ivec>::value.n_elem) < jump_probability);
  //     for(unsigned int i = 0; i < flips.n_elem; i++) {
  //       Stochastic<arma::ivec>::value[ flips[i] ] = Stochastic<arma::ivec>::value[ flips[i] ] ? 0 : 1;
  //     }
  //   }

  //   void dbern(const arma::vec& p) {
  //     arma::uvec p_less_than_eq_zero = find(p <= 0,1);
  //     arma::uvec p_greater_than_eq_1 = find(p >= 1,1);
  //     arma::uvec value_less_than_zero = find(Stochastic<arma::ivec>::value < 0,1);
  //     arma::uvec value_greater_than_n = find(Stochastic<arma::ivec>::value > 1,1);

  //     if(p_less_than_eq_zero.n_elem || p_greater_than_eq_1.n_elem || value_less_than_zero.n_elem || value_greater_than_n.n_elem) {
  //       Stochastic<arma::ivec>::logp_ = -std::numeric_limits<double>::infinity();
  //     } else {
  //       Stochastic<arma::ivec>::logp_ = accu(Stochastic<arma::ivec>::value % log(p) + (1-Stochastic<arma::ivec>::value) % log(1-p));
  //     }
  //   }
  // };
} // namespace cppbugs
#endif // MCMC_BERNOULLI_HPP