///////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012  Whit Armstrong                                    //
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

#include <sstream>
#include <stdexcept>
#include <Rinternals.h>
#include "distribution.types.h"
#include "helpers.h"

std::string getAttr(SEXP x, const char* attr_name) {
  std::string ans;
  SEXP attr = getAttrib(x,install(attr_name));
  if(attr != R_NilValue) {
    ans = std::string(CHAR(STRING_ELT(attr,0)));
  }
  return ans;
}

// bool isMatrix(SEXP x) {
//   return getAttrib(x,R_DimSymbol) != R_NilValue ? true : false;
// }

std::vector<R_len_t> getDims(SEXP x) {
  std::vector<R_len_t> ans;
  SEXP dims = getAttrib(x, R_DimSymbol);
  if(dims == R_NilValue) {
    // only add a dimension if len > 1
    // scalars will have null dims (vec.size() == 0)
    if(LENGTH(x) > 1) {
      ans.push_back(LENGTH(x));
    }
  } else {
    for(R_len_t i = 0; i < LENGTH(dims); i++) {
      ans.push_back(INTEGER(dims)[i]);
    }
  }
  //Rprintf("dim size: %d\n",ans.size());
  return ans;
}

distMapT initDistributions() {
  distMapT ans;

  // deterministic types
  ans["deterministic"] = deterministicT;
  ans["linear.deterministic"] = linearDeterministicT;
  ans["logistic.deterministic"] = logisticDeterministicT;

  // continuous types
  ans["normal"] = normalDistT;
  ans["uniform"] = uniformDistT;
  ans["gamma"] = gammaDistT;
  ans["beta"] = betaDistT;

  // discrete types
  ans["bernoulli"] = bernoulliDistT;
  ans["binomial"] = binomialDistT;
  return ans;
}

distT matchDistibution(const std::string distibution) {
  static distMapT distMap = initDistributions();
  std::stringstream error_ss;
  if( distMap.count(distibution) == 0) {
    error_ss << "unknown distribution:" + distibution;
    throw std::logic_error(error_ss.str());
  }
  return distMap[distibution];
}
