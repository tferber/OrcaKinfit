/*! \file 
 *  \brief Implements class NewFitterGSL
 *
 * Author: Benno List
 * $Date: 2011/05/03 13:16:41 $
 * $Author: blist $
 *
 * \b Changelog:
 * - 15.11.2010 First version
 *
 * \b CVS Log messages:
 * - $Log: NewFitterGSL.cc,v $
 * - Revision 1.3  2011/05/03 13:16:41  blist
 * - new version of NewFitterGSL
 * -
 * - Revision 1.2  2011/05/03 13:15:36  blist
 * - intermediate version of NewFitterGSL
 * -
 * - Revision 1.1  2011/03/03 15:03:03  blist
 * - Latest version, with NewFitterGSL
 * -
 *
 */ 

#undef NDEBUG

#include "NewFitterGSL.h" 

#include<iostream>
#include<cmath>
#include<cassert>
#include<limits>

#include "BaseFitObject.h"
#include "BaseHardConstraint.h"
#include "BaseSoftConstraint.h"
#include "BaseTracer.h"
#include "ftypes.h"
#include "cernlib.h"

#include <gsl/gsl_block.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_permutation.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_blas.h>

using std::cout;
using std::cerr;
using std::endl;
using std::abs;

static int debuglevel = 0;
static int nitdebug = 0;
// static int nitcalc = 0;
// static int nitsvd = 0;

// constructor
NewFitterGSL::NewFitterGSL() 
: npar (0), ncon (0), nsoft (0), idim (0),
  x(0), xold(0), xnew (0),
  // xbest(0), 
  dx(0), dxscal (0), 
  //grad(0), 
  y(0), yscal(0), 
  perr(0), v1 (0), v2(0), 
  //Meval (0),
  M(0), Mscal (0), W(0), W2 (0), W3 (0),
  //M1(0), M2 (0), M3 (0), M4 (0), M5 (0), Mevec (0), 
  CC (0), CC1 (0), CCinv (0), 
  permW(0), 
  eigenws(0), eigenwsdim (0),
  imerit (1),
  try2ndOrderCorr (true),
  debug (debuglevel)
{}

// destructor
NewFitterGSL::~NewFitterGSL() {

  if (x) gsl_vector_free (x);               x=0;
  if (xold) gsl_vector_free (xold);         xold=0;
  if (xnew) gsl_vector_free (xnew);         xnew=0;
//   if (xbest) gsl_vector_free (xbest);       xbest=0;
  if (dx) gsl_vector_free (dx);             dx=0;
  if (dxscal) gsl_vector_free (dxscal);     dxscal=0;
//   if (grad) gsl_vector_free (grad);         grad=0;
  if (y) gsl_vector_free (y);               y=0;
  if (yscal) gsl_vector_free (yscal);       yscal=0;
  if (perr) gsl_vector_free (perr);         perr=0;
  if (v1) gsl_vector_free (v1);             v1=0;
  if (v2) gsl_vector_free (v2);             v2=0;
//   if (Meval) gsl_vector_free (Meval);       Meval=0;
  if (M) gsl_matrix_free (M);               M=0;
  if (Mscal) gsl_matrix_free (Mscal);       Mscal=0;
  if (W) gsl_matrix_free (W);               W=0;
  if (W2) gsl_matrix_free (W2);             W2=0;
  if (W3) gsl_matrix_free (W3);             W3=0;
//   if (M1) gsl_matrix_free (M1);             M1=0;
//   if (M2) gsl_matrix_free (M2);             M2=0;
//   if (M3) gsl_matrix_free (M3);             M3=0;
//   if (M4) gsl_matrix_free (M4);             M4=0;
//   if (M5) gsl_matrix_free (M5);             M5=0;
//   if (Mevec) gsl_matrix_free (Mevec);       Mevec=0;
  if (CC) gsl_matrix_free (CC);             CC=0;
  if (CC1) gsl_matrix_free (CC1);           CC1=0;
  if (CCinv) gsl_matrix_free (CCinv);       CCinv=0;
  if (permW) gsl_permutation_free (permW);  permW=0;
  if (eigenws) gsl_eigen_symm_free (eigenws); eigenws=0; eigenwsdim=0;
}



double NewFitterGSL::fit() {

  // order parameters etc
  initialize();
  
  // initialize eta, etasv, y   
  assert (x && x->size == idim);
  assert (xold && xold->size == idim);
  assert (xnew && xnew->size == idim);
  assert (dx && dx->size == idim);
  assert (y && y->size == idim);
  assert (perr && perr->size == idim);
  assert (v1 && v1->size == idim);
  assert (v2 && v2->size == idim);
//   assert (Meval && Meval->size == idim);
  assert (M && M->size1 == idim && M->size1 == idim);
  assert (W && W->size1 == idim && W->size1 == idim);
  assert (W2 && W2->size1 == idim && W2->size1 == idim);
//   assert (M1 && M1->size1 == idim && M1->size1 == idim);
//   assert (Mevec && Mevec->size1 == idim && Mevec->size1 == idim);
  assert (permW && permW->size == idim);
  
  gsl_vector_set_zero (x);
  gsl_vector_set_zero (y);
  gsl_vector_set_all (perr, 1);

  // Store initial x values in x
  fillx(x);    
  if (debug>4) {
    cout << "fit: Start values: \n";
    debug_print (x, "x");
  }
  // make sure parameters are consistent
  updateParams (x);
  fillx(x);    
  
  assembleConstDer (M);
  determineLambdas (x, M, x, W, v1); 
  
  // Get starting values into x
//  gsl_vector_memcpy (x, xold);  
  
  
  // LET THE GAMES BEGIN
  
#ifndef FIT_TRACEOFF
  calcChi2();
  traceValues["alpha"] = 0;
  traceValues["phi"] = 0;
  traceValues["mu"] = 0;
  traceValues["detW"] = 0;
  if (tracer) tracer->initialize (*this);
#endif   
  
  bool converged = 0;
  ierr = 0;
  
  double chi2new = calcChi2();
  nit = 0;
  
  do {
#ifndef FIT_TRACEOFF
    if (tracer) tracer->step (*this);
#endif  
      
    // Store old x values in xold
    gsl_blas_dcopy (x, xold);    
    // Fill errors into perr
    fillperr(perr);    

    // Now, calculate the result vector y with the values of the derivatives
    // d chi^2/d x
    int ifail = calcNewtonDx(dx, dxscal, x, perr, M, Mscal, y, yscal, W, W2, permW, v1);
    
    if (ifail) {
      ierr = 99;
      if (debug > 0) {
        std::cout << "NewFitterGSL::fit: calcNewtonDx error " << ifail << std::endl;
      }
      
      break;
    }
    
    // test convergence: 
    if (gsl_blas_dasum (dxscal) < 1E-6*idim) {
      converged = true;
      break;
    }
    
    double alpha = 1;
    double mu = 0;
    int imode = 2;
    
    calcLimitedDx (alpha, mu, xnew, imode, x, v2, dx, dxscal, perr, M, Mscal, W, v1);

    gsl_blas_dcopy (xnew, x);    

    chi2new = calcChi2();
    //cout << "chi2: " << chi2old << " -> " << chi2new << endl;
    
//   *-- Convergence criteria 

    ++nit;
    if (nit > 200) ierr = 1;
    
    converged = (abs (chi2new - chi2old) < 0.0001);
                
//     if (abs (chi2new - chi2old) >= 0.001)
//       cout << "abs (chi2new - chi2old)=" << abs (chi2new - chi2old) << " -> try again\n";      
//     if (fvalbest >= 1E-3)
//       cout << "fvalbest=" << fvalbest << " -> try again\n";      
//     if (fvalbest >= 1E-6 && abs(fvals[0]-fvalbest) >= 0.2*fvalbest )
//       cout << "fvalbest=" << fvalbest  
//            << ", abs(fvals[0]-fvalbest)=" << abs(fvals[0]-fvalbest)<< " -> try again\n";      
//     if (stepbest >= 1E-3)
//       cout << "stepbest=" << stepbest << " -> try again\n";      
//     cout << "converged=" << converged << endl;
    if (debug > 2 && converged) {
      cout << "abs (chi2new - chi2old)=" << abs (chi2new - chi2old) << "\n"      
           << "fvalbest=" << fvalbest << "\n"
           << "abs(fvals[0]-fvalbest)=" << abs(fvals[0]-fvalbest)<< "\n";      
    } 
 
  } while (!(converged || ierr));
  
#ifndef FIT_TRACEOFF
  if (tracer) tracer->step (*this);
#endif  
  
// *-- End of iterations - calculate errors.

// // ERROR CALCULATION 
// 
//   if (!ierr) {
// 
//     calcCovMatrix();  
// 
//     // update errors in fitobjects
//     for (unsigned int ifitobj = 0; ifitobj < fitobjects.size(); ++ifitobj) {
//       for (int ilocal = 0; ilocal < fitobjects[ifitobj]->getNPar(); ++ilocal) {
//         int iglobal = fitobjects[ifitobj]->getGlobalParNum (ilocal); 
//         for (int jlocal = ilocal; jlocal < fitobjects[ifitobj]->getNPar(); ++jlocal) {
//           int jglobal = fitobjects[ifitobj]->getGlobalParNum (jlocal); 
//           if (iglobal >= 0 && jglobal >= 0) 
//           fitobjects[ifitobj]->setCov(ilocal, jlocal, gsl_matrix_get (CCinv, iglobal, jglobal)); 
//         }
//       }
//     }
//   }
//   
// 
//   if (debug>1) {
//     cout << "========= END =========\n";
//     cout << "Fit objects:\n";
//     for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
//       BaseFitObject *fo = *i;
//       assert (fo);
//       cout << fo->getName() << ": " << *fo << ", chi2=" << fo->getChi2() << endl;
//     }
//     cout << "constraints:\n";
//     for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
//       BaseHardConstraint *c = *i;
//       assert (c);
//       cout << i-constraints.begin() << " " << c->getName() << ": " << c->getValue() << "+-" << c->getError() << endl;
//     }
//     cout << "=============================================\n";
//   }

// *-- Turn chisq into probability.
  FReal chi = FReal(chi2new);
  fitprob = (chi >= 0 && ncon+nsoft-nunm> 0) ? prob(chi, ncon+nsoft-nunm) : -1;
  
#ifndef FIT_TRACEOFF
    if (tracer) tracer->finish (*this);
#endif   

  if (debug > 0) {
    cout << "NewFitterGSL::fit: converged=" << converged
         << ", nit=" << nit << ", fitprob=" << fitprob << endl;
  }

  if (ierr > 0) fitprob = -1;

  return fitprob;
    
}

bool NewFitterGSL::initialize() {
  covValid = false;
//  bool debug = true;

  // tell fitobjects the global ordering of their parameters:
  npar = 0;
  nunm = 0;
  // 
  for (unsigned int ifitobj = 0; ifitobj < fitobjects.size(); ++ifitobj) {
    for (int ilocal = 0; ilocal < fitobjects[ifitobj]->getNPar(); ++ilocal) {
      if (!fitobjects[ifitobj]->isParamFixed(ilocal)) {
        fitobjects[ifitobj]->setGlobalParNum (ilocal, npar);
        ++npar;
        if (!fitobjects[ifitobj]->isParamMeasured(ilocal)) ++nunm;
      }
    }
  }
  
  // set number of constraints
  ncon = constraints.size();
  // Tell the constraints their numbers
  for (unsigned int icon = 0; icon < constraints.size(); ++icon) {
    BaseHardConstraint *c = constraints[icon];
    assert (c);
    c->setGlobalNum (npar+icon);
//    if (debug) cout << "Constraint " << icon << " -> global " << c->getGlobalNum() << endl;
  }
  
  nsoft = softconstraints.size();
  
  if (nunm > ncon+nsoft) {
    cerr << "NewFitterGSL::initialize: nunm=" << nunm << " > ncon+nsoft=" 
         << ncon << "+" << nsoft << endl;
  }
  
  idim = npar+ncon;
  
  ini_gsl_vector (x, idim);
  ini_gsl_vector (xold, idim);
  ini_gsl_vector (xnew, idim);
//   ini_gsl_vector (xbest, idim);
  ini_gsl_vector (dx, idim);
  ini_gsl_vector (dxscal, idim);
//   ini_gsl_vector (grad, idim);
  ini_gsl_vector (y, idim);
  ini_gsl_vector (yscal, idim);
  ini_gsl_vector (perr, idim);
  ini_gsl_vector (v1, idim);
  ini_gsl_vector (v2, idim);
//   ini_gsl_vector (Meval, idim);
  
  ini_gsl_matrix (M, idim, idim);
  ini_gsl_matrix (Mscal, idim, idim);
  ini_gsl_matrix (W, idim, idim);
  ini_gsl_matrix (W2, idim, idim);
  ini_gsl_matrix (W3, idim, idim);
//   ini_gsl_matrix (M1, idim, idim);
//   ini_gsl_matrix (M2, idim, idim);
//   ini_gsl_matrix (M3, idim, idim);
//   ini_gsl_matrix (M4, idim, idim);
//   ini_gsl_matrix (M5, idim, idim);
//   ini_gsl_matrix (Mevec, idim, idim);
//   ini_gsl_matrix (CC, idim, idim);
//   ini_gsl_matrix (CC1, idim, idim);
//   ini_gsl_matrix (CCinv, idim, idim);
  
  ini_gsl_permutation (permW, idim);
  
  if (eigenws && eigenwsdim != idim) {
    gsl_eigen_symm_free (eigenws); 
    eigenws = 0;
  }
  if (eigenws == 0) eigenws = gsl_eigen_symm_alloc (idim); 
  eigenwsdim = idim;
 
  return true;

}
  
double NewFitterGSL::calcChi2() {
  chi2 = 0;
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    chi2 += fo->getChi2();
  }
  for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
    BaseSoftConstraint *bsc = *i;
    assert (bsc);
    chi2 += bsc->getChi2();
  }
  return chi2;
}

int NewFitterGSL::getError() const {return ierr;}
double NewFitterGSL::getProbability() const {return fitprob;}
double NewFitterGSL::getChi2() const {return chi2;}
int NewFitterGSL::getDoF() const {return ncon+nsoft-nunm;}
int NewFitterGSL::getIterations() const {return nit;}

void NewFitterGSL::ini_gsl_permutation (gsl_permutation *&p, unsigned int size) {
  if (p) {
    if (p->size != size) {
      gsl_permutation_free (p);
      if (size > 0) p = gsl_permutation_alloc (size);
      else p=0;
    }
  }
  else 
    if (size > 0) p = gsl_permutation_alloc (size);
}

void NewFitterGSL::ini_gsl_vector (gsl_vector *&v, unsigned int size) {
  
  if (v) {
    if (v->size != size) {
      gsl_vector_free (v);
      if (size>0) v = gsl_vector_alloc (size);
      else v=0;
    }
  }
  else 
    if (size > 0) v = gsl_vector_alloc (size);
}

void NewFitterGSL::ini_gsl_matrix (gsl_matrix *&m, unsigned int size1, unsigned int size2) {
  if (m) {
    if (m->size1 != size1 || m->size2 != size2) {
      gsl_matrix_free (m);
      if (size1*size2 > 0) m = gsl_matrix_alloc (size1, size2);
      else m=0;
    }
  }
  else 
    if (size1*size2 > 0) m = gsl_matrix_alloc (size1, size2);
}

void NewFitterGSL::debug_print (const gsl_matrix *m, const char *name) {
  for (unsigned int  i = 0; i < m->size1; ++i) 
    for (unsigned int j = 0; j < m->size2; ++j)
      if (gsl_matrix_get (m, i, j) != 0)
        cout << name << "[" << i << "][" << j << "]=" << gsl_matrix_get (m, i, j) << endl;
}

void NewFitterGSL::debug_print (const gsl_vector *v, const char *name) {
  for (unsigned int  i = 0; i < v->size; ++i) 
      if (gsl_vector_get (v, i) != 0)
        cout << name << "[" << i << "]=" << gsl_vector_get (v, i) << endl;
}

void NewFitterGSL::add (gsl_vector *vecz, const gsl_vector *vecx, double a, const gsl_vector *vecy) {
  assert (vecx);
  assert (vecy);
  assert (vecz);
  assert (vecx->size == vecy->size);
  assert (vecx->size == vecz->size);

  gsl_blas_dcopy (vecx, vecz);
  gsl_blas_daxpy (a, vecy, vecz);
}

int NewFitterGSL::getNcon() const {return ncon;}
int NewFitterGSL::getNsoft() const {return nsoft;}
int NewFitterGSL::getNunm() const {return nunm;}
int NewFitterGSL::getNpar() const {return npar;}

bool NewFitterGSL::updateParams (gsl_vector *vecx) {
  assert (vecx);
  assert (vecx->size == idim);
  bool significant = false;
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    bool s = fo->updateParams (vecx->block->data, vecx->size);
    significant |=  s;
    if (debug>5 && nit<nitdebug && s) {
      cout << "Significant update for FO " << i-fitobjects.begin() << " (" 
           << fo->getName() << ")\n";
    }
  }
  return significant;
}

bool NewFitterGSL::isfinite (const gsl_vector *vec) {
  if (vec == 0) return true;
  for (size_t i = 0; i < vec->size; ++i)
    if (!std::isfinite (gsl_vector_get (vec, i))) return false;
  return true;
}

bool NewFitterGSL::isfinite (const gsl_matrix *mat) {
  if (mat == 0) return true;
  for (size_t i = 0; i < mat->size1; ++i)
    for (size_t j = 0; j < mat->size2; ++j)
      if (!std::isfinite (gsl_matrix_get (mat, i, j))) return false;
  return true;
}


void NewFitterGSL::fillx(gsl_vector *vecx) {
  assert (vecx);
  assert (vecx->size == idim);
  
  gsl_vector_set_zero (vecx);
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    for (int ilocal = 0; ilocal < fo->getNPar(); ++ilocal) {
      if (!fo->isParamFixed(ilocal)) {
        int iglobal = fo->getGlobalParNum (ilocal);
        assert (iglobal >= 0 && iglobal < npar);
        gsl_vector_set (vecx, iglobal, fo->getParam (ilocal));
      }
    }
  }
}

void NewFitterGSL::fillperr(gsl_vector *vece) {
  assert (vece);
  assert (vece->size == idim);
  gsl_vector_set_all (vece, 1);
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    for (int ilocal = 0; ilocal < fo->getNPar(); ++ilocal) {
      if (!fo->isParamFixed(ilocal)) {
        int iglobal = fo->getGlobalParNum (ilocal);
        assert (iglobal >= 0 && iglobal < npar);
        double e = std::abs(fo->getError (ilocal));
        gsl_vector_set (vece, iglobal, e ? e : 1);
      }
    }
  }
  for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
    BaseHardConstraint *c = *i;
    assert (c);
    int iglobal = c->getGlobalNum ();
    assert (iglobal >= 0 && iglobal < (int)idim);
    double e =  c->getError();
    gsl_vector_set (vece, iglobal, e ? 1/e : 1);
  }
}

void NewFitterGSL::assembleM(gsl_matrix *MatM, const gsl_vector *vecx) {
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (vecx);
  assert (vecx->size == idim);
  
  gsl_matrix_set_zero (MatM);
  // First, all terms d^2 chi^2/dx1 dx2
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    fo->addToGlobalChi2DerMatrix (MatM->block->data, MatM->tda);
    if (debug > 0 && !isfinite (MatM)) {
      cout << "NewFitterGSL::assembleM: illegal elements in MatM after adding fo " << *fo << ":\n";
      if (debug > 5) debug_print (MatM, "M");
    }
  }
  
  // Second, all terms d^2 chi^2/dlambda dx, 
  // i.e. the first derivatives of the contraints,
  // plus the second derivatives times the lambda values
  for (unsigned int k = 0; k < constraints.size(); ++k) {
    BaseHardConstraint *c = constraints[k];
    assert (c);
    int kglobal = c->getGlobalNum();
    assert (kglobal >= 0 && kglobal < (int)idim);
    c->add1stDerivativesToMatrix (MatM->block->data, MatM->tda);
    if (debug > 0 && !isfinite (MatM)) {
      cout << "NewFitterGSL::assembleM: illegal elements in MatM after adding 1st derivatives of constraint " << *c << ":\n";
      if (debug > 3) debug_print (MatM, "M");
    }
    c->add2ndDerivativesToMatrix (MatM->block->data, MatM->tda, gsl_vector_get (vecx, kglobal));
    if (debug > 0 && !isfinite (MatM)) {
      cout << "NewFitterGSL::assembleM: illegal elements in MatM after adding 2nd derivatives of constraint " << *c << ":\n";
      if (debug > 3) debug_print (MatM, "M");
    }
  }
  
  // Finally, treat the soft constraints

  for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
    BaseSoftConstraint *bsc = *i;
    assert (bsc);
    bsc->add2ndDerivativesToMatrix (MatM->block->data, MatM->tda);
    if (debug > 0 && !isfinite (MatM)) {
      cout << "NewFitterGSL::assembleM: illegal elements in MatM after adding soft constraint " << *bsc << ":\n";
      if (debug > 3) debug_print (MatM, "M");
    }
  }
}

void NewFitterGSL::assembleG (gsl_matrix *MatM, const gsl_vector *vecx) {
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (vecx);
  assert (vecx->size == idim);
  
  gsl_matrix_set_zero (MatM);
  // First, all terms d^2 chi^2/dx1 dx2
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    fo->addToGlobalChi2DerMatrix (MatM->block->data, MatM->tda);
  }
  
  // Second,  the second derivatives times the lambda values
  for (unsigned int k = 0; k < constraints.size(); ++k) {
    BaseHardConstraint *c = constraints[k];
    assert (c);
    int kglobal = c->getGlobalNum();
    assert (kglobal >= 0 && kglobal < (int)idim);
    c->add2ndDerivativesToMatrix (MatM->block->data, MatM->tda, gsl_vector_get (vecx, kglobal));
  }
  
  // Finally, treat the soft constraints

  for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
    BaseSoftConstraint *bsc = *i;
    assert (bsc);
    bsc->add2ndDerivativesToMatrix (MatM->block->data, MatM->tda);
  }
}

void NewFitterGSL::scaleM (gsl_matrix *MatMscal, const gsl_matrix *MatM, const gsl_vector *vece) {
  assert (MatMscal);
  assert (MatMscal->size1 == idim && MatMscal->size2 == idim);
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (vece);
  assert (vece->size == idim);

  // Rescale columns and rows by perr
  for (unsigned int i = 0; i < idim; ++i) 
    for (unsigned int j = 0; j < idim; ++j) 
      gsl_matrix_set (MatMscal, i, j,  
        gsl_vector_get (vece, i)*gsl_vector_get (vece, j)*gsl_matrix_get (MatM, i, j));
}



void NewFitterGSL::assembley(gsl_vector *vecy, const gsl_vector *vecx) {
  assert (vecy);
  assert (vecy->size == idim);
  assert (vecx);
  assert (vecx->size == idim);
  gsl_vector_set_zero (vecy);
  // First, for the parameters
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    fo->addToGlobalChi2DerVector (vecy->block->data, vecy->size);
  }
  
  // Now add lambda*derivatives of constraints,
  // And finally, the derivatives w.r.t. to the constraints, i.e. the constraints themselves
  for (unsigned int k = 0; k < constraints.size(); ++k) {
    BaseHardConstraint *c = constraints[k];
    assert (c);
    int kglobal = c->getGlobalNum();
    assert (kglobal >= 0 && kglobal < (int)idim);
    c->addToGlobalChi2DerVector (vecy->block->data, vecy->size, gsl_vector_get (vecx, kglobal));
    gsl_vector_set (vecy, kglobal, c->getValue());
  }
  
    // Finally, treat the soft constraints

  for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
    BaseSoftConstraint *bsc = *i;
    assert (bsc);
    bsc->addToGlobalChi2DerVector (vecy->block->data, vecy->size);
  }
}

void NewFitterGSL::scaley (gsl_vector *vecyscal, const gsl_vector *vecy, const gsl_vector *vece) {
  assert (vecyscal);
  assert (vecyscal->size == idim);
  assert (vecy);
  assert (vecy->size == idim);
  assert (vece);
  assert (vece->size == idim);

  gsl_vector_memcpy (vecyscal, vecy);
  gsl_vector_mul (vecyscal, vece);
}

void NewFitterGSL::assembleChi2Der (gsl_vector *vecy) {
  assert (vecy);
  assert (vecy->size == idim);

  gsl_vector_set_zero (vecy);
  // First, for the parameters
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    fo->addToGlobalChi2DerVector (vecy->block->data, vecy->size);
  }
  
  // Treat the soft constraints

  for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
    BaseSoftConstraint *bsc = *i;
    assert (bsc);
    bsc->addToGlobalChi2DerVector (vecy->block->data, vecy->size);
  }
}

void NewFitterGSL::addConstraints (gsl_vector *vecy) {
  assert (vecy);
  assert (vecy->size == idim);

  // Now add the derivatives w.r.t. to the constraints, i.e. the constraints themselves
  for (unsigned int k = 0; k < constraints.size(); ++k) {
    BaseHardConstraint *c = constraints[k];
    assert (c);
    int kglobal = c->getGlobalNum();
    gsl_vector_set (vecy, kglobal, c->getValue());
  }
}

void NewFitterGSL::assembleConstDer (gsl_matrix *MatM) {
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);

  gsl_matrix_set_zero (MatM);
  
  // The first derivatives of the contraints,
  for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
    BaseHardConstraint *c = *i;    
    assert (c);
    int kglobal = c->getGlobalNum();
    assert (kglobal >= 0 && kglobal < (int)idim);
    c->add1stDerivativesToMatrix (MatM->block->data, MatM->tda);
  }
}

int NewFitterGSL::calcNewtonDx (gsl_vector *vecdx, gsl_vector *vecdxscal, 
                                gsl_vector *vecx, const gsl_vector *vece,      
                                gsl_matrix *MatM, gsl_matrix *MatMscal,  
                                gsl_vector *vecy, gsl_vector *vecyscal, 
                                gsl_matrix *MatW, gsl_matrix *MatW2, 
                                gsl_permutation *permW,
                                gsl_vector *vecw
                               ) {
  assert (vecdx);
  assert (vecdx->size == idim);
  assert (vecdxscal);
  assert (vecdxscal->size == idim);
  assert (vecx);
  assert (vecx->size == idim);
  assert (vece);
  assert (vece->size == idim);
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (MatMscal);
  assert (MatMscal->size1 == idim && MatMscal->size2 == idim);
  assert (vecy);
  assert (vecy->size == idim);
  assert (vecyscal);
  assert (vecyscal->size == idim);
  assert (MatW);
  assert (MatW->size1 == idim && MatW->size2 == idim);
  assert (MatW2);
  assert (MatW2->size1 == idim && MatW2->size2 == idim);
  assert (permW);
  assert (permW->size == idim);
  assert (vecw);
  assert (vecw->size == idim);
  
  int ncalc = 0;
  double ptLp = 0;
  
  do {
    if (ncalc == 1) {
      // try to recalculate lambdas
      assembleConstDer (MatM);
      determineLambdas (vecx, MatM, vecx, MatW, vecw); 
      if (debug>2) cout << "NewFitterGSL::calcNewtonDx: ptLp=" << ptLp << " with lambdas from last iteration" << endl;
    }
    else if (ncalc == 2) {
      // try to set lambdas to zero
      gsl_vector_view lambda (gsl_vector_subvector (vecx, npar, ncon));
      gsl_vector_set_zero (&lambda.vector);
      if (debug>2) cout << "NewFitterGSL::calcNewtonDx: ptLp=" << ptLp << " with recalculated lambdas" << endl;
    }
    else if (ncalc >= 3) {
      if (debug>2) cout << "NewFitterGSL::calcNewtonDx: ptLp=" << ptLp << " with zero lambdas" << endl;
      break;
    }    
      
    if (debug>5) {
      cout << "calcNewtonDx: before setting up equations: \n";
      debug_print (vecx, "x");
    }
         
    assembleM (MatM, vecx);
    if (!isfinite (MatM)) return 1;
    
    scaleM  (MatMscal, MatM, vece);
    assembley (vecy, vecx);
    if (!isfinite (vecy)) return 2;
    scaley (vecyscal, vecy, vece);
      
    if (debug>5) {
      cout << "calcNewtonDx: After setting up equations: \n";
      debug_print (MatM, "M");
      debug_print (MatMscal, "Mscal");
      debug_print (vecy, "y");
      debug_print (vecyscal, "yscal");
      debug_print (vece, "perr");
      debug_print (vecx, "x");
      debug_print (xnew, "xnew");
    }
        
    // from x_(n+1) = x_n - y/y' = x_n - M^(-1)*y we have M*(x_n-x_(n+1)) = y, 
    // which we solve for dx = x_n-x_(n+1) and hence x_(n+1) = x_n-dx
  
    double epsLU = 1E-12;
    double epsSV = 1E-3;
    double detW;
    solveSystem (vecdxscal, detW, vecyscal, MatMscal, MatW, MatW2, vecw, epsLU, epsSV);
    

#ifndef FIT_TRACEOFF
    traceValues["detW"] = detW;
#endif  
  
    // step is - computed vector
    gsl_blas_dscal (-1, dxscal);
  
    // dx = dxscal*e (component wise)
    gsl_vector_memcpy (vecdx, vecdxscal);
    gsl_vector_mul (vecdx, vece);

    if (debug>5) {
      cout << "calcNewtonDx: Result: \n";
      debug_print (vecdx, "dx");
      debug_print (vecdxscal, "dxscal");
    }              

    
    ptLp = calcpTLp (dx, M, v1);
    ++ncalc;
  }
  while (ptLp < 0);

  return 0;                
}  

int NewFitterGSL::calcLimitedDx (double& alpha, double& mu, gsl_vector *vecxnew, 
                                 int imode,
                                 gsl_vector *vecx, gsl_vector *vecdxhat,
                                 const gsl_vector *vecdx, const gsl_vector *vecdxscal,
                                 const gsl_vector *vece, 
                                 const gsl_matrix *MatM, const gsl_matrix *MatMscal,
                                 gsl_matrix *MatW, gsl_vector *vecw
                                ) { 
  assert (vecxnew);
  assert (vecxnew->size == idim);
  assert (vecx);
  assert (vecx->size == idim);
  assert (vecdxhat);
  assert (vecdxhat->size == idim);
  assert (vecdx);
  assert (vecdx->size == idim);
  assert (vecdxscal);
  assert (vecdxscal->size == idim);
  assert (vece);
  assert (vece->size == idim);
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (MatMscal);
  assert (MatMscal->size1 == idim && MatMscal->size2 == idim);
  assert (MatW);
  assert (MatW->size1 == idim && MatW->size2 == idim);
  assert (vecw);
  assert (vecw->size == idim);
    
  alpha = 1;
  double eta = 0.1;
  double zeta = 0.5;

  add (vecxnew, vecx, alpha, vecdx);
  
  if (debug>5) {
    cout << "calcLimitedDx: After solving equations: \n";
    debug_print (vecx, "x");
    gsl_blas_dcopy (vecx, vecw);
    gsl_vector_div (vecw, vece);
    debug_print (vecw, "xscal");
    debug_print (vecxnew, "xnew");
    gsl_blas_dcopy (vecxnew, vecw);
    gsl_vector_div (vecw, vece);
    debug_print (vecw, "xnewscal");
  }

  mu = calcMu (vecx, vece, vecdx, vecdxscal, vecxnew, MatM, MatMscal, vecw);
    
  updateParams (vecx);
  
  double phi0  = meritFunction (mu, vecx, vece);
  double dphi0 = meritFunctionDeriv (mu, vecx, vece, vecdx, vecw); 

  double dphinum, eps=1E-6;
  if (debug > 2) {
    add (vecw, vecx, eps, vecdx);
    updateParams (vecw);
    dphinum = (meritFunction (mu, vecw, vece) - phi0)/eps;
    updateParams (vecx);
    
    cout << "analytic der: " << dphi0 << ", num=" << dphinum << endl;
  }
  
#ifndef FIT_TRACEOFF
  traceValues["alpha"] = 0;
  traceValues["phi"] = phi0;
  traceValues["mu"] = mu;
  if (tracer) tracer->substep (*this, 0);
#endif   
    
  updateParams (vecxnew);
  
  double phiR = meritFunction (mu, vecxnew, vece);
  
  if (debug > 2) {
    cout << "calcLimitedDx: phi0=" << phi0 << ", dphi0=" << dphi0
         << ", phiR = " << phiR << ", mu=" << mu 
         << ", threshold = " << phi0 + eta*dphi0
         << " => test=" << (phiR > phi0 + eta*dphi0)
         << endl;
  }
  
#ifndef FIT_TRACEOFF
  traceValues["alpha"] = 1;
  traceValues["phi"] = phiR;
  if (tracer) tracer->substep (*this, 0);
#endif   

  
  // Try Armijo's rule for alpha=1 first, do linesearch only if it fails
  if (phiR > phi0 + eta*alpha*dphi0) {
  
    // try second order correction first
    if (try2ndOrderCorr) {
      calc2ndOrderCorr (vecdxhat, vecxnew, MatM, MatW, vecw);
      gsl_blas_dcopy (vecxnew, vecw);
      add (vecxnew, vecxnew, 1, vecdxhat);
      updateParams (vecxnew);
      double phi2ndOrder  = meritFunction (mu, vecxnew, vece);
      
      #ifndef FIT_TRACEOFF
        traceValues["alpha"] = 1.5;
        traceValues["phi"] = phi2ndOrder;
        if (tracer) tracer->substep (*this, 2);
      #endif   

      if (debug > 2) {
        cout << "calcLimitedDx: tried 2nd order correction, phi2ndOrder = " 
             <<phi2ndOrder << ", threshold = " << phi0 + eta*dphi0 << endl;
        if (debug > 5) debug_print (vecdxhat, "dxhat");
        if (debug > 5) debug_print (xnew, "xnew");
      }
      if (phi2ndOrder <= phi0 + eta*alpha*dphi0) {
        if (debug > 2) 
          cout << "  -> 2nd order correction successfull!"  << endl;
        return 1;
      }
      if (debug > 2) 
        cout << "  -> 2nd order correction failed, do linesearch!"  << endl;
      gsl_blas_dcopy (vecw, vecxnew);
      updateParams (vecxnew);
      #ifndef FIT_TRACEOFF
        calcChi2();
        traceValues["alpha"] = 1;
        traceValues["phi"] = phiR;
        if (tracer) tracer->substep (*this, 2);
      #endif   

    }

    doLineSearch (alpha, vecxnew, imode, phi0, dphi0, phiR, eta, zeta, mu, 
                  vecx, vecdx, vece, vecw);
  }
  
  return 0;
}
                
int NewFitterGSL::doLineSearch (double& alpha, gsl_vector *vecxnew, 
                                int imode, 
                                double phi0, double dphi0, double phiR,
                                double eta, double zeta,  
                                double mu,
                                const gsl_vector *vecx, const gsl_vector *vecdx,       
                                const gsl_vector *vece, gsl_vector *vecw) {
  assert (vecxnew);
  assert (vecxnew->size == idim);
  assert (vecx);
  assert (vecx->size == idim);
  assert (vecdx);
  assert (vecdx->size == idim);
  assert (vece);
  assert (vece->size == idim);
  assert (vecw);
  assert (vecw->size == idim);
  
  // don't do anything for imode = -1
  if (imode < 0) return -1;
  
  assert ((imode == 0) || eta<zeta);
  
  if (dphi0 >= 0) {
    // Difficult situation: merit function will increase,
    // thus every step makes it worse
    // => choose the minimum step and return
    if (debug > 2) cout << "NewFitterGSL::doLineSearch: dphi0 > 0!" << endl;
    // Choose new alpha
    alpha = 0.001;
    
    add (vecxnew, vecx, alpha, vecdx);
    updateParams (vecxnew);
  
    double phi = meritFunction (mu, vecxnew, vece);
  
#ifndef FIT_TRACEOFF
    traceValues["alpha"] = alpha;
    traceValues["phi"] = phi;
    if (tracer) tracer->substep (*this, 1);
#endif   
    return 2;
  }
  
  // alpha=1 already tried
  double alphaR = alpha;
  // vecxnew = vecx + alpha*vecdx
  add (vecxnew, vecx, alpha, vecdx);
  updateParams (vecxnew);
    
  double alphaL = 0;
  double phiL = phi0;
  double dphiL = dphi0;
  
  double phi, dphi;
  int nit = 0;
  
  do {
    nit++;
    // Choose new alpha
    alpha = 0.5*(alphaL + alphaR);
    
    add (vecxnew, vecx, alpha, vecdx);
    updateParams (vecxnew);
  
    phi = meritFunction (mu, vecxnew, vece);
  
#ifndef FIT_TRACEOFF
    traceValues["alpha"] = alpha;
    traceValues["phi"] = phi;
    if (tracer) tracer->substep (*this, 1);
#endif   
    
    // Armijo's rule always holds
    if (phi >= phi0 + eta*alpha*dphi0) {
      if (debug > 5) cout << "NewFitterGSL::doLineSearch, Armijo: phi=" << phi 
                          << " >= " << phi0 + eta*alpha*dphi0 
                          << " at alpha=" << alpha << endl;
      alphaR = alpha;
      phiR = phi;
      continue;
    }
    
    if (imode == 0) {       // Armijo
      break;
    }
    else if (imode == 1) {  // Wolfe
      dphi = meritFunctionDeriv (mu, vecxnew, vece, vecdx, vecw); 
      if (dphi < zeta*dphi0) {
        if (debug > 5) cout << "NewFitterGSL::doLineSearch, Wolfe: dphi=" << dphi 
                            << " < " << zeta*dphi0 
                            << " at alpha=" << alpha << endl;
        alphaL = alpha;
        phiL   = phi;
        dphiL  = dphi;
      }
      else {     
        break;
      }  
    }
    else {                  // Goldstein
      if (phi < phi0 + zeta*alpha*dphi0) {
        if (debug > 5) cout << "NewFitterGSL::doLineSearch, Goldstein: phi=" << phi 
                            << " < " << phi0 + zeta*alpha*dphi0 
                            << " at alpha=" << alpha << endl;
        alphaL = alpha;
        phiL   = phi;
      }
      else {     
        break;
      }  
    }
  } while (nit < 30 && (alphaL == 0 || nit < 6));
  if (alphaL > 0) alpha = alphaL;
  return 1;
}


double NewFitterGSL::calcMu (const gsl_vector *vecx, const gsl_vector *vece, 
                             const gsl_vector *vecdx, const gsl_vector *vecdxscal, 
                             const gsl_vector *vecxnew, 
                             const gsl_matrix *MatM, const gsl_matrix *MatMscal, 
                             gsl_vector *vecw) {
  assert (vecx);
  assert (vecx->size == idim);
  assert (vece);
  assert (vece->size == idim);
  assert (vecdx);
  assert (vecdx->size == idim);
  assert (vecdxscal);
  assert (vecdxscal->size == idim);
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (MatMscal);
  assert (MatMscal->size1 == idim && MatMscal->size2 == idim);
  assert (vecw);
  assert (vecw->size == idim);
  
  double result = 0;
  switch (imerit) {
    case 1: // l1 penalty function, Nocedal&Wright Eq. (15.24)
      {
        result = 0;
//         for (int kglobal = npar; kglobal < npar+ncon; ++kglobal) {
//           double abslambda = std::fabs (gsl_vector_get (vecx, kglobal));
//           if (abslambda > result)
//             result = abslambda;
//         }
        // calculate grad f^T*p
        gsl_vector_set_zero (vecw);
        addConstraints (vecw);
        gsl_vector_view c (gsl_vector_subvector (vecw, npar, ncon));
        // ||c||_1
        double cnorm1 = gsl_blas_dasum (&c.vector);
        // scale constraint values by 1/(delta e)
        gsl_vector_const_view lambdaerr (gsl_vector_const_subvector (vece, npar, ncon));
        gsl_vector_mul (&c.vector, &lambdaerr.vector);
        // ||c||_1
        double cnorm1scal = gsl_blas_dasum (&c.vector);

        double rho = 0.1;
        double eps = 0.001;
      
        assembleChi2Der (vecw);                              
        gsl_vector_view gradf (gsl_vector_subvector (vecw, 0, npar));               
        gsl_vector_const_view p (gsl_vector_const_subvector (vecdx, 0, npar));                  
        double gradfTp;                                      
        gsl_blas_ddot (&gradf.vector, &p.vector, &gradfTp);  
        
        if (debug > 7)
          cout << "NewFitterGSL::calcMu: cnorm1scal=" << cnorm1scal
               << ", gradfTp=" << gradfTp << endl;
      
        // all constraints very well fulfilled, use max(lambda+1) criterium
        if (cnorm1scal < ncon*eps || gradfTp <= 0) {
          for (int kglobal = npar; kglobal < npar+ncon; ++kglobal) {
            double abslambda = std::fabs (gsl_vector_get (vecxnew, kglobal));
            if (abslambda > result)
              result = abslambda;
          }
          result /= (1-rho);
        }
        else {
          // calculate p^T L p
          double pTLp = calcpTLp (vecdx, MatM, vecw);
          double sigma = (pTLp > 0) ? 1 : 0;
          if (debug > 7)
            cout << "  pTLp = " << pTLp << endl;
          // Nocedal&Wright Eq. (18.36)
          result = (gradfTp + 0.5*sigma*pTLp)/((1-rho)*cnorm1);
        }
        if (debug > 7)
          cout << "  result = " << result << endl;
      }
      break;
    case 2: // l1 penalty function, errors scaled, Nocedal&Wright Eq. (15.24)
      result = 0;
      for (int kglobal = npar; kglobal < npar+ncon; ++kglobal) {
        double abslambdascal = std::fabs (gsl_vector_get (vecx, kglobal)/gsl_vector_get (vece, kglobal));
        if (abslambdascal > result)
          result = abslambdascal;
      }
      break;
    default: assert (0);
  }
  return result;

}


double NewFitterGSL::meritFunction (double mu, const gsl_vector *vecx, const gsl_vector *vece){
  assert (vecx);
  assert (vecx->size == idim);
  assert (vece);
  assert (vece->size == idim);
  
  double result = 0;
  switch (imerit) {
    case 1: // l1 penalty function, Nocedal&Wright Eq. (15.24)
      result = calcChi2();
      for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
        BaseHardConstraint *c = *i;    
        assert (c);
        int kglobal = c->getGlobalNum();
        assert (kglobal >= 0 && kglobal < (int)idim);
        result += mu*std::fabs (c->getValue());
      }
      break;
    case 2: // l1 penalty function, errors scaled, Nocedal&Wright Eq. (15.24)
      result = calcChi2();
      for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
        BaseHardConstraint *c = *i;    
        assert (c);
        int kglobal = c->getGlobalNum();
        assert (kglobal >= 0 && kglobal < (int)idim);
        // vece[kglobal] is 1/error for constraint k
        result += mu*std::fabs (c->getValue()*gsl_vector_get (vece, kglobal));
      }
      break;
    default: assert (0);
  }
  return result;

}


double NewFitterGSL::meritFunctionDeriv (double mu, const gsl_vector *vecx, const gsl_vector *vece, 
                                         const gsl_vector *vecdx, gsl_vector *vecw) {
  assert (vecx);
  assert (vecx->size == idim);
  assert (vece);
  assert (vece->size == idim);
  assert (vecdx);
  assert (vecdx->size == idim);
  assert (vecw);
  assert (vecw->size == idim);

  double result = 0;
  switch (imerit) {
    case 1: // l1 penalty function, Nocedal&Wright Eq. (15.24), Eq. (18.29)
      assembleChi2Der (vecw);
      for (int i = 0; i < npar; ++i) {
        result += gsl_vector_get (vecdx, i)*gsl_vector_get (vecw, i);
      }
//       for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
//         BaseHardConstraint *c = *i;    
//         assert (c);
//         int kglobal = c->getGlobalNum();
//         assert (kglobal >= 0 && kglobal < (int)idim);
//         result +=  c->dirDerAbs (vecdx->block->data, vecw->block->data, npar, mu);
//       }
      for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
        BaseHardConstraint *c = *i;    
        assert (c);
        int kglobal = c->getGlobalNum();
        assert (kglobal >= 0 && kglobal < (int)idim);
        result -=  mu*std::fabs (c->getValue());
      }
      break;
    case 2: // l1 penalty function, errors scaled, Nocedal&Wright Eq. (15.24), Eq. (18.29)
      assembleChi2Der (vecw);
      for (int i = 0; i < npar; ++i) {
        result += gsl_vector_get (vecdx, i)*gsl_vector_get (vecw, i);
      }
//       for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
//         BaseHardConstraint *c = *i;    
//         assert (c);
//         int kglobal = c->getGlobalNum();
//         assert (kglobal >= 0 && kglobal < (int)idim);
//         result +=  c->dirDerAbs (vecdx->block->data, vecw->block->data, npar, mu*gsl_vector_get (vece, kglobal));
//       }
      for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
        BaseHardConstraint *c = *i;    
        assert (c);
        int kglobal = c->getGlobalNum();
        assert (kglobal >= 0 && kglobal < (int)idim);
        result -=  mu*std::fabs (c->getValue())*gsl_vector_get (vece, kglobal);
      }
      break;
    default: assert (0);
  }
  return result;
}

  
int NewFitterGSL::invertM() {
    gsl_matrix_memcpy (W, M);
    
    int ifail = 0;
    
    int signum;
    // Calculate LU decomposition of M into W
    int result = gsl_linalg_LU_decomp (W, permW, &signum);
    if (debug>1)cout << "invertM: gsl_linalg_LU_decomp result=" << result << endl;
    // Calculate inverse of M
    ifail = gsl_linalg_LU_invert (W, permW, M);
    if (debug>1)cout << "invertM: gsl_linalg_LU_invert result=" << ifail << endl;
    
    if (ifail != 0) {
      cerr << "NewtonFitter::invert: ifail from gsl_linalg_LU_invert=" << ifail << endl;
    }
    return ifail;
}

void NewFitterGSL::setDebug (int debuglevel) {
  debug = debuglevel;
}


// void NewFitterGSL::calcCovMatrix(gsl_matrix *MatW, gsl_permutation *permW,
//                                  gsl_matrix *MatM1, gsl_matrix *MatM2,
//                                  gsl_vector *vecx) {
//   // Set up equation system M*dadeta + dydeta = 0
//   // here, dadeta is d a / d eta, i.e. the derivatives of the fitted 
//   // parameters a w.r.t. to the measured parameters eta,
//   // and dydeta is the derivative of the set of equations
//   // w.r.t eta, i.e. simply d^2 chi^2 / d a d eta.
//   // Now, if chi2 = (a-eta)^T*Vinv((a-eta), we have simply
//   // d^2 chi^2 / d a d eta = - d^2 chi^2 / d a d a
//   // and can use the method addToGlobalChi2DerMatrix.
//   
//   gsl_matrix_set_zero (MatM1);
//   gsl_matrix_set_zero (MatM2);
//   // First, all terms d^2 chi^2/dx1 dx2
//   for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
//     BaseFitObject *fo = *i;
//     assert (fo);
//     fo->addToGlobalChi2DerMatrix (MatM1->block->data, MatM1->tda);
//     fo->addToGlobCov (MatM2->block->data, MatM2->tda);
//   }
//   // multiply by -1
//   gsl_matrix_scale (MatM1, -1);
//   
//   gsl_matrix_view dydeta  = gsl_matrix_submatrix (MatM1, 0, 0, idim, npar);
//   gsl_matrix_view Cov_eta = gsl_matrix_submatrix (MatM2, 0, 0, npar, npar);
//   
//   cout << "NewFitterGSL::calcCovMatrix\n";
//   debug_print (&dydeta.matrix, "dydeta");
//   debug_print (&Cov_eta.matrix, "Cov_eta");
//   
//   assembleM(MatW, vecx);
//   
//   // Now, solve M*dadeta = dydeta
// 
//   // Calculate LU decomposition of M into M3
//   int signum;
//   int result = gsl_linalg_LU_decomp (MatW, permW, &signum);
//   cout << "invertM: gsl_linalg_LU_decomp result=" << result << endl;
//   debug_print (M, "M_LU"); 
//   // Calculate inverse of M, store in M3
//   int ifail = gsl_linalg_LU_invert (MatW, permW, MatM3);
//   cout << "invertM: gsl_linalg_LU_invert ifail=" << ifail << endl;
//  
//   // Calculate dadeta = M3*dydeta
//   gsl_matrix_set_zero (MatM4);
//   gsl_matrix_view dadeta   = gsl_matrix_submatrix (MatM4, 0, 0, idim, npar);
// 
//   debug_print (&dadeta.matrix, "dadeta");
//   
//   // dadeta = 1*M*dydeta + 0*dadeta
//   gsl_blas_dgemm (CblasNoTrans, CblasNoTrans, 1, MatM3, &dydeta.matrix, 0, &dadeta.matrix);
//   
//   
//   // Now calculate Cov_a = dadeta*Cov_eta*dadeta^T
// 
//   // First, calculate M3 = Cov_eta*dadeta^T as 
//   gsl_matrix_view M3part   = gsl_matrix_submatrix (MatM3, 0, 0, npar, idim);
//   gsl_blas_dgemm (CblasNoTrans, CblasTrans, 1, &Cov_eta.matrix, &dadeta.matrix, 0, &M3part.matrix);
//   // Now Cov_a = dadeta*M3part
//   gsl_matrix_set_zero (MatM5);
//   gsl_matrix_view  Cov_a = gsl_matrix_submatrix (M5, 0, 0, npar, npar);
//   gsl_blas_dgemm (CblasNoTrans, CblasNoTrans, 1, &dadeta.matrix, &M3part.matrix, 0, M5);
// 
//   debug_print (&Cov_a.matrix, "Cov_a");
// 
//   // debug_print (CCinv, "full Cov from err prop");
//   // debug_print (M1, "uncorr Cov from err prop");
//     // Finally, copy covariance matrix
//   if (cov && covDim != npar) {
//     delete[] cov;
//     cov = 0;
//   }
//   covDim = npar;
//   if (!cov) cov = new double[covDim*covDim];
//   for (int i = 0; i < covDim; ++i) {
//     for (int j = 0; j < covDim; ++j) {
//       cov[i*covDim+j] = gsl_matrix_get (&Cov_a.matrix, i, j);
//     }
//   }    
//   covValid = true;
// }
  
void NewFitterGSL::determineLambdas (gsl_vector *vecxnew, 
                                     const gsl_matrix *MatM, const gsl_vector *vecx, 
                                     gsl_matrix *MatW, gsl_vector *vecw,
                                     double eps) {
  assert (vecxnew);
  assert (vecxnew->size == idim);
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (vecx);
  assert (vecx->size == idim);
  assert (MatW);
  assert (MatW->size1 == idim && MatW->size2 == idim);
  assert (vecw);
  assert (vecw->size == idim);
  assert (idim == static_cast<unsigned int>(npar + ncon));

  gsl_matrix_const_view A (gsl_matrix_const_submatrix (MatM, 0, npar, npar, ncon));
  gsl_matrix_view ATA (gsl_matrix_submatrix (MatW, npar, npar, ncon, ncon));
  gsl_matrix_view Acopy (gsl_matrix_submatrix (MatW, 0, npar, npar, ncon));
  gsl_matrix_view& V = ATA;
  
  gsl_vector_view gradf (gsl_vector_subvector (vecw, 0, npar));
  gsl_vector_view ATgradf (gsl_vector_subvector (vecw, npar, ncon));
  gsl_vector_view& s = ATgradf;

  gsl_vector_view lambdanew (gsl_vector_subvector (vecxnew, npar, ncon));

  if (debug > 5) {
    gsl_vector_const_view lambda (gsl_vector_const_subvector (vecx, npar, ncon));
    cout << "lambda: " <<endl;;
    gsl_vector_fprintf (stdout, &lambdanew.vector, "%f");
    cout << endl;
  }
  
  // ATA = 1*A^T*A + 0*ATA
  gsl_blas_dgemm (CblasTrans, CblasNoTrans, 1, &A.matrix, &A.matrix, 0, &ATA.matrix);

  // put grad(f) into vecw
  assembleChi2Der (vecw);

  
  // ATgradf = -1*A^T*gradf + 0*ATgradf
  gsl_blas_dgemv (CblasTrans, -1, &A.matrix, &gradf.vector, 0, &ATgradf.vector);
  
  if (debug > 7) {
    cout << "A: " <<endl;;
    gsl_matrix_fprintf (stdout, &A.matrix, "%f");
    cout << endl;
    cout << "ATA: " <<endl;;
    gsl_matrix_fprintf (stdout, &ATA.matrix, "%f");
    cout << endl;
    cout << "gradf: " <<endl;;
    gsl_vector_fprintf (stdout, &gradf.vector, "%f");
    cout << endl;
    cout << "ATgradf: " <<endl;;
    gsl_vector_fprintf (stdout, &ATgradf.vector, "%f");
    cout << endl;
  }
  
  // solve ATA * lambdanew = ATgradf using the Cholsky factorization method
  gsl_error_handler_t *old_handler =  gsl_set_error_handler_off ();
  int cholesky_result = gsl_linalg_cholesky_decomp (&ATA.matrix);
  gsl_set_error_handler (old_handler);
  if (cholesky_result) {
    cout << "NewFitterGSL::determineLambdas: resorting to SVD" << endl;
    // ATA is not positive definite, i.e. A does not have full column rank
    // => use the SVD of A to solve A lambdanew = gradf
    
    // Acopy = A
    gsl_matrix_memcpy (&Acopy.matrix, &A.matrix);
    
    // SVD decomposition of Acopy
    gsl_linalg_SV_decomp_jacobi (&Acopy.matrix, &V.matrix, &s.vector);
    // set small values to zero
    double mins = eps*std::fabs (gsl_vector_get (&s.vector, 0));
    for (int i = 0; i < ncon; ++i) {
      if (std::fabs (gsl_vector_get (&s.vector, i)) <= mins) 
        gsl_vector_set (&s.vector, i, 0);
    }
    gsl_linalg_SV_solve (&Acopy.matrix, &V.matrix, &s.vector, &gradf.vector, &lambdanew.vector);
  }
  else {
    gsl_linalg_cholesky_solve (&ATA.matrix, &ATgradf.vector, &lambdanew.vector);
  }
  if (debug > 5) {
    cout << "lambdanew: " <<endl;;
    gsl_vector_fprintf (stdout, &lambdanew.vector, "%f");
    cout << endl;
  }
}

void NewFitterGSL::MoorePenroseInverse (gsl_matrix *Ainv, gsl_matrix *A, 
                                        gsl_matrix *W, gsl_vector *w,
                                        double eps    
                                       ) {
  assert (Ainv);
  assert (A);
  assert (Ainv->size1 == A->size2 && Ainv->size2 == A->size1);
  assert (A->size1 >= A->size2);
  assert (W);
  assert (W->size1 >= A->size1 && W->size2 >= A->size2);
  assert (w);
  assert (w->size >= A->size2);
  
  int n = A->size1;
  int m = A->size2;
  
  // Original A -> A diag(w) W^T
  gsl_linalg_SV_decomp_jacobi (A, W, w);
  
  double mins = eps*std::fabs (gsl_vector_get (w, 0));
  
  // Compute pseudo-inverse of diag(w)
  for (int i = 0; i < m; ++i) {
    double singval = gsl_vector_get (w, i);
    if (std::fabs (singval) > mins)
      gsl_vector_set (w, i, 1/singval);
    else
      gsl_vector_set (w, i, 0);
  }
  
  // Compute Ainv = W diag(w) A^T
  
  // first: Ainv = W* diag(w)
  for (int j = 0; j < n; ++j) {
    double wval = gsl_vector_get (w, j);
    for (int i = 0; i < m; ++i)
      gsl_matrix_set (W, i, j, wval*gsl_matrix_get (W, i, j));
  }
  // Ainv = 1*W*A^T + 0*Ainv
  gsl_blas_dgemm (CblasNoTrans, CblasTrans, 1, W, A, 0, Ainv);
  
}

double NewFitterGSL::calcpTLp (const gsl_vector *vecdx, const gsl_matrix *MatM,   
                               gsl_vector *vecw) {
  assert (vecdx);
  assert (vecdx->size == idim);
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (vecw);
  assert (vecw->size == idim);
  
  gsl_vector_const_view p (gsl_vector_const_subvector (vecdx, 0, npar));
  gsl_vector_view Lp (gsl_vector_subvector (vecw, 0, npar));
  gsl_matrix_const_view L (gsl_matrix_const_submatrix (MatM, 0, 0, npar, npar));
  gsl_blas_dsymv (CblasUpper, 1, &L.matrix, &p.vector, 0, &Lp.vector);
  double result;
  gsl_blas_ddot (&p.vector, &Lp.vector, &result);

  return result;
}

void NewFitterGSL::calc2ndOrderCorr (gsl_vector *vecdxhat, 
                                     const gsl_vector *vecxnew, 
                                     const gsl_matrix *MatM,   
                                     gsl_matrix *MatW,   
                                     gsl_vector *vecw,
                                     double eps) {  
  assert (vecdxhat);
  assert (vecdxhat->size == idim);
  assert (vecxnew);
  assert (vecxnew->size == idim);
  assert (MatM);
  assert (MatM->size1 == idim && MatM->size2 == idim);
  assert (MatW);
  assert (MatW->size1 == idim && MatW->size2 == idim);
  assert (vecw);
  assert (vecw->size == idim);
  assert (idim == static_cast<unsigned int>(npar + ncon));


  // Calculate 2nd order correction 
  // see Nocedal&Wright (15.36)
  gsl_matrix_const_view AT     (gsl_matrix_const_submatrix (MatM, 0,    npar, npar, ncon));
  gsl_matrix_view AAT          (gsl_matrix_submatrix       (MatW, npar, npar, ncon, ncon));
  gsl_matrix_view ATcopy       (gsl_matrix_submatrix       (MatW, 0,    npar, npar, ncon));
  gsl_matrix_view& V = AAT;

  gsl_vector_set_zero (vecdxhat);
  addConstraints (vecdxhat);
  gsl_vector_view c (gsl_vector_subvector (vecdxhat, npar, ncon));
  gsl_vector_view phat = (gsl_vector_subvector (vecdxhat, 0, npar));
                                       
  gsl_vector_set_zero (vecw);
  gsl_vector_view AATinvc (gsl_vector_subvector (vecw, npar, ncon));
  gsl_vector_view& s = AATinvc;
  
  
  // AAT = 1*A*A^T + 0*AAT
  gsl_blas_dgemm (CblasTrans, CblasNoTrans, 1, &AT.matrix, &AT.matrix, 0, &AAT.matrix);
  
  // solve AAT * AATinvc = c using the Cholsky factorization method
  gsl_error_handler_t *old_handler =  gsl_set_error_handler_off ();
  int cholesky_result = gsl_linalg_cholesky_decomp (&AAT.matrix);
  gsl_set_error_handler (old_handler);
  if (cholesky_result) {
    cout << "NewFitterGSL::calc2ndOrderCorr: resorting to SVD" << endl;
    // AAT is not positive definite, i.e. A does not have full column rank
    // => use the SVD of AT to solve A lambdanew = gradf
    
    // ATcopy = AT
    gsl_matrix_memcpy (&ATcopy.matrix, &AT.matrix);
    
    // SVD decomposition of Acopy
    gsl_linalg_SV_decomp_jacobi (&ATcopy.matrix, &V.matrix, &s.vector);
    // set small values to zero
    double mins = eps*std::fabs (gsl_vector_get (&s.vector, 0));
    for (int i = 0; i < ncon; ++i) {
      if (std::fabs (gsl_vector_get (&s.vector, i)) <= mins) 
        gsl_vector_set (&s.vector, i, 0);
    }
    gsl_linalg_SV_solve (&ATcopy.matrix, &V.matrix, &s.vector, &c.vector, &AATinvc.vector);
  }
  else {
    gsl_linalg_cholesky_solve (&AAT.matrix, &c.vector, &AATinvc.vector);
  }
  
  // phat = -1*A^T*AATinvc+ 0*phat
  gsl_blas_dgemv (CblasNoTrans, -1, &AT.matrix, &AATinvc.vector, 0, &phat.vector);
  gsl_vector_set_zero (&c.vector);
                                       
}

int NewFitterGSL::solveSystem (      gsl_vector *vecdxscal, 
                                     double& detW,
                               const gsl_vector *vecyscal, 
                               const gsl_matrix *MatMscal,  
                                     gsl_matrix *MatW,   
                                     gsl_matrix *MatW2,   
                                     gsl_vector *vecw,
                                     double epsLU,
                                     double epsSV) {  
  assert (vecdxscal);
  assert (vecdxscal->size == idim);
  assert (vecyscal);
  assert (vecyscal->size == idim);
  assert (MatMscal);
  assert (MatMscal->size1 == idim && MatMscal->size2 == idim);
  assert (MatW);
  assert (MatW->size1 == idim && MatW->size2 == idim);
  assert (MatW2);
  assert (MatW2->size1 == idim && MatW2->size2 == idim);
  assert (vecw);
  assert (vecw->size == idim);
  
  int result = 0;
  
  int iLU = solveSystemLU (vecdxscal, detW, vecyscal, MatMscal, MatW, vecw, epsLU);
  if (iLU == 0) return result;
  
  result = 1;
  int iSVD = solveSystemSVD (vecdxscal, detW, vecyscal, MatMscal, MatW, MatW2, vecw, epsSV);
  if (iSVD == 0) return result;
  
  return -1;
}

int NewFitterGSL::solveSystemLU (      gsl_vector *vecdxscal, 
                                       double&     detW,
                                 const gsl_vector *vecyscal,
                                 const gsl_matrix *MatMscal,
                                       gsl_matrix *MatW,   
                                       gsl_vector *vecw,
                                       double eps) {  
  assert (vecdxscal);
  assert (vecdxscal->size == idim);
  assert (vecyscal);
  assert (vecyscal->size == idim);
  assert (MatMscal);
  assert (MatMscal->size1 == idim && MatMscal->size2 == idim);
  assert (MatW);
  assert (MatW->size1 == idim && MatW->size2 == idim);
  assert (vecw);
  assert (vecw->size == idim);
  
  gsl_matrix_memcpy (MatW, MatMscal);
  
  int ifail = 0;
  detW = 0;
  
  int signum;
  int result = gsl_linalg_LU_decomp (MatW, permW, &signum);
  if (debug>4)cout << "NewFitterGSL::solveSystem: gsl_linalg_LU_decomp result=" << result << endl;
  if (result != 0) return 1;
  
  detW = gsl_linalg_LU_det (MatW, signum);
  if (debug>4)cout << "NewFitterGSL::solveSystem: determinant of W=" << detW << endl;
  if (std::fabs(detW) < eps) return 2;
  if (!std::isfinite(detW)) {
    if (debug>0)cout << "NewFitterGSL::solveSystem: infinite determinant of W=" << detW << endl;
    return 3;
  }
  if (debug>5) {
    cout << "NewFitterGSL::solveSystem: after LU decomposition: \n";
    debug_print (MatW, "W");
  }
  // Solve W*dxscal = yscal
  ifail = gsl_linalg_LU_solve (MatW, permW, vecyscal, vecdxscal);
  if (debug>4)cout << "NewFitterGSL::solveSystem: gsl_linalg_LU_solve result=" << ifail << endl;
  
  if (ifail != 0) {
    cerr << "NewFitterGSL::solveSystem: ifail from gsl_linalg_LU_solve=" << ifail << endl;
    return 3;
  }
  return 0;
}


  

int NewFitterGSL::solveSystemSVD (      gsl_vector *vecdxscal, 
                                         double& detW,
                                   const gsl_vector *vecyscal, 
                                   const gsl_matrix *MatMscal,  
                                         gsl_matrix *MatW,   
                                         gsl_matrix *MatW2,   
                                         gsl_vector *vecw,
                                         double eps) {  
  assert (vecdxscal);
  assert (vecdxscal->size == idim);
  assert (vecyscal);
  assert (vecyscal->size == idim);
  assert (MatMscal);
  assert (MatMscal->size1 == idim && MatMscal->size2 == idim);
  assert (MatW);
  assert (MatW->size1 == idim && MatW->size2 == idim);
  assert (MatW2);
  assert (MatW2->size1 == idim && MatW2->size2 == idim);
  assert (vecw);
  assert (vecw->size == idim);
  
  if (debug>0) cout << "solveSystemSVD called" << endl;
  
  gsl_matrix_memcpy (MatW, MatMscal);
    
  // SVD decomposition of MatW
  gsl_linalg_SV_decomp_jacobi (MatW, MatW2, vecw);
  // set small values to zero
  double mins = eps*std::fabs (gsl_vector_get (vecw, 0));
  if (debug>5) cout << "SV 0 = " << gsl_vector_get (vecw, 0) << endl;
  for (unsigned int i = 0; i < idim; ++i) {
    if (std::fabs (gsl_vector_get (vecw, i)) <= mins) {
      if (debug>5) cout << "Setting SV" << i << " = " << gsl_vector_get (vecw, i) << " to zero!" << endl;
      gsl_vector_set (vecw, i, 0);
    }
  }
  gsl_linalg_SV_solve (MatW, MatW2, vecw, vecyscal, vecdxscal);
  return 0;
}  

gsl_matrix_view NewFitterGSL::calcZ (int& rankA, gsl_matrix *MatW1,  gsl_matrix *MatW2, 
                                     gsl_vector *vecw1, gsl_vector *vecw2, 
                                     gsl_permutation *permW, double eps) {
  assert (MatW1);
  assert (MatW1->size1 == idim && MatW1->size2 == idim);
  assert (MatW2);
  assert (MatW2->size1 == idim && MatW2->size2 == idim);
  assert (vecw1);
  assert (vecw1->size == idim);
  assert (vecw2);
  assert (vecw2->size == idim);
  assert (permW);
  assert (permW->size == idim);
  
  // fill A and AT
  assembleConstDer (MatW2);                    
                          
  gsl_matrix_const_view AT (gsl_matrix_const_submatrix (MatW2, 0,    0,    npar, npar));
  //gsl_matrix_view QR       (gsl_matrix_submatrix       (MatW2, 0,    0,    npar, npar));
  gsl_matrix_view Q        (gsl_matrix_submatrix       (MatW1, 0,    0,    npar, npar));
  gsl_matrix_view R        (gsl_matrix_submatrix       (MatW1, npar, 0,    ncon, npar));

  int signum = 0;
  //gsl_linalg_QRPT_decomp   (&QR.matrix, vecw1, permW, &signum, vecw2);
  //gsl_linalg_QR_unpack     (&QR.matrix, vecw1, &Q.matrix, &R.matrix);
  gsl_linalg_QRPT_decomp2 (&AT.matrix, &Q.matrix, &R.matrix, vecw1, permW, &signum, vecw2); 

  rankA = 0;
  for (int i = 0; i < ncon; ++i) {
    if (fabs(gsl_matrix_get (&R.matrix, i, i)) > eps) rankA++;
  }
  gsl_matrix_view result = gsl_matrix_view (gsl_matrix_submatrix       (MatW1, 0, rankA, ncon-rankA, npar));
  return result;
}

gsl_matrix_view NewFitterGSL::calcReducedHessian (int& rankH, gsl_matrix *MatW1,  
                                                  const gsl_vector *vecx, gsl_matrix *MatW2, 
                                                  gsl_matrix *MatW3, 
                                                  gsl_vector *vecw1, gsl_vector *vecw2, 
                                                  gsl_permutation *permW, double eps) {
  assert (MatW1);
  assert (MatW1->size1 == idim && MatW1->size2 == idim);
  assert (vecx);
  assert (vecx->size == idim);
  assert (MatW2);
  assert (MatW2->size1 == idim && MatW2->size2 == idim);
  assert (MatW3);
  assert (MatW2->size1 == idim && MatW2->size2 == idim);
  assert (vecw1);
  assert (vecw1->size == idim);
  assert (vecw2);
  assert (vecw2->size == idim);
  assert (permW);
  assert (permW->size == idim);
  
  int rankA;
  // Z is a matrix view of MatW2!
  gsl_matrix_view Z = calcZ (rankA, MatW2, MatW1, vecw1, vecw2, permW, eps);
  
  // fill Lagrangian
  assembleG (MatW1, vecx);      
  
  rankH =  npar-rankA;            
                          
  gsl_matrix_view G        (gsl_matrix_submatrix       (MatW1, 0,    0,    npar,  npar));
  gsl_matrix_view GZ       (gsl_matrix_submatrix       (MatW3, 0,    0,    npar,  rankH));
  gsl_matrix_view ZGZ      (gsl_matrix_submatrix       (MatW1, 0,    0,    rankH, rankH));

  // Calculate Z^T G Z
  
  // GZ = 1*G*Z + 0*GZ
  gsl_blas_dsymm (CblasLeft, CblasUpper, 1, &G.matrix, &Z.matrix, 0, &GZ.matrix);
  // ZGZ = 1*Z^T*GZ + 0*ZGZ
  gsl_blas_dgemm (CblasTrans, CblasNoTrans, 1, &Z.matrix, &GZ.matrix, 0, &ZGZ.matrix);

  return ZGZ;
}

gsl_vector_view NewFitterGSL::calcReducedHessianEigenvalues (int& rankH, gsl_matrix *MatW1,  
                                                  const gsl_vector *vecx, gsl_matrix *MatW2, 
                                                  gsl_matrix *MatW3, 
                                                  gsl_vector *vecw1, gsl_vector *vecw2, 
                                                  gsl_permutation *permW, gsl_eigen_symm_workspace *eigenws, 
                                                  double eps) {
  assert (MatW1);
  assert (MatW1->size1 == idim && MatW1->size2 == idim);
  assert (vecx);
  assert (vecx->size == idim);
  assert (MatW2);
  assert (MatW2->size1 == idim && MatW2->size2 == idim);
  assert (MatW3);
  assert (MatW2->size1 == idim && MatW2->size2 == idim);
  assert (vecw1);
  assert (vecw1->size == idim);
  assert (vecw2);
  assert (vecw2->size == idim);
  assert (permW);
  assert (permW->size == idim);
  assert (eigenws);
  
  gsl_matrix_view Hred = calcReducedHessian (rankH, MatW1, vecx, MatW2, MatW3, vecw1, vecw2, permW, eps);

  gsl_matrix_view Hredcopy (gsl_matrix_submatrix       (MatW3, 0,    0,    rankH, rankH));
  // copy Hred -> Hredcopy
  gsl_matrix_memcpy (&Hredcopy.matrix, &Hred.matrix);
    
  gsl_vector_view eval (gsl_vector_subvector (vecw1, 0, rankH));
  gsl_eigen_symm (&Hredcopy.matrix, &eval.vector, eigenws);
  
  return eval;
}
