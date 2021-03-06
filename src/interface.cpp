///////////////////////////////////////////////////////////////////////////
// Copyright (C) 2011  Whit Armstrong                                    //
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

#include <map>
#include <limits>
#include <stdexcept>
#include <RcppArmadillo.h>
#define NDEBUG
#include <cppbugs/mcmc.deterministic.hpp>
#include <cppbugs/distributions/mcmc.normal.hpp>
#include <cppbugs/distributions/mcmc.uniform.hpp>
#include <cppbugs/distributions/mcmc.gamma.hpp>
#include <cppbugs/distributions/mcmc.beta.hpp>
#include <cppbugs/distributions/mcmc.binomial.hpp>
#include <cppbugs/distributions/mcmc.bernoulli.hpp>

#include "helpers.h"
#include "raw.address.h"
#include "distribution.types.h"
#include "arma.context.h"
#include "assign.normal.logp.h"
#include "assign.uniform.logp.h"
#include "assign.gamma.logp.h"
#include "assign.beta.logp.h"
#include "assign.bernoulli.logp.h"
#include "assign.binomial.logp.h"
#include "r.deterministic.h"
#include "linear.deterministic.h"
#include "linear.grouped.deterministic.h"
#include "logistic.deterministic.h"
#include "r.mcmc.model.h"

typedef std::map<void*,ArmaContext*> vpArmaMapT;
typedef std::map<void*,cppbugs::MCMCObject*> vpMCMCMapT;

// public interface
extern "C" SEXP logp(SEXP x_,SEXP rho_);
extern "C" SEXP createModel(SEXP args_sexp);
extern "C" SEXP runModel(SEXP mp_, SEXP iterations, SEXP burn_in, SEXP adapt, SEXP thin);

// private methods
cppbugs::MCMCObject* createMCMC(SEXP x, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createDeterministic(SEXP args_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createLinearDeterministic(SEXP x_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createLinearGroupedDeterministic(SEXP x_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createLogisticDeterministic(SEXP x_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createNormal(SEXP x_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createUniform(SEXP x_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createGamma(SEXP x_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createBeta(SEXP x_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createBernoulli(SEXP x_, vpArmaMapT& armaMap);
cppbugs::MCMCObject* createBinomial(SEXP x_, vpArmaMapT& armaMap);

ArmaContext* getArma(SEXP x);
ArmaContext* mapOrFetch(SEXP x_, vpArmaMapT& armaMap);
void initArgList(SEXP args, arglistT& arglist, const size_t skip);
SEXP makeNames(std::vector<const char*>& argnames);
SEXP createTrace(arglistT& arglist, vpArmaMapT& armaMap, vpMCMCMapT& mcmcMap);

template<typename T>
void releaseMap(T& m) {
  for (typename T::iterator it=m.begin(); it != m.end(); it++) {
    delete it->second;
  }
}

void initArgList(SEXP args, arglistT& arglist, const size_t skip) {

  for(size_t i = 0; i < skip; i++) {
    args = CDR(args);
  }

  // loop through rest of args
  for(; args != R_NilValue; args = CDR(args)) {
    arglist.push_back(CAR(args));
  }
}

ArmaContext* mapOrFetch(SEXP x_, vpArmaMapT& armaMap) {
  ArmaContext* x_arma(NULL);
  void* vp = rawAddress(x_);

  if(armaMap.count(vp)==0) {
    // protect object if adding to armaMap
    PROTECT(x_);
    x_arma = getArma(x_);
    armaMap[vp] = x_arma;
  } else {
    x_arma = armaMap[vp];
  }
  return x_arma;
}

SEXP logp(SEXP x_, SEXP rho_) {
  const int eval_limit = 10;
  double ans = std::numeric_limits<double>::quiet_NaN();
  cppbugs::MCMCObject* node(NULL);
  vpArmaMapT armaMap;

  if(rho_ == R_NilValue || TYPEOF(rho_) != ENVSXP) {
    REprintf("ERROR: bad environment passed to logp (contact the package maintainer).");
  }

  try {
    x_ = forceEval(x_, rho_, eval_limit);
    ArmaContext* ap = mapOrFetch(x_, armaMap);
    node = createMCMC(x_,armaMap);
  } catch (std::logic_error &e) {
    releaseMap(armaMap); UNPROTECT(armaMap.size());
    REprintf("%s\n",e.what());
    return R_NilValue;
  }

  cppbugs::Stochastic* sp = dynamic_cast<cppbugs::Stochastic*>(node);
  if(sp) {
    ans = sp->loglik();
  } else {
    REprintf("ERROR: could not convert node to stochastic.\n");
  }
  releaseMap(armaMap); UNPROTECT(armaMap.size());
  return Rcpp::wrap(ans);
}

template<typename T>
SEXP getHistory(cppbugs::MCMCObject* node) {
  //SEXP ans;
  cppbugs::MCMCSpecialized<T>* sp = dynamic_cast<cppbugs::MCMCSpecialized<T>*>(node);
  if(sp == NULL) {
    throw std::logic_error("invalid node conversion.");
  }
  //Rprintf("getHistory<T> history.size(): %d\n",sp->history.size());
  //PROTECT(ans = Rf_allocVector(VECSXP, sp->history.size()));
  //Rcpp::List ans(sp->history.size());
  //const size_t NC = sp->history.begin()->n_elem;
  const size_t NC = sp->history.begin()->n_cols;
  Rcpp::NumericMatrix ans(sp->history.size(),NC);
  R_len_t i = 0;
  for(typename std::list<T>::const_iterator it = sp->history.begin(); it != sp->history.end(); it++) {
    //SET_VECTOR_ELT(ans, i, Rcpp::wrap(*it)); ++i;
    //ans[i] = Rcpp::wrap(*it);
    //Rprintf("%d %d",i, it->n_cols);
    for(size_t j = 0; j < NC; j++) {
      ans(i,j) = it->at(j);
    }
    ++i;
  }
  //UNPROTECT(1);
  return Rcpp::wrap(ans);
}

template<> SEXP getHistory<arma::vec>(cppbugs::MCMCObject* node) {
  //SEXP ans;
  cppbugs::MCMCSpecialized<arma::vec>* sp = dynamic_cast<cppbugs::MCMCSpecialized<arma::vec>*>(node);
  if(sp == NULL) {
    throw std::logic_error("invalid node conversion.");
  }

  if(sp->history.size()==0) {
    return R_NilValue;
  }

  //Rprintf("getHistory<arma::vec> history.size(): %d\n",sp->history.size());
  const size_t NC = sp->history.begin()->n_elem;
  //Rprintf("getHistory<arma::vec> history dim: %d\n",NC);
  Rcpp::NumericMatrix ans(sp->history.size(),NC);
  R_len_t i = 0;
  for(typename std::list<arma::vec>::const_iterator it = sp->history.begin(); it != sp->history.end(); it++) {
    for(size_t j = 0; j < NC; j++) {
      ans(i,j) = it->at(j);
    }
    ++i;
  }
  //UNPROTECT(1);
  return Rcpp::wrap(ans);
}

template<> SEXP getHistory<double>(cppbugs::MCMCObject* node) {
  cppbugs::MCMCSpecialized<double>* sp = dynamic_cast<cppbugs::MCMCSpecialized<double>*>(node);
  if(sp == NULL) {
    throw std::logic_error("invalid node conversion.");
  }
  //Rprintf("getHistory<double> history.size(): %d\n",sp->history.size());
  Rcpp::NumericVector ans(sp->history.size());
  R_len_t i = 0;
  for(typename std::list<double>::const_iterator it = sp->history.begin(); it != sp->history.end(); it++) {
    ans[i] =*it; ++i;
  }
  return Rcpp::wrap(ans);
}

SEXP makeNames(std::vector<const char*>& argnames) {
  SEXP ans;
  PROTECT(ans = Rf_allocVector(STRSXP, argnames.size()));
  for(size_t i = 0; i < argnames.size(); i++) {
    SET_STRING_ELT(ans, i, Rf_mkChar(argnames[i]));
  }
  UNPROTECT(1);
  return ans;
}

SEXP createTrace(arglistT& arglist, vpArmaMapT& armaMap, vpMCMCMapT& mcmcMap) {
  SEXP ans; PROTECT(ans = Rf_allocVector(VECSXP, arglist.size()));
  for(size_t i = 0; i < arglist.size(); i++) {
    ArmaContext* ap = armaMap[rawAddress(arglist[i])];
    cppbugs::MCMCObject* node = mcmcMap[rawAddress(arglist[i])];
    if(!node->isObserved()) {
      switch(ap->getArmaType()) {
      case doubleT:
        SET_VECTOR_ELT(ans,i,getHistory<double>(node));
        break;
      case vecT:
        SET_VECTOR_ELT(ans,i,getHistory<arma::vec>(node));
        break;
      case matT:
      default:
        SET_VECTOR_ELT(ans,i,R_NilValue);
      }
    } else {
      SET_VECTOR_ELT(ans,i,R_NilValue);
    }
  }
  UNPROTECT(1);
  return ans;
}

SEXP runModel(SEXP m_, SEXP iterations, SEXP burn_in, SEXP adapt, SEXP thin) {
  const int eval_limit = 10;

  SEXP env_ = Rf_getAttrib(m_,Rf_install("env"));
  if(env_ == R_NilValue || TYPEOF(env_) != ENVSXP) {
    throw std::logic_error("ERROR: bad environment passed to deterministic.");
  }

  vpArmaMapT armaMap;
  vpMCMCMapT mcmcMap;
  std::vector<cppbugs::MCMCObject*> mcmcObjects;

  arglistT arglist;
  std::vector<const char*> argnames;

  initArgList(m_, arglist, 1);
  for(size_t i = 0; i < arglist.size(); i++) {

    // capture arg name
    // FIXME: check class of args to make sure it's mcmc
    if(TYPEOF(arglist[i])==SYMSXP) { argnames.push_back(CHAR(PRINTNAME(arglist[i]))); }

    // force eval of late bindings
    arglist[i] = forceEval(arglist[i],env_,eval_limit);

    try {
      ArmaContext* ap = mapOrFetch(arglist[i], armaMap);
      cppbugs::MCMCObject* node = createMCMC(arglist[i],armaMap);
      mcmcMap[rawAddress(arglist[i])] = node;
      mcmcObjects.push_back(node);
    } catch (std::logic_error &e) {
      releaseMap(armaMap); releaseMap(mcmcMap); UNPROTECT(armaMap.size());
      REprintf("%s\n",e.what());
      return R_NilValue;
    }
  }

  int iterations_ = Rcpp::as<int>(iterations);
  int burn_in_ = Rcpp::as<int>(burn_in);
  int adapt_ = Rcpp::as<int>(adapt);
  int thin_ = Rcpp::as<int>(thin);
  SEXP ar; PROTECT(ar = Rf_allocVector(REALSXP,1));
  try {
    cppbugs::RMCModel m(mcmcObjects);
    m.sample(iterations_, burn_in_, adapt_, thin_);
    //std::cout << "acceptance_ratio: " << m.acceptance_ratio() << std::endl;
    REAL(ar)[0] = m.acceptance_ratio();
  } catch (std::logic_error &e) {
    releaseMap(armaMap); releaseMap(mcmcMap); UNPROTECT(armaMap.size());
    UNPROTECT(1); // ar
    REprintf("%s\n",e.what());
    return R_NilValue;
  }

  SEXP ans;
  PROTECT(ans = createTrace(arglist,armaMap,mcmcMap));
  releaseMap(armaMap);releaseMap(mcmcMap); UNPROTECT(armaMap.size());
  Rf_setAttrib(ans, R_NamesSymbol, makeNames(argnames));
  Rf_setAttrib(ans, Rf_install("acceptance.ratio"), ar);
  UNPROTECT(2); // ans + ar
  return ans;
}

ArmaContext* getArma(SEXP x_) {
  ArmaContext* ap;
  switch(TYPEOF(x_)) {
  case REALSXP:
    switch(getDims(x_).size()) {
    case 0: ap = new ArmaDouble(x_); break;
    case 1: ap = new ArmaVec(x_); break;
    case 2: ap = new ArmaMat(x_); break;
    default:
      throw std::logic_error("ERROR: tensor conversion not supported yet.");
    }
    break;
  case LGLSXP:
  case INTSXP:
    switch(getDims(x_).size()) {
    case 0: ap = new ArmaInt(x_); break;
    case 1: ap = new ArmaiVec(x_); break;
    case 2: ap = new ArmaiMat(x_); break;
    default:
      throw std::logic_error("ERROR: tensor conversion not supported yet.");
    }
    break;
  default:
    std::stringstream error_ss;
    error_ss << "ERROR: (getArma) conversion not supported ";
    error_ss << "TYPEOF: " << TYPEOF(x_);
    // if(PRINTNAME(x_) != R_NilValue) {
    //   error_ss << "variable: " << CHAR(PRINTNAME(x_));
    // }
    throw std::logic_error(error_ss.str());
  }
  return ap;
}

cppbugs::MCMCObject* createMCMC(SEXP x_, vpArmaMapT& armaMap) {
  SEXP distributed_sexp;
  distributed_sexp = Rf_getAttrib(x_,Rf_install("distributed"));
  SEXP class_sexp = Rf_getAttrib(x_,R_ClassSymbol);
  if(class_sexp == R_NilValue || TYPEOF(class_sexp) != STRSXP || CHAR(STRING_ELT(class_sexp,0))==NULL || strcmp(CHAR(STRING_ELT(class_sexp,0)),"mcmc.object"))  {
    throw std::logic_error("ERROR: class attribute not defined or not equal to 'mcmc.object'.");
  }

  if(distributed_sexp == R_NilValue) {
    throw std::logic_error("ERROR: 'distributed' attribute not defined. Is this an mcmc.object?");
  }

  if(armaMap.count(rawAddress(x_))==0) {
    throw std::logic_error("ArmaContext not found (object should be mapped before call to createMCMC).");
  }

  distT distributed = matchDistibution(std::string(CHAR(STRING_ELT(distributed_sexp,0))));
  cppbugs::MCMCObject* ans;

  switch(distributed) {
    // deterministic types
  case deterministicT:
    ans = createDeterministic(x_,armaMap);
    break;
  case linearDeterministicT:
    ans = createLinearDeterministic(x_,armaMap);
    break;
  case linearGroupedDeterministicT:
    ans = createLinearGroupedDeterministic(x_,armaMap);
    break;
  case logisticDeterministicT:
    ans = createLogisticDeterministic(x_,armaMap);
    break;
    // continuous types
  case normalDistT:
    ans = createNormal(x_,armaMap);
    break;
  case uniformDistT:
    ans = createUniform(x_,armaMap);
    break;
  case gammaDistT:
    ans = createGamma(x_,armaMap);
    break;
  case betaDistT:
    ans = createBeta(x_,armaMap);
    break;
    // discrete types
  case bernoulliDistT:
    ans = createBernoulli(x_,armaMap);
    break;
  case binomialDistT:
    ans = createBinomial(x_,armaMap);
    break;
  default:
    // not implemented
    ans = NULL;
    throw std::logic_error("ERROR: distribution not supported yet.");
  }
  return ans;
}

cppbugs::MCMCObject* createDeterministic(SEXP x_, vpArmaMapT& armaMap) {
  SEXP args_;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  // function should be in position 1 (excluding fun/call name)
  SEXP fun_ = Rf_getAttrib(x_,Rf_install("update.method"));
  if(fun_ == R_NilValue || (TYPEOF(fun_) != CLOSXP && TYPEOF(fun_) != BCODESXP)) {
    throw std::logic_error("ERROR: update method must be a function.");
  }

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  if(env_ == R_NilValue || TYPEOF(env_) != ENVSXP) {
    throw std::logic_error("ERROR: bad environment passed to deterministic.");
  }
  SEXP call_ = Rf_getAttrib(x_,Rf_install("call"));
  if(TYPEOF(call_) != LANGSXP) {
    throw std::logic_error("ERROR: function arguments not LANGSXP.");
  }
  if(Rf_length(call_) <= 2) {
    throw std::logic_error("ERROR: function must have at least one argument.");
  }

  // advance by 2
  args_ = CDR(call_);
  args_ = CDR(args_);

  // map to arma types
  try {
    switch(x_arma->getArmaType()) {
    case doubleT:
      p = new cppbugs::RDeterministic<double>(x_arma->getDouble(),fun_,args_,env_);
      break;
    case vecT:
      p = new cppbugs::RDeterministic<arma::vec>(x_arma->getVec(),fun_,args_,env_);
      break;
    case matT:
      p = new cppbugs::RDeterministic<arma::mat>(x_arma->getMat(),fun_,args_,env_);
      break;
    case intT:
    case ivecT:
    case imatT:
    default:
      throw std::logic_error("ERROR: deterministic must be a continuous variable type (double, vec, or mat) for now (under development).");
    }
  } catch(std::logic_error &e) {
    REprintf("%s\n",e.what());
    return NULL;
  }
  return p;
}

cppbugs::MCMCObject* createLinearDeterministic(SEXP x_, vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP X_ = Rf_getAttrib(x_,Rf_install("X"));
  SEXP b_ = Rf_getAttrib(x_,Rf_install("b"));

  if(x_ == R_NilValue || env_ == R_NilValue || X_ == R_NilValue || b_ == R_NilValue) {
    throw std::logic_error("ERROR: createLinearDeterministic, missing or null argument.");
  }

  // force substitutions
  X_ = forceEval(X_, env_, eval_limit);
  b_ = forceEval(b_, env_, eval_limit);

  // map to arma types
  ArmaContext* X_arma = mapOrFetch(X_, armaMap);
  ArmaContext* b_arma = mapOrFetch(b_, armaMap);

  // little x
  if(x_arma->getArmaType() != matT) {
    throw std::logic_error("ERROR: createLinearDeterministic, x must be a real valued matrix.");
  }

  // big X
  if(X_arma->getArmaType() != matT && X_arma->getArmaType() != imatT) {
    throw std::logic_error("ERROR: createLinearDeterministic, X must be a matrix.");
  }

  // b -- coefs vector
  if(b_arma->getArmaType() != vecT) {
    throw std::logic_error("ERROR: createLinearDeterministic, b must be a real valued vector.");
  }

  switch(X_arma->getArmaType()) {
  case matT:
    p = new cppbugs::LinearDeterministic<arma::mat>(x_arma->getMat(),X_arma->getMat(),b_arma->getVec());
    break;
  case imatT:
    p = new cppbugs::LinearDeterministic<arma::imat>(x_arma->getMat(),X_arma->getiMat(),b_arma->getVec());
    break;
  default:
    throw std::logic_error("ERROR: createLogisticDeterministic, combination of arguments not supported.");
  }
  return p;
}

cppbugs::MCMCObject* createLinearGroupedDeterministic(SEXP x_, vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP X_ = Rf_getAttrib(x_,Rf_install("X"));
  SEXP b_ = Rf_getAttrib(x_,Rf_install("b"));
  SEXP group_ = Rf_getAttrib(x_,Rf_install("group"));

  if(x_ == R_NilValue || env_ == R_NilValue || X_ == R_NilValue || b_ == R_NilValue || group_ == R_NilValue) {
    throw std::logic_error("ERROR: createLinearDeterministic, missing or null argument.");
  }

  // force substitutions
  X_ = forceEval(X_, env_, eval_limit);
  b_ = forceEval(b_, env_, eval_limit);
  group_ = forceEval(group_, env_, eval_limit);

  // map to arma types
  ArmaContext* X_arma = mapOrFetch(X_, armaMap);
  ArmaContext* b_arma = mapOrFetch(b_, armaMap);
  ArmaContext* group_arma = mapOrFetch(group_, armaMap);

  // little x
  if(x_arma->getArmaType() != matT) {
    throw std::logic_error("ERROR: createLinearGroupedDeterministic, x must be a real valued matrix.");
  }

  // big X
  if(X_arma->getArmaType() != matT) {
    throw std::logic_error("ERROR: createLinearGroupedDeterministic, X must be a matrix.");
  }

  // b -- coefs vector
  if(b_arma->getArmaType() != matT) {
    throw std::logic_error("ERROR: createLinearGroupedDeterministic, b must be a real valued matrix.");
  }

  // group -- multilevel group
  if(group_arma->getArmaType() != ivecT) {
    throw std::logic_error("ERROR: createLinearGroupedDeterministic, group must be an integer vector.");
  }

  switch(X_arma->getArmaType()) {
  case matT:
    p = new cppbugs::LinearGroupedDeterministic<arma::mat>(x_arma->getMat(),X_arma->getMat(),b_arma->getMat(),group_arma->getiVec());
    break;
  case imatT:
    p = new cppbugs::LinearGroupedDeterministic<arma::imat>(x_arma->getMat(),X_arma->getiMat(),b_arma->getMat(),group_arma->getiVec());
    break;
  default:
    throw std::logic_error("ERROR: createLinearGroupedDeterministic, combination of arguments not supported.");
  }
  return p;
}

cppbugs::MCMCObject* createLogisticDeterministic(SEXP x_, vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP X_ = Rf_getAttrib(x_,Rf_install("X"));
  SEXP b_ = Rf_getAttrib(x_,Rf_install("b"));

  if(x_ == R_NilValue || env_ == R_NilValue || X_ == R_NilValue || b_ == R_NilValue) {
    throw std::logic_error("ERROR: createLogisticDeterministic, missing or null argument.");
  }

  // force substitutions
  X_ = forceEval(X_, env_, eval_limit);
  b_ = forceEval(b_, env_, eval_limit);

  // map to arma types
  ArmaContext* X_arma = mapOrFetch(X_, armaMap);
  ArmaContext* b_arma = mapOrFetch(b_, armaMap);

  // little x
  if(x_arma->getArmaType() != matT) {
    throw std::logic_error("ERROR: createLogisticDeterministic, x must be a real valued matrix.");
  }

  // big X
  if(X_arma->getArmaType() != matT && X_arma->getArmaType() != imatT) {
    throw std::logic_error("ERROR: createLogisticDeterministic, X must be a matrix.");
  }

  // b -- coefs vector
  if(b_arma->getArmaType() != vecT) {
    throw std::logic_error("ERROR: createLogisticDeterministic, b must be a real valued vector.");
  }

  switch(X_arma->getArmaType()) {
  case matT:
    p = new cppbugs::LogisticDeterministic<arma::mat>(x_arma->getMat(),X_arma->getMat(),b_arma->getVec());
    break;
  case imatT:
    p = new cppbugs::LogisticDeterministic<arma::imat>(x_arma->getMat(),X_arma->getiMat(),b_arma->getVec());
    break;
  default:
    throw std::logic_error("ERROR: createLogisticDeterministic, combination of arguments not supported.");
  }
  return p;
}

cppbugs::MCMCObject* createNormal(SEXP x_,vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP mu_ = Rf_getAttrib(x_,Rf_install("mu"));
  SEXP tau_ = Rf_getAttrib(x_,Rf_install("tau"));
  SEXP observed_ = Rf_getAttrib(x_,Rf_install("observed"));

  //Rprintf("typeof mu: %d\n",TYPEOF(mu_));

  if(x_ == R_NilValue || env_ == R_NilValue || mu_ == R_NilValue || tau_ == R_NilValue || observed_ == R_NilValue) {
    throw std::logic_error("ERROR: createNormal, missing or null argument.");
  }

  // force substitutions
  mu_ = forceEval(mu_, env_, eval_limit);
  tau_ = forceEval(tau_, env_, eval_limit);

  bool observed = Rcpp::as<bool>(observed_);

  // map to arma types
  ArmaContext* mu_arma = mapOrFetch(mu_, armaMap);
  ArmaContext* tau_arma = mapOrFetch(tau_, armaMap);

  switch(x_arma->getArmaType()) {
  case doubleT:
    if(observed) {
      p = assignNormalLogp<cppbugs::ObservedNormal>(x_arma->getDouble(),mu_arma,tau_arma);
    } else {
      p = assignNormalLogp<cppbugs::Normal>(x_arma->getDouble(),mu_arma,tau_arma);
    }
    break;
  case vecT:
    if(observed) {
      p = assignNormalLogp<cppbugs::ObservedNormal>(x_arma->getVec(),mu_arma,tau_arma);
    } else {
      p = assignNormalLogp<cppbugs::Normal>(x_arma->getVec(),mu_arma,tau_arma);
    }
    break;
  case matT:
    if(observed) {
      p = assignNormalLogp<cppbugs::ObservedNormal>(x_arma->getMat(),mu_arma,tau_arma);
    } else {
      p = assignNormalLogp<cppbugs::Normal>(x_arma->getMat(),mu_arma,tau_arma);
    }
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: normal must be a continuous variable type (double, vec, or mat).");
  }
  return p;
}

cppbugs::MCMCObject* createUniform(SEXP x_,vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP lower_ = Rf_getAttrib(x_,Rf_install("lower"));
  SEXP upper_ = Rf_getAttrib(x_,Rf_install("upper"));
  SEXP observed_ = Rf_getAttrib(x_,Rf_install("observed"));

  if(x_ == R_NilValue || env_ == R_NilValue || lower_ == R_NilValue || upper_ == R_NilValue || observed_ == R_NilValue) {
    REprintf("ERROR: missing argument.");
    return NULL;
  }

  // force substitutions
  lower_ = forceEval(lower_, env_, eval_limit);
  upper_ = forceEval(upper_, env_, eval_limit);

  bool observed = Rcpp::as<bool>(observed_);

  // map to arma types
  ArmaContext* lower_arma = mapOrFetch(lower_, armaMap);
  ArmaContext* upper_arma = mapOrFetch(upper_, armaMap);

  switch(x_arma->getArmaType()) {
  case doubleT:
    if(observed) {
      p = assignUniformLogp<cppbugs::ObservedUniform>(x_arma->getDouble(),lower_arma,upper_arma);
    } else {
      p = assignUniformLogp<cppbugs::Uniform>(x_arma->getDouble(),lower_arma,upper_arma);
    }
    break;
  case vecT:
    if(observed) {
      p = assignUniformLogp<cppbugs::ObservedUniform>(x_arma->getVec(),lower_arma,upper_arma);
    } else {
      p = assignUniformLogp<cppbugs::Uniform>(x_arma->getVec(),lower_arma,upper_arma);
    }
    break;
  case matT:
    if(observed) {
      p = assignUniformLogp<cppbugs::ObservedUniform>(x_arma->getMat(),lower_arma,upper_arma);
    } else {
      p = assignUniformLogp<cppbugs::Uniform>(x_arma->getMat(),lower_arma,upper_arma);
    }
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: uniform must be a continuous variable type (double, vec, or mat).");
  }
  return p;
}

cppbugs::MCMCObject* createGamma(SEXP x_, vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP alpha_ = Rf_getAttrib(x_,Rf_install("alpha"));
  SEXP beta_ = Rf_getAttrib(x_,Rf_install("beta"));
  SEXP observed_ = Rf_getAttrib(x_,Rf_install("observed"));

  if(x_ == R_NilValue || env_ == R_NilValue || alpha_ == R_NilValue || beta_ == R_NilValue || observed_ == R_NilValue) {
    REprintf("ERROR: missing argument.");
    return NULL;
  }

  // force substitutions
  alpha_ = forceEval(alpha_, env_, eval_limit);
  beta_ = forceEval(beta_, env_, eval_limit);

  bool observed = Rcpp::as<bool>(observed_);

  // map to arma types
  ArmaContext* alpha_arma = mapOrFetch(alpha_, armaMap);
  ArmaContext* beta_arma = mapOrFetch(beta_, armaMap);

  switch(x_arma->getArmaType()) {
  case doubleT:
    if(observed) {
      p = assignGammaLogp<cppbugs::ObservedGamma>(x_arma->getDouble(),alpha_arma,beta_arma);
    } else {
      p = assignGammaLogp<cppbugs::Gamma>(x_arma->getDouble(),alpha_arma,beta_arma);
    }
    break;
  case vecT:
    if(observed) {
      p = assignGammaLogp<cppbugs::ObservedGamma>(x_arma->getVec(),alpha_arma,beta_arma);
    } else {
      p = assignGammaLogp<cppbugs::Gamma>(x_arma->getVec(),alpha_arma,beta_arma);
    }
    break;
  case matT:
    if(observed) {
      p = assignGammaLogp<cppbugs::ObservedGamma>(x_arma->getMat(),alpha_arma,beta_arma);
    } else {
      p = assignGammaLogp<cppbugs::Gamma>(x_arma->getMat(),alpha_arma,beta_arma);
    }
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: gamma must be a continuous variable type (double, vec, or mat).");
  }
  return p;
}

cppbugs::MCMCObject* createBeta(SEXP x_, vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP alpha_ = Rf_getAttrib(x_,Rf_install("alpha"));
  SEXP beta_ = Rf_getAttrib(x_,Rf_install("beta"));
  SEXP observed_ = Rf_getAttrib(x_,Rf_install("observed"));

  if(x_ == R_NilValue || env_ == R_NilValue || alpha_ == R_NilValue || beta_ == R_NilValue || observed_ == R_NilValue) {
    REprintf("ERROR: missing argument.");
    return NULL;
  }

  // force substitutions
  alpha_ = forceEval(alpha_, env_, eval_limit);
  beta_ = forceEval(beta_, env_, eval_limit);
  bool observed = Rcpp::as<bool>(observed_);

  // map to arma types
  ArmaContext* alpha_arma = mapOrFetch(alpha_, armaMap);
  ArmaContext* beta_arma = mapOrFetch(beta_, armaMap);

  switch(x_arma->getArmaType()) {
  case doubleT:
    if(observed) {
      p = assignBetaLogp<cppbugs::ObservedBeta>(x_arma->getDouble(),alpha_arma,beta_arma);
    } else {
      p = assignBetaLogp<cppbugs::Beta>(x_arma->getDouble(),alpha_arma,beta_arma);
    }
    break;
  case vecT:
    if(observed) {
      p = assignBetaLogp<cppbugs::ObservedBeta>(x_arma->getVec(),alpha_arma,beta_arma);
    } else {
      p = assignBetaLogp<cppbugs::Beta>(x_arma->getVec(),alpha_arma,beta_arma);
    }
    break;
  case matT:
    if(observed) {
      p = assignBetaLogp<cppbugs::ObservedBeta>(x_arma->getMat(),alpha_arma,beta_arma);
    } else {
      p = assignBetaLogp<cppbugs::Beta>(x_arma->getMat(),alpha_arma,beta_arma);
    }
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: beta must be a continuous variable type (double, vec, or mat).");
  }
  return p;
}

cppbugs::MCMCObject* createBernoulli(SEXP x_, vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP p_ = Rf_getAttrib(x_,Rf_install("p"));
  SEXP observed_ = Rf_getAttrib(x_,Rf_install("observed"));

  if(x_ == R_NilValue || env_ == R_NilValue || p_ == R_NilValue || observed_ == R_NilValue) {
    REprintf("ERROR: missing argument.");
    return NULL;
  }

  // force substitutions
  p_ = forceEval(p_, env_, eval_limit);
  bool observed = Rcpp::as<bool>(observed_);

  // map to arma types
  ArmaContext* p_arma = mapOrFetch(p_, armaMap);

  if(p_arma->getArmaType() != doubleT && p_arma->getArmaType() != vecT && p_arma->getArmaType() != matT) {
    throw std::logic_error("ERROR: createBernoulli, p must be a continuous variable.");
  }

  switch(x_arma->getArmaType()) {
  case doubleT:
    if(observed) {
      p = assignBernoulliLogp<cppbugs::ObservedBernoulli>(x_arma->getDouble(),p_arma);
    } else {
      p = assignBernoulliLogp<cppbugs::Bernoulli>(x_arma->getDouble(),p_arma);
    }
    break;
  case vecT:
    if(observed) {
      p = assignBernoulliLogp<cppbugs::ObservedBernoulli>(x_arma->getVec(),p_arma);
    } else {
      p = assignBernoulliLogp<cppbugs::Bernoulli>(x_arma->getVec(),p_arma);
    }
    break;
  case matT:
    if(observed) {
      p = assignBernoulliLogp<cppbugs::ObservedBernoulli>(x_arma->getMat(),p_arma);
    } else {
      p = assignBernoulliLogp<cppbugs::Bernoulli>(x_arma->getMat(),p_arma);
    }
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: Bernoulli must be a discrete valued continuous variable type (double, vec, or mat).  This is due to an issue in armadillo.");
  }
  return p;
}

cppbugs::MCMCObject* createBinomial(SEXP x_, vpArmaMapT& armaMap) {
  const int eval_limit = 10;
  cppbugs::MCMCObject* p;
  ArmaContext* x_arma = armaMap[rawAddress(x_)];

  SEXP env_ = Rf_getAttrib(x_,Rf_install("env"));
  SEXP n_ = Rf_getAttrib(x_,Rf_install("n"));
  SEXP p_ = Rf_getAttrib(x_,Rf_install("p"));
  SEXP observed_ = Rf_getAttrib(x_,Rf_install("observed"));

  if(x_ == R_NilValue || env_ == R_NilValue || n_ == R_NilValue || p_ == R_NilValue || observed_ == R_NilValue) {
    REprintf("ERROR: missing argument.");
    return NULL;
  }

  // force substitutions
  n_ = forceEval(n_, env_, eval_limit);
  p_ = forceEval(p_, env_, eval_limit);

  bool observed = Rcpp::as<bool>(observed_);

  // map to arma types
  ArmaContext* n_arma = mapOrFetch(n_, armaMap);
  ArmaContext* p_arma = mapOrFetch(p_, armaMap);

  armaT n_arma_type = n_arma->getArmaType();
  if(n_arma_type == intT || n_arma_type == ivecT || n_arma_type == imatT) {
    throw std::logic_error("ERROR: binomial hyperparameter n must be a continuous variable type (double, vec, or mat).  This is due to an issue in armadillo.");
  }

  armaT p_arma_type = p_arma->getArmaType();
  if(p_arma_type == intT || p_arma_type == ivecT || p_arma_type == imatT) {
    throw std::logic_error("ERROR: binomial hyperparameter p must be a continuous variable type (double, vec, or mat).");
  }

  switch(x_arma->getArmaType()) {
  case doubleT:
    if(observed) {
      p = assignBinomialLogp<cppbugs::ObservedBinomial>(x_arma->getDouble(),n_arma,p_arma);
    } else {
      p = assignBinomialLogp<cppbugs::Binomial>(x_arma->getDouble(),n_arma,p_arma);
    }
    break;
  case vecT:
    if(observed) {
      p = assignBinomialLogp<cppbugs::ObservedBinomial>(x_arma->getVec(),n_arma,p_arma);
    } else {
      p = assignBinomialLogp<cppbugs::Binomial>(x_arma->getVec(),n_arma,p_arma);
    }
    break;
  case matT:
    if(observed) {
      p = assignBinomialLogp<cppbugs::ObservedBinomial>(x_arma->getMat(),n_arma,p_arma);
    } else {
      p = assignBinomialLogp<cppbugs::Binomial>(x_arma->getMat(),n_arma,p_arma);
    }
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    //throw std::logic_error("ERROR: binomial must be an integer variable type.");
    throw std::logic_error("ERROR: binomial must be an discrete valued continuous variable type.  This is due to a small issue in armadillo.  email me if you want a full explanation");
  }
  return p;
}
