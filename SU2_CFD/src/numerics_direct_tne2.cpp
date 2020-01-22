﻿/*!
 * \file numerics_direct_tne2.cpp
 * \brief This file contains the numerical methods for compressible flow.
 * \author F. Palacios, T. Economon
 * \version 6.1.0 "Falcon"
 *
 * The current SU2 release has been coordinated by the
 * SU2 International Developers Society <www.su2devsociety.org>
 * with selected contributions from the open-source community.
 *
 * The main research teams contributing to the current release are:
 *  - Prof. Juan J. Alonso's group at Stanford University.
 *  - Prof. Piero Colonna's group at Delft University of Technology.
 *  - Prof. Nicolas R. Gauger's group at Kaiserslautern University of Technology.
 *  - Prof. Alberto Guardone's group at Polytechnic University of Milan.
 *  - Prof. Rafael Palacios' group at Imperial College London.
 *  - Prof. Vincent Terrapon's group at the University of Liege.
 *  - Prof. Edwin van der Weide's group at the University of Twente.
 *  - Lab. of New Concepts in Aeronautics at Tech. Institute of Aeronautics.
 *
 * Copyright 2012-2018, Francisco D. Palacios, Thomas D. Economon,
 *                      Tim Albring, and the SU2 contributors.
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */
#include "../include/numerics_structure.hpp"
#include "../include/variables/CTNE2EulerVariable.hpp"
#include <limits>

CUpwRoe_TNE2::CUpwRoe_TNE2(unsigned short val_nDim, unsigned short val_nVar,
                           unsigned short val_nPrimVar,
                           unsigned short val_nPrimVarGrad,
                           CConfig *config) : CNumerics(val_nDim, val_nVar,
                                                        config) {

  unsigned short iVar;

  /*--- Read configuration parameters ---*/
  implicit   = (config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT);
  ionization = config->GetIonization();

  /*--- Define useful constants ---*/
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();

  /*--- Allocate arrays ---*/
  Diff_U      = new su2double  [nVar];
  RoeU        = new su2double  [nVar];
  RoeV        = new su2double  [nPrimVar];
  RoedPdU     = new su2double  [nVar];
  RoeEve      = new su2double  [nSpecies];
  Lambda      = new su2double  [nVar];
  Epsilon     = new su2double  [nVar];
  P_Tensor    = new su2double* [nVar];
  invP_Tensor = new su2double* [nVar];

  for (iVar = 0; iVar < nVar; iVar++) {
    P_Tensor[iVar]    = new su2double [nVar];
    invP_Tensor[iVar] = new su2double [nVar];
  }

  ProjFlux_i = new su2double [nVar];
  ProjFlux_j = new su2double [nVar];

}

CUpwRoe_TNE2::~CUpwRoe_TNE2(void) {

  unsigned short iVar;

  delete [] Diff_U;
  delete [] RoeU;
  delete [] RoeV;
  delete [] RoedPdU;
  delete [] RoeEve;
  delete [] Lambda;
  delete [] Epsilon;

  for (iVar = 0; iVar < nVar; iVar++) {
    delete [] P_Tensor[iVar];
    delete [] invP_Tensor[iVar];
  }

  delete [] P_Tensor;
  delete [] invP_Tensor;
  delete [] ProjFlux_i;
  delete [] ProjFlux_j;
}

void CUpwRoe_TNE2::ComputeResidual(su2double *val_residual,
                                   su2double **val_Jacobian_i,
                                   su2double **val_Jacobian_j,
                                   CConfig *config) {

  unsigned short iDim, iSpecies, iVar, jVar, kVar;

  /*--- Face area (norm or the normal vector) ---*/
  Area = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  /*--- Unit Normal ---*/
  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  /*--- Calculate Roe variables ---*/
  R    = sqrt(abs(V_j[RHO_INDEX]/V_i[RHO_INDEX]));

  for (iVar = 0; iVar < nVar; iVar++)
    RoeU[iVar] = (R*U_j[iVar] + U_i[iVar])/(R+1);

  for (iVar = 0; iVar < nPrimVar; iVar++)
    RoeV[iVar] = (R*V_j[iVar] + V_i[iVar])/(R+1);

  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    RoeEve[iSpecies] = var->CalcEve(config, RoeV[TVE_INDEX], iSpecies);

  /*--- Calculate derivatives of pressure ---*/
  var->CalcdPdU(RoeV, RoeEve, config, RoedPdU);

  /*--- Calculate dual grid tangent vectors for P & invP ---*/
  CreateBasis(UnitNormal);

  /*--- Compute the inviscid projected fluxes ---*/
  GetInviscidProjFlux(U_i, V_i, Normal, ProjFlux_i);
  GetInviscidProjFlux(U_j, V_j, Normal, ProjFlux_j);

  /*--- Compute projected P, invP, and Lambda ---*/
  GetPMatrix(RoeU, RoeV, RoedPdU, UnitNormal, l, m, P_Tensor);
  GetPMatrix_inv(RoeU, RoeV, RoedPdU, UnitNormal, l, m, invP_Tensor);

  /*--- Compute projected velocities ---*/
  ProjVelocity = 0.0; ProjVelocity_i = 0.0; ProjVelocity_j = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    ProjVelocity   += RoeV[VEL_INDEX+iDim] * UnitNormal[iDim];
    ProjVelocity_i += V_i[VEL_INDEX+iDim]  * UnitNormal[iDim];
    ProjVelocity_j += V_j[VEL_INDEX+iDim]  * UnitNormal[iDim];
  }

  RoeSoundSpeed = sqrt((1.0+RoedPdU[nSpecies+nDim])*
      RoeV[P_INDEX]/RoeV[RHO_INDEX]);

  /*--- Calculate eigenvalues ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    Lambda[iSpecies] = ProjVelocity;

  for (iDim = 0; iDim < nDim-1; iDim++)
    Lambda[nSpecies+iDim] = ProjVelocity;

  Lambda[nSpecies+nDim-1] = ProjVelocity + RoeSoundSpeed;
  Lambda[nSpecies+nDim]   = ProjVelocity - RoeSoundSpeed;
  Lambda[nSpecies+nDim+1] = ProjVelocity;

  /*--- Harten and Hyman (1983) entropy correction ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    Epsilon[iSpecies] = 4.0*max(0.0, max(Lambda[iDim]-ProjVelocity_i,
                                         ProjVelocity_j-Lambda[iDim] ));
  for (iDim = 0; iDim < nDim-1; iDim++)
    Epsilon[nSpecies+iDim] = 4.0*max(0.0, max(Lambda[iDim]-ProjVelocity_i,
                                              ProjVelocity_j-Lambda[iDim] ));
  Epsilon[nSpecies+nDim-1] = 4.0*max(0.0, max(Lambda[nSpecies+nDim-1]-(ProjVelocity_i+V_i[A_INDEX]),
                                     (ProjVelocity_j+V_j[A_INDEX])-Lambda[nSpecies+nDim-1]));
  Epsilon[nSpecies+nDim]   = 4.0*max(0.0, max(Lambda[nSpecies+nDim]-(ProjVelocity_i-V_i[A_INDEX]),
                                     (ProjVelocity_j-V_j[A_INDEX])-Lambda[nSpecies+nDim]));
  Epsilon[nSpecies+nDim+1] = 4.0*max(0.0, max(Lambda[iDim]-ProjVelocity_i,
                                              ProjVelocity_j-Lambda[iDim] ));
  for (iVar = 0; iVar < nVar; iVar++)
    if ( fabs(Lambda[iVar]) < Epsilon[iVar] )
      Lambda[iVar] = (Lambda[iVar]*Lambda[iVar] + Epsilon[iVar]*Epsilon[iVar])/(2.0*Epsilon[iVar]);
    else
      Lambda[iVar] = fabs(Lambda[iVar]);

  for (iVar = 0; iVar < nVar; iVar++)
    Lambda[iVar] = fabs(Lambda[iVar]);

  /*--- Calculate inviscid projected Jacobians ---*/
  // Note: Scaling value is 0.5 because inviscid flux is based on 0.5*(Fc_i+Fc_j)
  if (implicit){
    GetInviscidProjJac(U_i, V_i, dPdU_i, Normal, 0.5, val_Jacobian_i);
    GetInviscidProjJac(U_j, V_j, dPdU_j, Normal, 0.5, val_Jacobian_j);
  }

  /*--- Difference of conserved variables at iPoint and jPoint ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    Diff_U[iVar] = U_j[iVar]-U_i[iVar];

  /*--- Roe's Flux approximation ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_residual[iVar] = 0.5 * (ProjFlux_i[iVar] + ProjFlux_j[iVar]);
    for (jVar = 0; jVar < nVar; jVar++) {

      /*--- Compute |Proj_ModJac_Tensor| = P x |Lambda| x inverse P ---*/
      Proj_ModJac_Tensor_ij = 0.0;
      for (kVar = 0; kVar < nVar; kVar++)
        Proj_ModJac_Tensor_ij += P_Tensor[iVar][kVar]*Lambda[kVar]*invP_Tensor[kVar][jVar];

      val_residual[iVar] -= 0.5*Proj_ModJac_Tensor_ij*Diff_U[jVar]*Area;
      if (implicit){
        val_Jacobian_i[iVar][jVar] += 0.5*Proj_ModJac_Tensor_ij*Area;
        val_Jacobian_j[iVar][jVar] -= 0.5*Proj_ModJac_Tensor_ij*Area;
      }
    }
  }

  //AD::SetPreaccOut(val_residual, nVar);
  //AD::EndPreacc();
}

CUpwMSW_TNE2::CUpwMSW_TNE2(unsigned short val_nDim,
                           unsigned short val_nVar,
                           unsigned short val_nPrimVar,
                           unsigned short val_nPrimVarGrad,
                           CConfig *config) : CNumerics(val_nDim,
                                                        val_nVar,
                                                        config) {

  /*--- Set booleans from CConfig settings ---*/
  ionization = config->GetIonization();
  implicit = (config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT);

  /*--- Set iterator size ---*/
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();

  /*--- Allocate arrays ---*/
  Diff_U   = new su2double [nVar];
  Fc_i	   = new su2double [nVar];
  Fc_j	   = new su2double [nVar];
  Lambda_i = new su2double [nVar];
  Lambda_j = new su2double [nVar];

  rhos_i   = new su2double [nSpecies];
  rhos_j   = new su2double [nSpecies];
  rhosst_i = new su2double [nSpecies];
  rhosst_j = new su2double [nSpecies];
  u_i		   = new su2double [nDim];
  u_j		   = new su2double [nDim];
  ust_i    = new su2double [nDim];
  ust_j    = new su2double [nDim];
  Vst_i    = new su2double [nPrimVar];
  Vst_j    = new su2double [nPrimVar];
  Ust_i    = new su2double [nVar];
  Ust_j    = new su2double [nVar];
  Evest_i  = new su2double [nSpecies];
  Evest_j  = new su2double [nSpecies];
  dPdUst_i = new su2double [nVar];
  dPdUst_j = new su2double [nVar];

  P_Tensor		= new su2double* [nVar];
  invP_Tensor	= new su2double* [nVar];
  for (unsigned short iVar = 0; iVar < nVar; iVar++) {
    P_Tensor[iVar]    = new su2double [nVar];
    invP_Tensor[iVar] = new su2double [nVar];
  }
}

CUpwMSW_TNE2::~CUpwMSW_TNE2(void) {

  delete [] Diff_U;
  delete [] Fc_i;
  delete [] Fc_j;
  delete [] Lambda_i;
  delete [] Lambda_j;

  delete [] rhos_i;
  delete [] rhos_j;
  delete [] rhosst_i;
  delete [] rhosst_j;
  delete [] u_i;
  delete [] u_j;
  delete [] ust_i;
  delete [] ust_j;
  delete [] Ust_i;
  delete [] Vst_i;
  delete [] Ust_j;
  delete [] Vst_j;
  delete [] Evest_i;
  delete [] Evest_j;
  delete [] dPdUst_i;
  delete [] dPdUst_j;

  for (unsigned short iVar = 0; iVar < nVar; iVar++) {
    delete [] P_Tensor[iVar];
    delete [] invP_Tensor[iVar];
  }
  delete [] P_Tensor;
  delete [] invP_Tensor;

}

void CUpwMSW_TNE2::ComputeResidual(su2double *val_residual,
                                   su2double **val_Jacobian_i,
                                   su2double **val_Jacobian_j, CConfig *config) {

  unsigned short iDim, iSpecies, iVar, jVar, kVar;
  su2double P_i, P_j;
  su2double ProjVel_i, ProjVel_j, ProjVelst_i, ProjVelst_j;
  su2double sqvel_i, sqvel_j;
  su2double epsilon, alpha, w, dp, onemw;
  su2double Proj_ModJac_Tensor_i, Proj_ModJac_Tensor_j;

  /*--- Set parameters in the numerical method ---*/
  alpha   = 5.0;
  epsilon = 0.0;
  /*--- Calculate supporting geometry parameters ---*/
  Area = 0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  /*--- Initialize flux & Jacobian vectors ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    Fc_i[iVar] = 0.0;
    Fc_j[iVar] = 0.0;
  }
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[iVar][jVar] = 0.0;
        val_Jacobian_j[iVar][jVar] = 0.0;
      }
    }
  }

  /*--- Load variables from nodes i & j ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    rhos_i[iSpecies] = V_i[RHOS_INDEX+iSpecies];
    rhos_j[iSpecies] = V_j[RHOS_INDEX+iSpecies];
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    u_i[iDim] = V_i[VEL_INDEX+iDim];
    u_j[iDim] = V_j[VEL_INDEX+iDim];
  }
  P_i = V_i[P_INDEX];
  P_j = V_j[P_INDEX];

  /*--- Calculate supporting quantities ---*/
  sqvel_i   = 0.0;  sqvel_j   = 0.0;
  ProjVel_i = 0.0;  ProjVel_j = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    sqvel_i   += u_i[iDim]*u_i[iDim];
    sqvel_j   += u_j[iDim]*u_j[iDim];
    ProjVel_i += u_i[iDim]*UnitNormal[iDim];
    ProjVel_j += u_j[iDim]*UnitNormal[iDim];
  }

  /*--- Calculate the state weighting function ---*/
  dp = fabs(P_j-P_i) / min(P_j,P_i);
  w = 0.5 * (1.0/(pow(alpha*dp,2.0) +1.0));
  onemw = 1.0 - w;

  /*--- Calculate weighted state vector (*) for i & j ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    Ust_i[iVar] = onemw*U_i[iVar] + w*U_j[iVar];
    Ust_j[iVar] = onemw*U_j[iVar] + w*U_i[iVar];
  }
  for (iVar = 0; iVar < nPrimVar; iVar++) {
    Vst_i[iVar] = onemw*V_i[iVar] + w*V_j[iVar];
    Vst_j[iVar] = onemw*V_j[iVar] + w*V_i[iVar];
  }
  ProjVelst_i = onemw*ProjVel_i + w*ProjVel_j;
  ProjVelst_j = onemw*ProjVel_j + w*ProjVel_i;

  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Evest_i[iSpecies] = var->CalcEve(config, Vst_i[TVE_INDEX], iSpecies);
    Evest_j[iSpecies] = var->CalcEve(config, Vst_j[TVE_INDEX], iSpecies);
  }
  var->CalcdPdU(Vst_i, Evest_i, config, dPdUst_i);
  var->CalcdPdU(Vst_j, Evest_j, config, dPdUst_j);

  /*--- Flow eigenvalues at i (Lambda+) ---*/
  for (iSpecies = 0; iSpecies < nSpecies+nDim-1; iSpecies++)
    Lambda_i[iSpecies]      = 0.5*(ProjVelst_i + sqrt(ProjVelst_i*ProjVelst_i +
                                                      epsilon*epsilon));
  Lambda_i[nSpecies+nDim-1] = 0.5*(ProjVelst_i + Vst_i[A_INDEX] +
                                   sqrt((ProjVelst_i + Vst_i[A_INDEX])*
                                        (ProjVelst_i + Vst_i[A_INDEX])+
                                        epsilon*epsilon)                );
  Lambda_i[nSpecies+nDim]   = 0.5*(ProjVelst_i - Vst_i[A_INDEX] +
                                   sqrt((ProjVelst_i - Vst_i[A_INDEX])*
                                        (ProjVelst_i - Vst_i[A_INDEX]) +
                                        epsilon*epsilon)                );
  Lambda_i[nSpecies+nDim+1] = 0.5*(ProjVelst_i + sqrt(ProjVelst_i*ProjVelst_i +
                                                      epsilon*epsilon));

  /*--- Compute projected P, invP, and Lambda ---*/
  GetPMatrix    (Ust_i, Vst_i, dPdU_i, UnitNormal, l, m, P_Tensor   );
  GetPMatrix_inv(Ust_i, Vst_i, dPdU_i, UnitNormal, l, m, invP_Tensor);

  /*--- Projected flux (f+) at i ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      Proj_ModJac_Tensor_i = 0.0;

      /*--- Compute Proj_ModJac_Tensor = P x Lambda+ x inverse P ---*/
      for (kVar = 0; kVar < nVar; kVar++)
        Proj_ModJac_Tensor_i += P_Tensor[iVar][kVar]*Lambda_i[kVar]*invP_Tensor[kVar][jVar];
      Fc_i[iVar] += Proj_ModJac_Tensor_i*U_i[jVar]*Area;
      if (implicit)
        val_Jacobian_i[iVar][jVar] += Proj_ModJac_Tensor_i*Area;
    }
  }

  /*--- Flow eigenvalues at j (Lambda-) ---*/
  for (iVar = 0; iVar < nSpecies+nDim-1; iVar++)
    Lambda_j[iVar]          = 0.5*(ProjVelst_j - sqrt(ProjVelst_j*ProjVelst_j +
                                                      epsilon*epsilon));
  Lambda_j[nSpecies+nDim-1] = 0.5*(ProjVelst_j + Vst_j[A_INDEX] -
                                   sqrt((ProjVelst_j + Vst_j[A_INDEX])*
                                        (ProjVelst_j + Vst_j[A_INDEX])+
                                        epsilon*epsilon)                 );
  Lambda_j[nSpecies+nDim]   = 0.5*(ProjVelst_j - Vst_j[A_INDEX] -
                                   sqrt((ProjVelst_j - Vst_j[A_INDEX])*
                                        (ProjVelst_j - Vst_j[A_INDEX])+
                                        epsilon*epsilon)                 );
  Lambda_j[nSpecies+nDim+1] = 0.5*(ProjVelst_j - sqrt(ProjVelst_j*ProjVelst_j+
                                                      epsilon*epsilon));

  /*--- Compute projected P, invP, and Lambda ---*/
  GetPMatrix(Ust_j, Vst_j, dPdU_j, UnitNormal, l, m, P_Tensor);
  GetPMatrix_inv(Ust_j, Vst_j, dPdU_j, UnitNormal, l, m, invP_Tensor);

  /*--- Projected flux (f-) ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      Proj_ModJac_Tensor_j = 0.0;

      /*--- Compute Proj_ModJac_Tensor = P x Lambda- x inverse P ---*/
      for (kVar = 0; kVar < nVar; kVar++)
        Proj_ModJac_Tensor_j += P_Tensor[iVar][kVar]*Lambda_j[kVar]*invP_Tensor[kVar][jVar];
      Fc_j[iVar] += Proj_ModJac_Tensor_j*U_j[jVar]*Area;
      if (implicit)
        val_Jacobian_j[iVar][jVar] += Proj_ModJac_Tensor_j*Area;
    }
  }

  /*--- Flux splitting ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_residual[iVar] = Fc_i[iVar]+Fc_j[iVar];
  }
}

CUpwAUSM_TNE2::CUpwAUSM_TNE2(unsigned short val_nDim, unsigned short val_nVar,
                             CConfig *config) : CNumerics(val_nDim, val_nVar,
                                                          config) {

  /*--- Read configuration parameters ---*/
  implicit   = (config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT);
  ionization = config->GetIonization();

  /*--- Define useful constants ---*/
  nVar     = val_nVar;
  nDim     = val_nDim;
  nSpecies = config->GetnSpecies();

  FcL    = new su2double [nVar];
  FcR    = new su2double [nVar];
  dmLP   = new su2double [nVar];
  dmRM   = new su2double [nVar];
  dpLP   = new su2double [nVar];
  dpRM   = new su2double [nVar];
  daL    = new su2double [nVar];
  daR    = new su2double [nVar];
  rhos_i = new su2double [nSpecies];
  rhos_j = new su2double [nSpecies];
  u_i    = new su2double [nDim];
  u_j    = new su2double [nDim];
}

CUpwAUSM_TNE2::~CUpwAUSM_TNE2(void) {
  delete [] FcL;
  delete [] FcR;
  delete [] dmLP;
  delete [] dmRM;
  delete [] dpLP;
  delete [] dpRM;
  delete [] rhos_i;
  delete [] rhos_j;
  delete [] u_i;
  delete [] u_j;
}

void CUpwAUSM_TNE2::ComputeResidual(su2double *val_residual,
                                    su2double **val_Jacobian_i,
                                    su2double **val_Jacobian_j,
                                    CConfig *config         ) {

  unsigned short iDim, iVar, jVar, iSpecies, nHeavy, nEl;
  su2double rho_i, rho_j, rhoCvtr_i, rhoCvtr_j, rhoCvve_i, rhoCvve_j;
  su2double Cvtrs;
  su2double RuSI, Ru, rho_el_i, rho_el_j, *Ms, *xi;
  su2double e_ve_i, e_ve_j;
  su2double mL, mR, mLP, mRM, mF, pLP, pRM, pF, Phi;

  /*--- Compute geometric quantities ---*/
  Area = 0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  /*--- Read from config ---*/
  Ms   = config->GetMolar_Mass();
  xi   = config->GetRotationModes();
  RuSI = UNIVERSAL_GAS_CONSTANT;
  Ru   = 1000.0*RuSI;

  /*--- Determine the number of heavy particle species ---*/
  if (ionization) {
    nHeavy = nSpecies-1;
    nEl = 1;
    rho_el_i = V_i[nSpecies-1];
    rho_el_j = V_j[nSpecies-1];
  } else {
    nHeavy = nSpecies;
    nEl = 0;
    rho_el_i = 0.0;
    rho_el_j = 0.0;
  }

  /*--- Pull stored primitive variables ---*/
  // Primitives: [rho1,...,rhoNs, T, Tve, u, v, w, P, rho, h, a, c]
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    rhos_i[iSpecies] = V_i[RHOS_INDEX+iSpecies];
    rhos_j[iSpecies] = V_j[RHOS_INDEX+iSpecies];
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    u_i[iDim] = V_i[VEL_INDEX+iDim];
    u_j[iDim] = V_j[VEL_INDEX+iDim];
  }

  P_i       = V_i[P_INDEX];
  P_j       = V_j[P_INDEX];
  h_i       = V_i[H_INDEX];
  h_j       = V_j[H_INDEX];
  a_i       = V_i[A_INDEX];
  a_j       = V_j[A_INDEX];
  rho_i     = V_i[RHO_INDEX];
  rho_j     = V_j[RHO_INDEX];
  e_ve_i    = U_i[nSpecies+nDim+1] / rho_i;
  e_ve_j    = U_j[nSpecies+nDim+1] / rho_j;
  rhoCvtr_i = V_i[RHOCVTR_INDEX];
  rhoCvtr_j = V_j[RHOCVTR_INDEX];
  rhoCvve_i = V_i[RHOCVVE_INDEX];
  rhoCvve_j = V_j[RHOCVVE_INDEX];

  /*--- Projected velocities ---*/
  ProjVel_i = 0.0; ProjVel_j = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    ProjVel_i += u_i[iDim]*UnitNormal[iDim];
    ProjVel_j += u_j[iDim]*UnitNormal[iDim];
  }

  /*--- Calculate L/R Mach numbers ---*/
  mL = ProjVel_i/a_i;
  mR = ProjVel_j/a_j;

  /*--- Calculate split numerical fluxes ---*/
  if (fabs(mL) <= 1.0) mLP = 0.25*(mL+1.0)*(mL+1.0);
  else                 mLP = 0.5*(mL+fabs(mL));

  if (fabs(mR) <= 1.0) mRM = -0.25*(mR-1.0)*(mR-1.0);
  else                 mRM = 0.5*(mR-fabs(mR));

  mF = mLP + mRM;

  if (fabs(mL) <= 1.0) pLP = 0.25*P_i*(mL+1.0)*(mL+1.0)*(2.0-mL);
  else                 pLP = 0.5*P_i*(mL+fabs(mL))/mL;

  if (fabs(mR) <= 1.0) pRM = 0.25*P_j*(mR-1.0)*(mR-1.0)*(2.0+mR);
  else                 pRM = 0.5*P_j*(mR-fabs(mR))/mR;

  pF = pLP + pRM;
  Phi = fabs(mF);

  /*--- Assign left & right convective vectors ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    FcL[iSpecies] = rhos_i[iSpecies]*a_i;
    FcR[iSpecies] = rhos_j[iSpecies]*a_j;
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    FcL[nSpecies+iDim] = rho_i*a_i*u_i[iDim];
    FcR[nSpecies+iDim] = rho_j*a_j*u_j[iDim];
  }
  FcL[nSpecies+nDim]   = rho_i*a_i*h_i;
  FcR[nSpecies+nDim]   = rho_j*a_j*h_j;
  FcL[nSpecies+nDim+1] = rho_i*a_i*e_ve_i;
  FcR[nSpecies+nDim+1] = rho_j*a_j*e_ve_j;

  /*--- Compute numerical flux ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    val_residual[iVar] = 0.5*((mF+Phi)*FcL[iVar]+(mF-Phi)*FcR[iVar])*Area;

  for (iDim = 0; iDim < nDim; iDim++)
    val_residual[nSpecies+iDim] += pF*UnitNormal[iDim]*Area;

  if (implicit) {

    /*--- Initialize the Jacobians ---*/
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[iVar][jVar] = 0.0;
        val_Jacobian_j[iVar][jVar] = 0.0;
      }
    }

    if (mF >= 0.0) FcLR = FcL;
    else           FcLR = FcR;

    /*--- Sound speed derivatives: Species density ---*/
    for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
      Cvtrs = (3.0/2.0+xi[iSpecies]/2.0)*Ru/Ms[iSpecies];
      daL[iSpecies] = 1.0/(2.0*a_i) * (1/rhoCvtr_i*(Ru/Ms[iSpecies] - Cvtrs*dPdU_i[nSpecies+nDim])*P_i/rho_i
          + 1.0/rho_i*(1.0+dPdU_i[nSpecies+nDim])*(dPdU_i[iSpecies] - P_i/rho_i));
      daR[iSpecies] = 1.0/(2.0*a_j) * (1/rhoCvtr_j*(Ru/Ms[iSpecies] - Cvtrs*dPdU_j[nSpecies+nDim])*P_j/rho_j
          + 1.0/rho_j*(1.0+dPdU_j[nSpecies+nDim])*(dPdU_j[iSpecies] - P_j/rho_j));
    }
    for (iSpecies = 0; iSpecies < nEl; iSpecies++) {
      daL[nSpecies-1] = 1.0/(2.0*a_i*rho_i) * (1+dPdU_i[nSpecies+nDim])*(dPdU_i[nSpecies-1] - P_i/rho_i);
      daR[nSpecies-1] = 1.0/(2.0*a_j*rho_j) * (1+dPdU_j[nSpecies+nDim])*(dPdU_j[nSpecies-1] - P_j/rho_j);
    }

    /*--- Sound speed derivatives: Momentum ---*/
    for (iDim = 0; iDim < nDim; iDim++) {
      daL[nSpecies+iDim] = -1.0/(2.0*rho_i*a_i) * ((1.0+dPdU_i[nSpecies+nDim])*dPdU_i[nSpecies+nDim])*u_i[iDim];
      daR[nSpecies+iDim] = -1.0/(2.0*rho_j*a_j) * ((1.0+dPdU_j[nSpecies+nDim])*dPdU_j[nSpecies+nDim])*u_j[iDim];
    }

    /*--- Sound speed derivatives: Energy ---*/
    daL[nSpecies+nDim]   = 1.0/(2.0*rho_i*a_i) * ((1.0+dPdU_i[nSpecies+nDim])*dPdU_i[nSpecies+nDim]);
    daR[nSpecies+nDim]   = 1.0/(2.0*rho_j*a_j) * ((1.0+dPdU_j[nSpecies+nDim])*dPdU_j[nSpecies+nDim]);

    /*--- Sound speed derivatives: Vib-el energy ---*/
    daL[nSpecies+nDim+1] = 1.0/(2.0*rho_i*a_i) * ((1.0+dPdU_i[nSpecies+nDim])*dPdU_i[nSpecies+nDim+1]);
    daR[nSpecies+nDim+1] = 1.0/(2.0*rho_j*a_j) * ((1.0+dPdU_j[nSpecies+nDim])*dPdU_j[nSpecies+nDim+1]);

    /*--- Left state Jacobian ---*/
    if (mF >= 0) {

      /*--- Jacobian contribution: dFc terms ---*/
      for (iVar = 0; iVar < nSpecies+nDim; iVar++) {
        for (jVar = 0; jVar < nVar; jVar++) {
          val_Jacobian_i[iVar][jVar] += mF * FcL[iVar]/a_i * daL[jVar];
        }
        val_Jacobian_i[iVar][iVar] += mF * a_i;
      }
      for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
        val_Jacobian_i[nSpecies+nDim][iSpecies] += mF * (dPdU_i[iSpecies]*a_i + rho_i*h_i*daL[iSpecies]);
      }
      for (iDim = 0; iDim < nDim; iDim++) {
        val_Jacobian_i[nSpecies+nDim][nSpecies+iDim] += mF * (-dPdU_i[nSpecies+nDim]*u_i[iDim]*a_i + rho_i*h_i*daL[nSpecies+iDim]);
      }
      val_Jacobian_i[nSpecies+nDim][nSpecies+nDim]   += mF * ((1.0+dPdU_i[nSpecies+nDim])*a_i + rho_i*h_i*daL[nSpecies+nDim]);
      val_Jacobian_i[nSpecies+nDim][nSpecies+nDim+1] += mF * (dPdU_i[nSpecies+nDim+1]*a_i + rho_i*h_i*daL[nSpecies+nDim+1]);
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[nSpecies+nDim+1][jVar] +=  mF * FcL[nSpecies+nDim+1]/a_i * daL[jVar];
      }
      val_Jacobian_i[nSpecies+nDim+1][nSpecies+nDim+1] += mF * a_i;
    }

    /*--- Calculate derivatives of the split pressure flux ---*/
    if ( (mF >= 0) || ((mF < 0)&&(fabs(mF) <= 1.0)) ) {
      if (fabs(mL) <= 1.0) {

        /*--- Mach number ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dmLP[iSpecies] = 0.5*(mL+1.0) * (-ProjVel_i/(rho_i*a_i) - ProjVel_i*daL[iSpecies]/(a_i*a_i));
        for (iDim = 0; iDim < nDim; iDim++)
          dmLP[nSpecies+iDim] = 0.5*(mL+1.0) * (-ProjVel_i/(a_i*a_i) * daL[nSpecies+iDim] + UnitNormal[iDim]/(rho_i*a_i));
        dmLP[nSpecies+nDim]   = 0.5*(mL+1.0) * (-ProjVel_i/(a_i*a_i) * daL[nSpecies+nDim]);
        dmLP[nSpecies+nDim+1] = 0.5*(mL+1.0) * (-ProjVel_i/(a_i*a_i) * daL[nSpecies+nDim+1]);

        /*--- Pressure ---*/
        for(iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dpLP[iSpecies] = 0.25*(mL+1.0) * (dPdU_i[iSpecies]*(mL+1.0)*(2.0-mL)
                                            + P_i*(-ProjVel_i/(rho_i*a_i)
                                                   -ProjVel_i*daL[iSpecies]/(a_i*a_i))*(3.0-3.0*mL));
        for (iDim = 0; iDim < nDim; iDim++)
          dpLP[nSpecies+iDim] = 0.25*(mL+1.0) * (-u_i[iDim]*dPdU_i[nSpecies+nDim]*(mL+1.0)*(2.0-mL)
              + P_i*( -ProjVel_i/(a_i*a_i) * daL[nSpecies+iDim]
              + UnitNormal[iDim]/(rho_i*a_i))*(3.0-3.0*mL));
        dpLP[nSpecies+nDim]   = 0.25*(mL+1.0) * (dPdU_i[nSpecies+nDim]*(mL+1.0)*(2.0-mL)
            + P_i*(-ProjVel_i/(a_i*a_i) * daL[nSpecies+nDim])*(3.0-3.0*mL));
        dpLP[nSpecies+nDim+1] = 0.25*(mL+1.0) * (dPdU_i[nSpecies+nDim+1]*(mL+1.0)*(2.0-mL)
            + P_i*(-ProjVel_i/(a_i*a_i) * daL[nSpecies+nDim+1])*(3.0-3.0*mL));
      } else {

        /*--- Mach number ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dmLP[iSpecies]      = -ProjVel_i/(rho_i*a_i) - ProjVel_i*daL[iSpecies]/(a_i*a_i);
        for (iDim = 0; iDim < nDim; iDim++)
          dmLP[nSpecies+iDim] = -ProjVel_i/(a_i*a_i) * daL[nSpecies+iDim] + UnitNormal[iDim]/(rho_i*a_i);
        dmLP[nSpecies+nDim]   = -ProjVel_i/(a_i*a_i) * daL[nSpecies+nDim];
        dmLP[nSpecies+nDim+1] = -ProjVel_i/(a_i*a_i) * daL[nSpecies+nDim+1];

        /*--- Pressure ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dpLP[iSpecies] = dPdU_i[iSpecies];
        for (iDim = 0; iDim < nDim; iDim++)
          dpLP[nSpecies+iDim] = (-u_i[iDim]*dPdU_i[nSpecies+nDim]);
        dpLP[nSpecies+nDim]   = dPdU_i[nSpecies+nDim];
        dpLP[nSpecies+nDim+1] = dPdU_i[nSpecies+nDim+1];
      }

      /*--- dM contribution ---*/
      for (iVar = 0; iVar < nVar; iVar++) {
        for (jVar = 0; jVar < nVar; jVar++) {
          val_Jacobian_i[iVar][jVar] += dmLP[jVar]*FcLR[iVar];
        }
      }

      /*--- Jacobian contribution: dP terms ---*/
      for (iDim = 0; iDim < nDim; iDim++) {
        for (iVar = 0; iVar < nVar; iVar++) {
          val_Jacobian_i[nSpecies+iDim][iVar] += dpLP[iVar]*UnitNormal[iDim];
        }
      }
    }

    /*--- Right state Jacobian ---*/
    if (mF < 0) {

      /*--- Jacobian contribution: dFc terms ---*/
      for (iVar = 0; iVar < nSpecies+nDim; iVar++) {
        for (jVar = 0; jVar < nVar; jVar++) {
          val_Jacobian_j[iVar][jVar] += mF * FcR[iVar]/a_j * daR[jVar];
        }
        val_Jacobian_j[iVar][iVar] += mF * a_j;
      }
      for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
        val_Jacobian_j[nSpecies+nDim][iSpecies] += mF * (dPdU_j[iSpecies]*a_j + rho_j*h_j*daR[iSpecies]);
      }
      for (iDim = 0; iDim < nDim; iDim++) {
        val_Jacobian_j[nSpecies+nDim][nSpecies+iDim] += mF * (-dPdU_j[nSpecies+nDim]*u_j[iDim]*a_j + rho_j*h_j*daR[nSpecies+iDim]);
      }
      val_Jacobian_j[nSpecies+nDim][nSpecies+nDim]   += mF * ((1.0+dPdU_j[nSpecies+nDim])*a_j + rho_j*h_j*daR[nSpecies+nDim]);
      val_Jacobian_j[nSpecies+nDim][nSpecies+nDim+1] += mF * (dPdU_j[nSpecies+nDim+1]*a_j + rho_j*h_j*daR[nSpecies+nDim+1]);
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_j[nSpecies+nDim+1][jVar] +=  mF * FcR[nSpecies+nDim+1]/a_j * daR[jVar];
      }
      val_Jacobian_j[nSpecies+nDim+1][nSpecies+nDim+1] += mF * a_j;
    }

    /*--- Calculate derivatives of the split pressure flux ---*/
    if ( (mF < 0) || ((mF >= 0)&&(fabs(mF) <= 1.0)) ) {
      if (fabs(mR) <= 1.0) {

        /*--- Mach ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dmRM[iSpecies] = -0.5*(mR-1.0) * (-ProjVel_j/(rho_j*a_j) - ProjVel_j*daR[iSpecies]/(a_j*a_j));
        for (iDim = 0; iDim < nDim; iDim++)
          dmRM[nSpecies+iDim] = -0.5*(mR-1.0) * (-ProjVel_j/(a_j*a_j) * daR[nSpecies+iDim] + UnitNormal[iDim]/(rho_j*a_j));
        dmRM[nSpecies+nDim]   = -0.5*(mR-1.0) * (-ProjVel_j/(a_j*a_j) * daR[nSpecies+nDim]);
        dmRM[nSpecies+nDim+1] = -0.5*(mR-1.0) * (-ProjVel_j/(a_j*a_j) * daR[nSpecies+nDim+1]);

        /*--- Pressure ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dpRM[iSpecies] = 0.25*(mR-1.0) * (dPdU_j[iSpecies]*(mR-1.0)*(2.0+mR)
                                            + P_j*(-ProjVel_j/(rho_j*a_j)
                                                   -ProjVel_j*daR[iSpecies]/(a_j*a_j))*(3.0+3.0*mR));
        for (iDim = 0; iDim < nDim; iDim++)
          dpRM[nSpecies+iDim] = 0.25*(mR-1.0) * ((-u_j[iDim]*dPdU_j[nSpecies+nDim])*(mR-1.0)*(2.0+mR)
              + P_j*( -ProjVel_j/(a_j*a_j) * daR[nSpecies+iDim]
              + UnitNormal[iDim]/(rho_j*a_j))*(3.0+3.0*mR));
        dpRM[nSpecies+nDim]   = 0.25*(mR-1.0) * (dPdU_j[nSpecies+nDim]*(mR-1.0)*(2.0+mR)
            + P_j*(-ProjVel_j/(a_j*a_j)*daR[nSpecies+nDim])*(3.0+3.0*mR));
        dpRM[nSpecies+nDim+1] = 0.25*(mR-1.0) * (dPdU_j[nSpecies+nDim+1]*(mR-1.0)*(2.0+mR)
            + P_j*(-ProjVel_j/(a_j*a_j) * daR[nSpecies+nDim+1])*(3.0+3.0*mR));

      } else {

        /*--- Mach ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dmRM[iSpecies]      = -ProjVel_j/(rho_j*a_j) - ProjVel_j*daR[iSpecies]/(a_j*a_j);
        for (iDim = 0; iDim < nDim; iDim++)
          dmRM[nSpecies+iDim] = -ProjVel_j/(a_j*a_j) * daR[nSpecies+iDim] + UnitNormal[iDim]/(rho_j*a_j);
        dmRM[nSpecies+nDim]   = -ProjVel_j/(a_j*a_j) * daR[nSpecies+nDim];
        dmRM[nSpecies+nDim+1] = -ProjVel_j/(a_j*a_j) * daR[nSpecies+nDim+1];

        /*--- Pressure ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dpRM[iSpecies] = dPdU_j[iSpecies];
        for (iDim = 0; iDim < nDim; iDim++)
          dpRM[nSpecies+iDim] = -u_j[iDim]*dPdU_j[nSpecies+nDim];
        dpRM[nSpecies+nDim]   = dPdU_j[nSpecies+nDim];
        dpRM[nSpecies+nDim+1] = dPdU_j[nSpecies+nDim+1];
      }

      /*--- Jacobian contribution: dM terms ---*/
      for (iVar = 0; iVar < nVar; iVar++) {
        for (jVar = 0; jVar < nVar; jVar++) {
          val_Jacobian_j[iVar][jVar] += dmRM[jVar] * FcLR[iVar];
        }
      }

      /*--- Jacobian contribution: dP terms ---*/
      for (iDim = 0; iDim < nDim; iDim++) {
        for (iVar = 0; iVar < nVar; iVar++) {
          val_Jacobian_j[nSpecies+iDim][iVar] += dpRM[iVar]*UnitNormal[iDim];
        }
      }
    }

    /*--- Integrate over dual-face area ---*/
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[iVar][jVar] *= Area;
        val_Jacobian_j[iVar][jVar] *= Area;
      }
    }
  }
}

CUpwAUSMPLUSUP2_TNE2::CUpwAUSMPLUSUP2_TNE2(unsigned short val_nDim, unsigned short val_nVar,
                                           unsigned short val_nPrimVar,
                                           unsigned short val_nPrimVarGrad,
                                           CConfig *config): CNumerics (val_nDim, val_nVar, config){


  unsigned short iVar;

  /*--- Read configuration parameters ---*/
  implicit   = (config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT);
  ionization = config->GetIonization();

  /*--- Define useful constants ---*/
  Kp       = 0.25;
  sigma    = 1.0;
  nVar     = val_nVar;
  nPrimVar = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  nDim     = val_nDim;
  nSpecies = config->GetnSpecies();

  /*--- Allocate data structures ---*/
  FcL    = new su2double [nVar];
  FcR    = new su2double [nVar];
  dmLP   = new su2double [nVar];
  dmRM   = new su2double [nVar];
  dpLP   = new su2double [nVar];
  dpRM   = new su2double [nVar];
  daL    = new su2double [nVar];
  daR    = new su2double [nVar];
  rhos_i = new su2double [nSpecies];
  rhos_j = new su2double [nSpecies];
  u_i    = new su2double [nDim];
  u_j    = new su2double [nDim];

  /*--- Allocate arrays ---*/
  Diff_U      = new su2double [nVar];
  RoeU        = new su2double[nVar];
  RoeV        = new su2double[nPrimVar];
  RoedPdU     = new su2double [nVar];
  RoeEve      = new su2double [nSpecies];
  Lambda      = new su2double [nVar];
  Epsilon     = new su2double [nVar];
  P_Tensor    = new su2double* [nVar];
  invP_Tensor = new su2double* [nVar];
  for (iVar = 0; iVar < nVar; iVar++) {
    P_Tensor[iVar] = new su2double [nVar];
    invP_Tensor[iVar] = new su2double [nVar];
  }

}

CUpwAUSMPLUSUP2_TNE2::~CUpwAUSMPLUSUP2_TNE2(void) {

  delete [] FcL;
  delete [] FcR;
  delete [] dmLP;
  delete [] dmRM;
  delete [] dpLP;
  delete [] dpRM;
  delete [] rhos_i;
  delete [] rhos_j;
  delete [] u_i;
  delete [] u_j;
  unsigned short iVar;

  delete [] Diff_U;
  delete [] RoeU;
  delete [] RoeV;
  delete [] RoedPdU;
  delete [] RoeEve;
  delete [] Lambda;
  delete [] Epsilon;
  for (iVar = 0; iVar < nVar; iVar++) {
    delete [] P_Tensor[iVar];
    delete [] invP_Tensor[iVar];
  }
  delete [] P_Tensor;
  delete [] invP_Tensor;
}

void CUpwAUSMPLUSUP2_TNE2::ComputeResidual(su2double *val_residual, su2double **val_Jacobian_i, su2double **val_Jacobian_j, CConfig *config) {

  unsigned short iDim, iVar, jVar, kVar, iSpecies, nHeavy, nEl;
  su2double rho_i, rho_j, rhoCvtr_i, rhoCvtr_j, rhoCvve_i, rhoCvve_j;
  su2double Cvtrs;
  su2double RuSI, Ru, rho_el_i, rho_el_j, *Ms, *xi;
  su2double e_ve_i, e_ve_j;
  su2double mL, mR, mLP, mRM, mF, pLP, pRM, pF, Phi;
  su2double sq_veli, sq_velj;

  /*--- Face area ---*/
  Area = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  /*-- Unit Normal ---*/
  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  /*--- Read from config ---*/
  Ms    = config->GetMolar_Mass();
  xi    = config->GetRotationModes();
  RuSI  = UNIVERSAL_GAS_CONSTANT;
  Ru    = 1000.0*RuSI;
  Minf  = config->GetMach();
  Gamma = config->GetGamma();

  /*--- Determine the number of heavy particle species ---*/
  if (ionization) {
    nHeavy = nSpecies-1;
    nEl = 1;
    rho_el_i = V_i[nSpecies-1];
    rho_el_j = V_j[nSpecies-1];
  } else {
    nHeavy = nSpecies;
    nEl = 0;
    rho_el_i = 0.0;
    rho_el_j = 0.0;
  }

  /*--- Extracting primitive variables ---*/
  // Primitives: [rho1,...,rhoNs, T, Tve, u, v, w, P, rho, h, a, c]
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++){
    rhos_i[iSpecies] = V_i[RHOS_INDEX+iSpecies];
    rhos_j[iSpecies] = V_j[RHOS_INDEX+iSpecies];
  }

  sq_veli = 0.0; sq_velj = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    u_i[iDim] = V_i[VEL_INDEX+iDim];
    u_j[iDim] = V_j[VEL_INDEX+iDim];
    sq_veli   += u_i[iDim]*u_i[iDim];
    sq_velj   += u_j[iDim]*u_j[iDim];
  }

  P_i       = V_i[P_INDEX];
  P_j       = V_j[P_INDEX];
  h_i       = V_i[H_INDEX];
  h_j       = V_j[H_INDEX];
  a_i       = V_i[A_INDEX];
  a_j       = V_j[A_INDEX];
  rho_i     = V_i[RHO_INDEX];
  rho_j     = V_j[RHO_INDEX];
  e_ve_i    = U_i[nSpecies+nDim+1] / rho_i;
  e_ve_j    = U_j[nSpecies+nDim+1] / rho_j;
  rhoCvtr_i = V_i[RHOCVTR_INDEX];
  rhoCvtr_j = V_j[RHOCVTR_INDEX];
  rhoCvve_i = V_i[RHOCVVE_INDEX];
  rhoCvve_j = V_j[RHOCVVE_INDEX];

  /*--- Projected velocities ---*/
  ProjVel_i = 0.0; ProjVel_j = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    ProjVel_i += u_i[iDim]*UnitNormal[iDim];
    ProjVel_j += u_j[iDim]*UnitNormal[iDim];
  }

  /*--- Compute C*  ---*/
  CstarL = sqrt(2.0*(Gamma-1.0)/(Gamma+1.0)*h_i);
  CstarR = sqrt(2.0*(Gamma-1.0)/(Gamma+1.0)*h_j);

  /*--- Compute C^ ---*/
  ChatL = CstarL*CstarL/max(CstarL,ProjVel_i);
  ChatR = CstarR*CstarR/max(CstarR,-ProjVel_j);

  /*--- Interface speed of sound ---*/
  aF = min(ChatL,ChatR);

  mL  = ProjVel_i/aF;
  mR  = ProjVel_j/aF;

  rhoF = 0.5*(rho_i+rho_j);
  MFsq = 0.5*(mL*mL+mR*mR);

  param1 = max(MFsq, Minf*Minf);
  Mrefsq = (min(1.0, param1));
  fa = 2.0*sqrt(Mrefsq)-Mrefsq;

  alpha = 3.0/16.0*(-4.0+5.0*fa*fa);
  beta = 1.0/8.0;

  /*--- Pressure diffusion term ---*/
  Mp = -(Kp/fa)*max((1.0-sigma*MFsq),0.0)*(P_j-P_i)/(rhoF*aF*aF);

  if (fabs(mL) <= 1.0) mLP = 0.25*(mL+1.0)*(mL+1.0)+beta*(mL*mL-1.0)*(mL*mL-1.0);
  else                 mLP = 0.5*(mL+fabs(mL));

  if (fabs(mR) <= 1.0) mRM = -0.25*(mR-1.0)*(mR-1.0)-beta*(mR*mR-1.0)*(mR*mR-1.0);
  else                 mRM = 0.5*(mR-fabs(mR));

  mF = mLP + mRM + Mp;

  if (fabs(mL) <= 1.0) pLP = (0.25*(mL+1.0)*(mL+1.0)*(2.0-mL)+alpha*mL*(mL*mL-1.0)*(mL*mL-1.0));
  else                 pLP = 0.5*(mL+fabs(mL))/mL;

  if (fabs(mR) <= 1.0) pRM = (0.25*(mR-1.0)*(mR-1.0)*(2.0+mR)-alpha*mR*(mR*mR-1.0)*(mR*mR-1.0));
  else                 pRM = 0.5*(mR-fabs(mR))/mR;

  /*... Modified pressure flux ...*/
  //Use this definition
  pFi = sqrt(0.5*(sq_veli+sq_velj))*(pLP+pRM-1.0)*0.5*(rho_j+rho_i)*aF;
  pF  = 0.5*(P_j+P_i)+0.5*(pLP-pRM)*(P_i-P_j)+pFi;

  Phi = fabs(mF);

  mfP=0.5*(mF+Phi);
  mfM=0.5*(mF-Phi);

  /*--- Assign left & right covective fluxes ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    FcL[iSpecies] = rhos_i[iSpecies]*aF;
    FcR[iSpecies] = rhos_j[iSpecies]*aF;
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    FcL[nSpecies+iDim] = rho_i*aF*u_i[iDim];
    FcR[nSpecies+iDim] = rho_j*aF*u_j[iDim];
  }
  FcL[nSpecies+nDim]   = rho_i*aF*h_i;
  FcR[nSpecies+nDim]   = rho_j*aF*h_j;
  FcL[nSpecies+nDim+1] = rho_i*aF*e_ve_i;
  FcR[nSpecies+nDim+1] = rho_j*aF*e_ve_j;

  /*--- Compute numerical flux ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    val_residual[iVar] = (mfP*FcL[iVar]+mfM*FcR[iVar])*Area;

  for (iDim = 0; iDim < nDim; iDim++)
    val_residual[nSpecies+iDim] += pF*UnitNormal[iDim]*Area;

  /*--- Roe's Jacobian -> checking if there is an improvement over TNE2 AUSM Jacobian---*/
  //if (implicit){
  //
  //  /*--- Compute Roe Variables ---*/
  //  R    = sqrt(abs(V_j[RHO_INDEX]/V_i[RHO_INDEX]));
  //  for (iVar = 0; iVar < nVar; iVar++)
  //    RoeU[iVar] = (R*U_j[iVar] + U_i[iVar])/(R+1);
  //  for (iVar = 0; iVar < nPrimVar; iVar++)
  //    RoeV[iVar] = (R*V_j[iVar] + V_i[iVar])/(R+1);

  //  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
  //    RoeEve[iSpecies] = var->CalcEve(config, RoeV[TVE_INDEX], iSpecies);

  /*--- Calculate derivatives of pressure ---*/
  //    var->CalcdPdU(RoeV, RoeEve, config, RoedPdU);

  /*--- Calculate dual grid tangent vectors for P & invP ---*/
  //    CreateBasis(UnitNormal);

  /*--- Compute projected P, invP, and Lambda ---*/
  //  GetPMatrix(RoeU, RoeV, RoedPdU, UnitNormal, l, m, P_Tensor);
  //  GetPMatrix_inv(RoeU, RoeV, RoedPdU, UnitNormal, l, m, invP_Tensor);

  //  RoeSoundSpeed = sqrt((1.0+RoedPdU[nSpecies+nDim])*
  //      RoeV[P_INDEX]/RoeV[RHO_INDEX]);

  /*--- Compute projected velocities ---*/
  //  ProjVelocity = 0.0; ProjVelocity_i = 0.0; ProjVelocity_j = 0.0;
  //  for (iDim = 0; iDim < nDim; iDim++) {
  //    ProjVelocity   += RoeV[VEL_INDEX+iDim] * UnitNormal[iDim];
  //    ProjVelocity_i += V_i[VEL_INDEX+iDim]  * UnitNormal[iDim];
  //    ProjVelocity_j += V_j[VEL_INDEX+iDim]  * UnitNormal[iDim];
  //  }

  /*--- Calculate eigenvalues ---*/
  //  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
  //    Lambda[iSpecies] = ProjVelocity;
  //  for (iDim = 0; iDim < nDim-1; iDim++)
  //    Lambda[nSpecies+iDim] = ProjVelocity;
  //  Lambda[nSpecies+nDim-1] = ProjVelocity + RoeSoundSpeed;
  //  Lambda[nSpecies+nDim]   = ProjVelocity - RoeSoundSpeed;
  //  Lambda[nSpecies+nDim+1] = ProjVelocity;

  /*--- Harten and Hyman (1983) entropy correction ---*/
  //  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
  //    Epsilon[iSpecies] = 4.0*max(0.0, max(Lambda[iDim]-ProjVelocity_i,
  //                                         ProjVelocity_j-Lambda[iDim] ));
  //  for (iDim = 0; iDim < nDim-1; iDim++)
  //    Epsilon[nSpecies+iDim] = 4.0*max(0.0, max(Lambda[iDim]-ProjVelocity_i,
  //                                              ProjVelocity_j-Lambda[iDim] ));
  //  Epsilon[nSpecies+nDim-1] = 4.0*max(0.0, max(Lambda[nSpecies+nDim-1]-(ProjVelocity_i+V_i[A_INDEX]),
  //                                     (ProjVelocity_j+V_j[A_INDEX])-Lambda[nSpecies+nDim-1]));
  //  Epsilon[nSpecies+nDim]   = 4.0*max(0.0, max(Lambda[nSpecies+nDim]-(ProjVelocity_i-V_i[A_INDEX]),
  //                                     (ProjVelocity_j-V_j[A_INDEX])-Lambda[nSpecies+nDim]));
  //  Epsilon[nSpecies+nDim+1] = 4.0*max(0.0, max(Lambda[iDim]-ProjVelocity_i,
  //                                              ProjVelocity_j-Lambda[iDim] ));
  //  for (iVar = 0; iVar < nVar; iVar++)
  //    if ( fabs(Lambda[iVar]) < Epsilon[iVar] )
  //      Lambda[iVar] = (Lambda[iVar]*Lambda[iVar] + Epsilon[iVar]*Epsilon[iVar])/(2.0*Epsilon[iVar]);
  //    else
  //      Lambda[iVar] = fabs(Lambda[iVar]);

  //  for (iVar = 0; iVar < nVar; iVar++)
  //    Lambda[iVar] = fabs(Lambda[iVar]);

  /*--- Calculate inviscid projected Jacobians ---*/
  // Note: Scaling value is 0.5 because inviscid flux is based on 0.5*(Fc_i+Fc_j)
  //  GetInviscidProjJac(U_i, V_i, dPdU_i, Normal, 0.5, val_Jacobian_i);
  //  GetInviscidProjJac(U_j, V_j, dPdU_j, Normal, 0.5, val_Jacobian_j);

  /*--- Roe's Flux approximation ---*/
  //  for (iVar = 0; iVar < nVar; iVar++) {
  //    for (jVar = 0; jVar < nVar; jVar++) {

  /*--- Compute |Proj_ModJac_Tensor| = P x |Lambda| x inverse P ---*/
  //      Proj_ModJac_Tensor_ij = 0.0;
  //      for (kVar = 0; kVar < nVar; kVar++)
  //        Proj_ModJac_Tensor_ij += P_Tensor[iVar][kVar]*Lambda[kVar]*invP_Tensor[kVar][jVar];
  //      val_Jacobian_i[iVar][jVar] += 0.5*Proj_ModJac_Tensor_ij*Area;
  //      val_Jacobian_j[iVar][jVar] -= 0.5*Proj_ModJac_Tensor_ij*Area;
  //    }
  //  }
  //}

  /*--- AUSM's Jacobian....requires tiny CFL's (this must be fixed) ---*/
  if (implicit) {

    /*--- Initialize the Jacobians ---*/
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[iVar][jVar] = 0.0;
        val_Jacobian_j[iVar][jVar] = 0.0;
      }
    }

    if (mF >= 0.0) FcLR = FcL;
    else           FcLR = FcR;

    /*--- Sound speed derivatives: Species density ---*/
    for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
      Cvtrs = (3.0/2.0+xi[iSpecies]/2.0)*Ru/Ms[iSpecies];
      daL[iSpecies] = 1.0/(2.0*aF) * (1/rhoCvtr_i*(Ru/Ms[iSpecies] - Cvtrs*dPdU_i[nSpecies+nDim])*P_i/rho_i
          + 1.0/rho_i*(1.0+dPdU_i[nSpecies+nDim])*(dPdU_i[iSpecies] - P_i/rho_i));
      daR[iSpecies] = 1.0/(2.0*aF) * (1/rhoCvtr_j*(Ru/Ms[iSpecies] - Cvtrs*dPdU_j[nSpecies+nDim])*P_j/rho_j
          + 1.0/rho_j*(1.0+dPdU_j[nSpecies+nDim])*(dPdU_j[iSpecies] - P_j/rho_j));
    }
    for (iSpecies = 0; iSpecies < nEl; iSpecies++) {
      daL[nSpecies-1] = 1.0/(2.0*aF*rho_i) * (1+dPdU_i[nSpecies+nDim])*(dPdU_i[nSpecies-1] - P_i/rho_i);
      daR[nSpecies-1] = 1.0/(2.0*aF*rho_j) * (1+dPdU_j[nSpecies+nDim])*(dPdU_j[nSpecies-1] - P_j/rho_j);
    }

    /*--- Sound speed derivatives: Momentum ---*/
    for (iDim = 0; iDim < nDim; iDim++) {
      daL[nSpecies+iDim] = -1.0/(2.0*rho_i*aF) * ((1.0+dPdU_i[nSpecies+nDim])*dPdU_i[nSpecies+nDim])*u_i[iDim];
      daR[nSpecies+iDim] = -1.0/(2.0*rho_j*aF) * ((1.0+dPdU_j[nSpecies+nDim])*dPdU_j[nSpecies+nDim])*u_j[iDim];
    }

    /*--- Sound speed derivatives: Energy ---*/
    daL[nSpecies+nDim]   = 1.0/(2.0*rho_i*aF) * ((1.0+dPdU_i[nSpecies+nDim])*dPdU_i[nSpecies+nDim]);
    daR[nSpecies+nDim]   = 1.0/(2.0*rho_j*aF) * ((1.0+dPdU_j[nSpecies+nDim])*dPdU_j[nSpecies+nDim]);

    /*--- Sound speed derivatives: Vib-el energy ---*/
    daL[nSpecies+nDim+1] = 1.0/(2.0*rho_i*aF) * ((1.0+dPdU_i[nSpecies+nDim])*dPdU_i[nSpecies+nDim+1]);
    daR[nSpecies+nDim+1] = 1.0/(2.0*rho_j*aF) * ((1.0+dPdU_j[nSpecies+nDim])*dPdU_j[nSpecies+nDim+1]);

    /*--- Left state Jacobian ---*/
    if (mF >= 0) {

      /*--- Jacobian contribution: dFc terms ---*/
      for (iVar = 0; iVar < nSpecies+nDim; iVar++) {
        for (jVar = 0; jVar < nVar; jVar++) {
          val_Jacobian_i[iVar][jVar] += mF * FcL[iVar]/aF * daL[jVar];
        }
        val_Jacobian_i[iVar][iVar] += mF * aF;
      }
      for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
        val_Jacobian_i[nSpecies+nDim][iSpecies] += mF * (dPdU_i[iSpecies]*aF + rho_i*h_i*daL[iSpecies]);
      }
      for (iDim = 0; iDim < nDim; iDim++) {
        val_Jacobian_i[nSpecies+nDim][nSpecies+iDim] += mF * (-dPdU_i[nSpecies+nDim]*u_i[iDim]*aF + rho_i*h_i*daL[nSpecies+iDim]);
      }
      val_Jacobian_i[nSpecies+nDim][nSpecies+nDim]   += mF * ((1.0+dPdU_i[nSpecies+nDim])*aF + rho_i*h_i*daL[nSpecies+nDim]);
      val_Jacobian_i[nSpecies+nDim][nSpecies+nDim+1] += mF * (dPdU_i[nSpecies+nDim+1]*aF + rho_i*h_i*daL[nSpecies+nDim+1]);
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[nSpecies+nDim+1][jVar] +=  mF * FcL[nSpecies+nDim+1]/aF * daL[jVar];
      }
      val_Jacobian_i[nSpecies+nDim+1][nSpecies+nDim+1] += mF * aF;
    }


    /*--- Calculate derivatives of the split pressure flux ---*/
    if ( (mF >= 0) || ((mF < 0)&&(fabs(mF) <= 1.0)) ) {
      if (fabs(mL) <= 1.0) {

        /*--- Mach number ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dmLP[iSpecies] = 0.5*(mL+1.0) * (-ProjVel_i/(rho_i*aF) - ProjVel_i*daL[iSpecies]/(aF*aF));
        for (iDim = 0; iDim < nDim; iDim++)
          dmLP[nSpecies+iDim] = 0.5*(mL+1.0) * (-ProjVel_i/(aF*aF) * daL[nSpecies+iDim] + UnitNormal[iDim]/(rho_i*aF));
        dmLP[nSpecies+nDim]   = 0.5*(mL+1.0) * (-ProjVel_i/(aF*aF) * daL[nSpecies+nDim]);
        dmLP[nSpecies+nDim+1] = 0.5*(mL+1.0) * (-ProjVel_i/(aF*aF) * daL[nSpecies+nDim+1]);

        /*--- Pressure ---*/
        for(iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dpLP[iSpecies] = 0.25*(mL+1.0) * (dPdU_i[iSpecies]*(mL+1.0)*(2.0-mL)
                                            + P_i*(-ProjVel_i/(rho_i*aF)
                                                   -ProjVel_i*daL[iSpecies]/(aF*aF))*(3.0-3.0*mL));
        for (iDim = 0; iDim < nDim; iDim++)
          dpLP[nSpecies+iDim] = 0.25*(mL+1.0) * (-u_i[iDim]*dPdU_i[nSpecies+nDim]*(mL+1.0)*(2.0-mL)
              + P_i*( -ProjVel_i/(aF*aF) * daL[nSpecies+iDim]
              + UnitNormal[iDim]/(rho_i*aF))*(3.0-3.0*mL));
        dpLP[nSpecies+nDim]   = 0.25*(mL+1.0) * (dPdU_i[nSpecies+nDim]*(mL+1.0)*(2.0-mL)
            + P_i*(-ProjVel_i/(aF*aF) * daL[nSpecies+nDim])*(3.0-3.0*mL));
        dpLP[nSpecies+nDim+1] = 0.25*(mL+1.0) * (dPdU_i[nSpecies+nDim+1]*(mL+1.0)*(2.0-mL)
            + P_i*(-ProjVel_i/(aF*aF) * daL[nSpecies+nDim+1])*(3.0-3.0*mL));
      } else {

        /*--- Mach number ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dmLP[iSpecies]      = -ProjVel_i/(rho_i*aF) - ProjVel_i*daL[iSpecies]/(aF*aF);
        for (iDim = 0; iDim < nDim; iDim++)
          dmLP[nSpecies+iDim] = -ProjVel_i/(aF*aF) * daL[nSpecies+iDim] + UnitNormal[iDim]/(rho_i*aF);
        dmLP[nSpecies+nDim]   = -ProjVel_i/(aF*aF) * daL[nSpecies+nDim];
        dmLP[nSpecies+nDim+1] = -ProjVel_i/(aF*aF) * daL[nSpecies+nDim+1];

        /*--- Pressure ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dpLP[iSpecies] = dPdU_i[iSpecies];
        for (iDim = 0; iDim < nDim; iDim++)
          dpLP[nSpecies+iDim] = (-u_i[iDim]*dPdU_i[nSpecies+nDim]);
        dpLP[nSpecies+nDim]   = dPdU_i[nSpecies+nDim];
        dpLP[nSpecies+nDim+1] = dPdU_i[nSpecies+nDim+1];
      }

      /*--- dM contribution ---*/
      for (iVar = 0; iVar < nVar; iVar++) {
        for (jVar = 0; jVar < nVar; jVar++) {
          val_Jacobian_i[iVar][jVar] += dmLP[jVar]*FcLR[iVar];
        }
      }

      /*--- Jacobian contribution: dP terms ---*/
      for (iDim = 0; iDim < nDim; iDim++) {
        for (iVar = 0; iVar < nVar; iVar++) {
          val_Jacobian_i[nSpecies+iDim][iVar] += dpLP[iVar]*UnitNormal[iDim];
        }
      }
    }

    /*--- Right state Jacobian ---*/
    if (mF < 0) {

      /*--- Jacobian contribution: dFc terms ---*/
      for (iVar = 0; iVar < nSpecies+nDim; iVar++) {
        for (jVar = 0; jVar < nVar; jVar++) {
          val_Jacobian_j[iVar][jVar] += mF * FcR[iVar]/aF * daR[jVar];
        }
        val_Jacobian_j[iVar][iVar] += mF * aF;
      }
      for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
        val_Jacobian_j[nSpecies+nDim][iSpecies] += mF * (dPdU_j[iSpecies]*aF + rho_j*h_j*daR[iSpecies]);
      }
      for (iDim = 0; iDim < nDim; iDim++) {
        val_Jacobian_j[nSpecies+nDim][nSpecies+iDim] += mF * (-dPdU_j[nSpecies+nDim]*u_j[iDim]*aF + rho_j*h_j*daR[nSpecies+iDim]);
      }
      val_Jacobian_j[nSpecies+nDim][nSpecies+nDim]   += mF * ((1.0+dPdU_j[nSpecies+nDim])*aF + rho_j*h_j*daR[nSpecies+nDim]);
      val_Jacobian_j[nSpecies+nDim][nSpecies+nDim+1] += mF * (dPdU_j[nSpecies+nDim+1]*aF + rho_j*h_j*daR[nSpecies+nDim+1]);
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_j[nSpecies+nDim+1][jVar] +=  mF * FcR[nSpecies+nDim+1]/aF * daR[jVar];
      }
      val_Jacobian_j[nSpecies+nDim+1][nSpecies+nDim+1] += mF * aF;
    }

    /*--- Calculate derivatives of the split pressure flux ---*/
    if ( (mF < 0) || ((mF >= 0)&&(fabs(mF) <= 1.0)) ) {
      if (fabs(mR) <= 1.0) {

        /*--- Mach ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dmRM[iSpecies] = -0.5*(mR-1.0) * (-ProjVel_j/(rho_j*aF) - ProjVel_j*daR[iSpecies]/(aF*aF));
        for (iDim = 0; iDim < nDim; iDim++)
          dmRM[nSpecies+iDim] = -0.5*(mR-1.0) * (-ProjVel_j/(aF*aF) * daR[nSpecies+iDim] + UnitNormal[iDim]/(rho_j*aF));
        dmRM[nSpecies+nDim]   = -0.5*(mR-1.0) * (-ProjVel_j/(aF*aF) * daR[nSpecies+nDim]);
        dmRM[nSpecies+nDim+1] = -0.5*(mR-1.0) * (-ProjVel_j/(aF*aF) * daR[nSpecies+nDim+1]);

        /*--- Pressure ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dpRM[iSpecies] = 0.25*(mR-1.0) * (dPdU_j[iSpecies]*(mR-1.0)*(2.0+mR)
                                            + P_j*(-ProjVel_j/(rho_j*aF)
                                                   -ProjVel_j*daR[iSpecies]/(aF*aF))*(3.0+3.0*mR));
        for (iDim = 0; iDim < nDim; iDim++)
          dpRM[nSpecies+iDim] = 0.25*(mR-1.0) * ((-u_j[iDim]*dPdU_j[nSpecies+nDim])*(mR-1.0)*(2.0+mR)
              + P_j*( -ProjVel_j/(aF*aF) * daR[nSpecies+iDim]
              + UnitNormal[iDim]/(rho_j*aF))*(3.0+3.0*mR));
        dpRM[nSpecies+nDim]   = 0.25*(mR-1.0) * (dPdU_j[nSpecies+nDim]*(mR-1.0)*(2.0+mR)
            + P_j*(-ProjVel_j/(aF*aF)*daR[nSpecies+nDim])*(3.0+3.0*mR));
        dpRM[nSpecies+nDim+1] = 0.25*(mR-1.0) * (dPdU_j[nSpecies+nDim+1]*(mR-1.0)*(2.0+mR)
            + P_j*(-ProjVel_j/(aF*aF) * daR[nSpecies+nDim+1])*(3.0+3.0*mR));

      } else {

        /*--- Mach ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dmRM[iSpecies]      = -ProjVel_j/(rho_j*aF) - ProjVel_j*daR[iSpecies]/(aF*aF);
        for (iDim = 0; iDim < nDim; iDim++)
          dmRM[nSpecies+iDim] = -ProjVel_j/(aF*aF) * daR[nSpecies+iDim] + UnitNormal[iDim]/(rho_j*aF);
        dmRM[nSpecies+nDim]   = -ProjVel_j/(aF*aF) * daR[nSpecies+nDim];
        dmRM[nSpecies+nDim+1] = -ProjVel_j/(aF*aF) * daR[nSpecies+nDim+1];

        /*--- Pressure ---*/
        for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
          dpRM[iSpecies] = dPdU_j[iSpecies];
        for (iDim = 0; iDim < nDim; iDim++)
          dpRM[nSpecies+iDim] = -u_j[iDim]*dPdU_j[nSpecies+nDim];
        dpRM[nSpecies+nDim]   = dPdU_j[nSpecies+nDim];
        dpRM[nSpecies+nDim+1] = dPdU_j[nSpecies+nDim+1];
      }

      /*--- Jacobian contribution: dM terms ---*/
      for (iVar = 0; iVar < nVar; iVar++) {
        for (jVar = 0; jVar < nVar; jVar++) {
          val_Jacobian_j[iVar][jVar] += dmRM[jVar] * FcLR[iVar];
        }
      }

      /*--- Jacobian contribution: dP terms ---*/
      for (iDim = 0; iDim < nDim; iDim++) {
        for (iVar = 0; iVar < nVar; iVar++) {
          val_Jacobian_j[nSpecies+iDim][iVar] += dpRM[iVar]*UnitNormal[iDim];
        }
      }
    }

    /*--- Integrate over dual-face area ---*/
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[iVar][jVar] *= Area;
        val_Jacobian_j[iVar][jVar] *= Area;
      }
    }
  }
}

CUpwAUSMPWplus_TNE2::CUpwAUSMPWplus_TNE2(unsigned short val_nDim,
                                         unsigned short val_nVar,
                                         CConfig *config) : CNumerics(val_nDim,
                                                                      val_nVar,
                                                                      config) {

  /*--- Read configuration parameters ---*/
  implicit   = (config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT);
  ionization = config->GetIonization();

  /*--- Define useful constants ---*/
  nVar     = val_nVar;
  nDim     = val_nDim;
  nSpecies = config->GetnSpecies();

  FcL     = new su2double [nVar];
  FcR     = new su2double [nVar];
  dmLdL   = new su2double [nVar];
  dmLdR   = new su2double [nVar];
  dmRdL   = new su2double [nVar];
  dmRdR   = new su2double [nVar];
  dmLPdL  = new su2double [nVar];
  dmLPdR  = new su2double [nVar];
  dmRMdL  = new su2double [nVar];
  dmRMdR  = new su2double [nVar];
  dmbLPdL = new su2double [nVar];
  dmbLPdR = new su2double [nVar];
  dmbRMdL = new su2double [nVar];
  dmbRMdR = new su2double [nVar];
  dpLPdL  = new su2double [nVar];
  dpLPdR  = new su2double [nVar];
  dpRMdL  = new su2double [nVar];
  dpRMdR  = new su2double [nVar];
  dHnL    = new su2double [nVar];
  dHnR    = new su2double [nVar];
  daL     = new su2double [nVar];
  daR     = new su2double [nVar];
  rhos_i  = new su2double [nSpecies];
  rhos_j  = new su2double [nSpecies];
  u_i     = new su2double [nDim];
  u_j     = new su2double [nDim];
  dPdU_i  = new su2double [nVar];
  dPdU_j  = new su2double [nVar];
}

CUpwAUSMPWplus_TNE2::~CUpwAUSMPWplus_TNE2(void) {

  delete [] FcL;
  delete [] FcR;
  delete [] dmLdL;
  delete [] dmLdR;
  delete [] dmRdL;
  delete [] dmRdR;
  delete [] dmLPdL;
  delete [] dmLPdR;
  delete [] dmRMdL;
  delete [] dmRMdR;
  delete [] dmbLPdL;
  delete [] dmbLPdR;
  delete [] dmbRMdL;
  delete [] dmbRMdR;
  delete [] dpLPdL;
  delete [] dpLPdR;
  delete [] dpRMdL;
  delete [] dpRMdR;
  delete [] dHnL;
  delete [] dHnR;
  delete [] daL;
  delete [] daR;
  delete [] rhos_i;
  delete [] rhos_j;
  delete [] u_i;
  delete [] u_j;
  delete [] dPdU_i;
  delete [] dPdU_j;
}

void CUpwAUSMPWplus_TNE2::ComputeResidual(su2double *val_residual,
                                          su2double **val_Jacobian_i,
                                          su2double **val_Jacobian_j,
                                          CConfig *config         ) {

  // NOTE: OSCILLATOR DAMPER "f" NOT IMPLEMENTED!!!

  unsigned short iDim, jDim, iVar, jVar, iSpecies, nHeavy, nEl;
  su2double rho_i, rho_j, rhoEve_i, rhoEve_j, P_i, P_j, h_i, h_j;
  su2double rhoCvtr_i, rhoCvtr_j, rhoCvve_i, rhoCvve_j;
  su2double aij, atl, gtl_i, gtl_j, sqVi, sqVj, Hnorm;
  su2double ProjVel_i, ProjVel_j;
  su2double rhoRi, rhoRj, RuSI, Ru, rho_el_i, rho_el_j, *Ms, *xi;
  su2double w, fL, fR, alpha;
  su2double mL, mR, mLP, mRM, mF, mbLP, mbRM, pLP, pRM, ps;
  su2double fact, gam, dV2L, dV2R;

  alpha = 3.0/16.0;

  /*---- Initialize the residual vector ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    val_residual[iVar] = 0.0;

  /*--- Calculate geometric quantities ---*/
  Area = 0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  /*--- Read from config ---*/
  Ms = config->GetMolar_Mass();
  xi = config->GetRotationModes();
  RuSI = UNIVERSAL_GAS_CONSTANT;
  Ru   = 1000.0*RuSI;

  /*--- Determine the number of heavy particle species ---*/
  if (ionization) {
    nHeavy = nSpecies-1;
    nEl = 1;
    rho_el_i = V_i[nSpecies-1];
    rho_el_j = V_j[nSpecies-1];
  } else {
    nHeavy = nSpecies;
    nEl = 0;
    rho_el_i = 0.0;
    rho_el_j = 0.0;
  }

  /*--- Pull stored primitive variables ---*/
  // Primitives: [rho1,...,rhoNs, T, Tve, u, v, w, P, rho, h, c]
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    rhos_i[iSpecies] = V_i[RHOS_INDEX+iSpecies];
    rhos_j[iSpecies] = V_j[RHOS_INDEX+iSpecies];
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    u_i[iDim] = 0.0; // V_i[VEL_INDEX+iDim];
    u_j[iDim] = 0.0; // V_j[VEL_INDEX+iDim];
  }
  P_i       = 0.0; // V_i[P_INDEX];
  P_j       = 0.0; // V_j[P_INDEX];
  h_i       = V_i[H_INDEX];
  h_j       = V_j[H_INDEX];
  rho_i     = V_i[RHO_INDEX];
  rho_j     = V_j[RHO_INDEX];
  rhoEve_i  = U_i[nSpecies+nDim+1];
  rhoEve_j  = U_j[nSpecies+nDim+1];
  rhoCvtr_i = V_i[RHOCVTR_INDEX];
  rhoCvtr_j = V_j[RHOCVTR_INDEX];
  rhoCvve_i = V_i[RHOCVVE_INDEX];
  rhoCvve_j = V_j[RHOCVVE_INDEX];
  rhoRi = 0.0;
  rhoRj = 0.0;
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    rhoRi += V_i[RHOS_INDEX+iSpecies]*Ru/Ms[iSpecies];
    rhoRj += V_j[RHOS_INDEX+iSpecies]*Ru/Ms[iSpecies];
  }

  /*--- Projected velocities ---*/
  ProjVel_i = 0.0; ProjVel_j = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    ProjVel_i += u_i[iDim]*UnitNormal[iDim];
    ProjVel_j += u_j[iDim]*UnitNormal[iDim];
  }
  sqVi = 0.0;   sqVj = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    sqVi += (u_i[iDim]-ProjVel_i*UnitNormal[iDim])
        * (u_i[iDim]-ProjVel_i*UnitNormal[iDim]);
    sqVj += (u_j[iDim]-ProjVel_j*UnitNormal[iDim])
        * (u_j[iDim]-ProjVel_j*UnitNormal[iDim]);
  }

  /*--- Calculate interface numerical speed of sound ---*/
  Hnorm = 0.5*(h_i-0.5*sqVi + h_j-0.5*sqVj);
  gtl_i = rhoRi/(rhoCvtr_i+rhoCvve_i)+1;
  gtl_j = rhoRj/(rhoCvtr_j+rhoCvve_j)+1;
  gam = 0.5*(gtl_i+gtl_j);
  if (fabs(rho_i-rho_j)/(0.5*(rho_i+rho_j)) < 1E-3)
    atl = sqrt(2.0*Hnorm*(gam-1.0)/(gam+1.0));
  else {
    atl = sqrt(2.0*Hnorm * (((gtl_i-1.0)/(gtl_i*rho_i) - (gtl_j-1.0)/(gtl_j*rho_j))/
                            ((gtl_j+1.0)/(gtl_j*rho_i) - (gtl_i+1.0)/(gtl_i*rho_j))));
  }

  if (0.5*(ProjVel_i+ProjVel_j) >= 0.0) aij = atl*atl/max(fabs(ProjVel_i),atl);
  else                                  aij = atl*atl/max(fabs(ProjVel_j),atl);

  /*--- Calculate L/R Mach & Pressure functions ---*/
  mL	= ProjVel_i/aij;
  mR	= ProjVel_j/aij;
  if (fabs(mL) <= 1.0) {
    mLP = 0.25*(mL+1.0)*(mL+1.0);
    pLP = P_i*(0.25*(mL+1.0)*(mL+1.0)*(2.0-mL)+alpha*mL*(mL*mL-1.0)*(mL*mL-1.0));
  } else {
    mLP = 0.5*(mL+fabs(mL));
    pLP = P_i*0.5*(mL+fabs(mL))/mL;
  }
  if (fabs(mR) <= 1.0) {
    mRM = -0.25*(mR-1.0)*(mR-1.0);
    pRM = P_j*(0.25*(mR-1.0)*(mR-1.0)*(2.0+mR)-alpha*mR*(mR*mR-1.0)*(mR*mR-1.0));
  } else {
    mRM = 0.5*(mR-fabs(mR));
    pRM = 0.5*P_j*(mR-fabs(mR))/mR;
  }

  /*--- Calculate supporting w & f functions ---*/
  w = 1.0 - pow(min(P_i/P_j, P_j/P_i), 3.0);
  ps = pLP + pRM;
  // Modified f function:
  if (fabs(mL) < 1.0) fL = P_i/ps - 1.0;
  else fL = 0.0;
  if (fabs(mR) < 1.0) fR = P_j/ps - 1.0;
  else fR = 0.0;

  /*--- Calculate modified M functions ---*/
  mF = mLP + mRM;
  if (mF >= 0.0) {
    mbLP = mLP + mRM*((1.0-w)*(1.0+fR) - fL);
    mbRM = mRM*w*(1.0+fR);
  } else {
    mbLP = mLP*w*(1+fL);
    mbRM = mRM + mLP*((1.0-w)*(1.0+fL) + fL -fR);
  }

  /*--- Assign left & right convective vectors ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    FcL[iSpecies] = rhos_i[iSpecies];
    FcR[iSpecies] = rhos_j[iSpecies];
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    FcL[nSpecies+iDim] = rho_i*u_i[iDim];
    FcR[nSpecies+iDim] = rho_j*u_j[iDim];
  }
  FcL[nSpecies+nDim]   = rho_i*h_i;
  FcR[nSpecies+nDim]   = rho_j*h_j;
  FcL[nSpecies+nDim+1] = rhoEve_i;
  FcR[nSpecies+nDim+1] = rhoEve_j;

  /*--- Calculate the numerical flux ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    val_residual[iVar] = (mbLP*aij*FcL[iVar] + mbRM*aij*FcR[iVar])*Area;
  for (iDim = 0; iDim < nDim; iDim++)
    val_residual[nSpecies+iDim] += (pLP*UnitNormal[iDim] + pRM*UnitNormal[iDim])*Area;

  if (implicit) {

    /*--- Initialize the Jacobians ---*/
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[iVar][jVar] = 0.0;
        val_Jacobian_j[iVar][jVar] = 0.0;
      }
    }

    /*--- Derivatives of the interface speed of sound, aij ---*/
    // Derivatives of Hnorm
    //fact = 0.5*sqrt(2*(gam-1.0)/((gam+1.0)*Hnorm));
    //for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
    //  dHnL[iSpecies] = 0.5*(dPdU_i[iSpecies] /*+ sqVi/rho_i*/);
    //  dHnR[iSpecies] = 0.5*(dPdU_j[iSpecies] /*+ sqVj/rho_j*/);
    //}
    //for (iDim = 0; iDim < nDim; iDim++) {
    //	dV2L = 0.0;
    //  dV2R = 0.0;
    //  for (jDim = 0; jDim < nDim; jDim++) {
    //    dV2L += 2.0/rho_i*(u_i[jDim]-ProjVel_i*UnitNormal[jDim]*(-UnitNormal[iDim]*UnitNormal[jDim]));
    //    dV2R += 2.0/rho_j*(u_j[jDim]-ProjVel_j*UnitNormal[jDim]*(-UnitNormal[iDim]*UnitNormal[jDim]));
    //  }
    //  dV2L += 2.0/rho_i*(u_i[iDim]-ProjVel_i*UnitNormal[iDim] - sqVi);
    //  dV2R += 2.0/rho_j*(u_j[iDim]-ProjVel_j*UnitNormal[iDim] - sqVj);
    //  dHnL[nSpecies+iDim] = 0.5*(dPdU_i[nSpecies+iDim] /*- 0.5*(dV2L)*/);
    //  dHnR[nSpecies+iDim] = 0.5*(dPdU_j[nSpecies+iDim] /*- 0.5*(dV2R)*/);
    //}
    //dHnL[nSpecies+nDim]   = 0.5*(1.0+dPdU_i[nSpecies+nDim]);
    //dHnR[nSpecies+nDim]   = 0.5*(1.0+dPdU_j[nSpecies+nDim]);
    //dHnL[nSpecies+nDim+1] = 0.5*dPdU_i[nSpecies+nDim+1];
    //dHnR[nSpecies+nDim+1] = 0.5*dPdU_j[nSpecies+nDim+1];

    //    //////////////////
    //    //debug:
    //    cout << "sqVi before: " << sqVi << endl;
    //    //check sqV routine w/ conserved:
    //    double rVi, delta;
    //    rVi = 0.0;
    //    for (iDim = 0; iDim < nDim; iDim++) {
    //      rVi += rho_i*u_i[iDim]*UnitNormal[iDim];
    //    }
    //    sqVi = 0.0;
    //    for (iDim = 0; iDim < nDim; iDim++) {
    //      sqVi += (rho_i*u_i[iDim]-rVi*UnitNormal[iDim])
    //            * (rho_i*u_i[iDim]-rVi*UnitNormal[iDim])/(rho_i*rho_i);
    //    }
    //    cout << "sqVi after: " << sqVi << endl;
    //
    //      //perturb:
    //    delta = V_i[0];
    //    rho_i = V_i[0]+V_i[1]+delta;
    //    rVi = 0.0;
    //    for (iDim = 0; iDim < nDim; iDim++) {
    //      rVi += rho_i*u_i[iDim]*UnitNormal[iDim];
    //    }
    //    sqVj = 0.0;
    //    for (iDim = 0; iDim < nDim; iDim++) {
    //      sqVj += (rho_i*u_i[iDim]-rVi*UnitNormal[iDim])
    //            * (rho_i*u_i[iDim]-rVi*UnitNormal[iDim])/(rho_i*rho_i);
    //    }
    //    cout << "FD: " << (sqVj-sqVi)/delta << endl;
    //    cout << "analytic: " << -2*sqVi/(rho_i-delta) << endl;
    //    cout << "V0: " << V_i[0] << endl;
    //    cout << "V1: " << V_i[1] << endl;
    //    cout << "rho_i: " << rho_i << endl;
    //    cout << "delta: " << delta << endl;
    //    cout << "diff: " << sqVj-sqVi << endl;
    //    cin.get();




    // Derivatives of aij
    //if (0.5*(ProjVel_i+ProjVel_j) >= 0.0) {
    //  if (atl >= fabs(ProjVel_i)) {
    //    for (iVar = 0; iVar < nVar; iVar++) {
    //      daL[iVar] = fact*dHnL[iVar];
    //      daR[iVar] = fact*dHnR[iVar];
    //    }
    //  } else {
    //    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    //      daL[iSpecies] = atl*atl/(rho_i*fabs(ProjVel_i))
    //                    + 2*atl/fabs(ProjVel_i)*fact*dHnL[iSpecies];
    //      daR[iSpecies] = 2*atl/fabs(ProjVel_i)*fact*dHnR[iSpecies];
    //    }
    //    for (iDim = 0; iDim < nDim; iDim++) {
    //      daL[nSpecies+iDim] = -UnitNormal[iDim]*atl*atl/(fabs(ProjVel_i)*ProjVel_i)
    //                          + 2*atl/fabs(ProjVel_i)*fact*dHnL[nSpecies+iDim];
    //       daR[nSpecies+iDim] = 2*atl/fabs(ProjVel_i)*fact*dHnR[nSpecies+iDim];
    //    }
    //    daL[nSpecies+nDim]   = 2*atl/fabs(ProjVel_i)*fact*dHnL[nSpecies+nDim];
    //    daR[nSpecies+nDim]   = 2*atl/fabs(ProjVel_i)*fact*dHnR[nSpecies+nDim];
    //    daL[nSpecies+nDim+1] = 2*atl/fabs(ProjVel_i)*fact*dHnL[nSpecies+nDim+1];
    //    daR[nSpecies+nDim+1] = 2*atl/fabs(ProjVel_i)*fact*dHnR[nSpecies+nDim+1];
    //  }
    //} else {
    //  if (atl >= fabs(ProjVel_j)) {
    //    for (iVar = 0; iVar < nVar; iVar++) {
    //      daL[iVar] = fact*dHnL[iVar];
    //      daR[iVar] = fact*dHnR[iVar];
    //    }
    //  } else {
    //    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    //      daR[iSpecies] = atl*atl/(rho_j*fabs(ProjVel_j))
    //                    + 2*atl/fabs(ProjVel_j)*fact*dHnR[iSpecies];
    //     daL[iSpecies] = 2*atl/fabs(ProjVel_j)*fact*dHnL[iSpecies];
    //    }
    //    for (iDim = 0; iDim < nDim; iDim++) {
    //      daR[nSpecies+iDim] = -UnitNormal[iDim]*atl*atl/(fabs(ProjVel_j)*ProjVel_j)
    //                         + 2*atl/fabs(ProjVel_j)*fact*dHnR[nSpecies+iDim];
    //      daL[nSpecies+iDim] = 2*atl/fabs(ProjVel_j)*fact*dHnL[nSpecies+iDim];
    //    }
    //    daR[nSpecies+nDim]   = 2*atl/fabs(ProjVel_j)*fact*dHnR[nSpecies+nDim];
    //    daL[nSpecies+nDim]   = 2*atl/fabs(ProjVel_j)*fact*dHnL[nSpecies+nDim];
    //    daR[nSpecies+nDim+1] = 2*atl/fabs(ProjVel_j)*fact*dHnR[nSpecies+nDim+1];
    //    daL[nSpecies+nDim+1] = 2*atl/fabs(ProjVel_j)*fact*dHnL[nSpecies+nDim+1];
    //  }
    // }

    //    cout << "atl: " << atl << endl;
    //    cout << "ProjVel_i: " << ProjVel_i << endl;
    //    cout << "term1: " << atl*atl/(rho_i*fabs(ProjVel_i)) << endl;
    //    cout << "term2: " << endl;
    //    for (iVar = 0; iVar < nVar; iVar++)
    //      cout << 2*atl/fabs(ProjVel_i)*fact*dHnL[iVar] << endl;
    //    cout << "area: " << Area << endl;
    //    cout << "daL: " << endl;
    //    for (iVar = 0; iVar < nVar; iVar++) {
    //      cout << daL[iVar] << endl;
    //    }
    //    cin.get();

    /*--- Derivatives of advection speed, mL & mR ---*/
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      dmLdL[iSpecies] = -ProjVel_i/(rho_i*aij) - ProjVel_i/(aij*aij)*daL[iSpecies];
      dmRdR[iSpecies] = -ProjVel_j/(rho_j*aij) - ProjVel_j/(aij*aij)*daR[iSpecies];
    }
    for (iDim = 0; iDim < nDim; iDim++) {
      dmLdL[nSpecies+iDim] = UnitNormal[iDim]/(rho_i*aij) - ProjVel_i/(aij*aij)*daL[nSpecies+iDim];
      dmRdR[nSpecies+iDim] = UnitNormal[iDim]/(rho_j*aij) - ProjVel_j/(aij*aij)*daR[nSpecies+iDim];
    }
    dmLdL[nSpecies+nDim]   = -ProjVel_i/(aij*aij)*daL[nSpecies+nDim];
    dmRdR[nSpecies+nDim]   = -ProjVel_j/(aij*aij)*daR[nSpecies+nDim];
    dmLdL[nSpecies+nDim+1] = -ProjVel_i/(aij*aij)*daL[nSpecies+nDim+1];
    dmRdR[nSpecies+nDim+1] = -ProjVel_j/(aij*aij)*daR[nSpecies+nDim+1];
    for (iVar = 0; iVar < nVar; iVar++) {
      dmLdR[iVar] = -ProjVel_i/(aij*aij)*daR[iVar];
      dmRdL[iVar] = -ProjVel_j/(aij*aij)*daL[iVar];
    }

    /*--- Derivatives of numerical advection, mLP & mRM ---*/
    if (fabs(mL) <= 1.0) {
      for (iVar = 0; iVar < nVar; iVar++) {
        dmLPdL[iVar] = 0.5*(mL+1)*dmLdL[iVar];
        dmLPdR[iVar] = 0.5*(mL+1)*dmLdR[iVar];
      }
    } else {
      for (iVar = 0; iVar < nVar; iVar++) {
        dmLPdL[iVar] = 0.5*(dmLdL[iVar] + mL/fabs(mL)*dmLdL[iVar]);
        dmLPdR[iVar] = 0.5*(dmLdR[iVar] + mL/fabs(mL)*dmLdR[iVar]);
      }
    }
    if (fabs(mR) <= 1.0) {
      for (iVar = 0; iVar < nVar; iVar++) {
        dmRMdR[iVar] = -0.5*(mR-1)*dmRdR[iVar];
        dmRMdL[iVar] = -0.5*(mR-1)*dmRdL[iVar];
      }
    } else {
      for (iVar = 0; iVar < nVar; iVar++) {
        dmRMdR[iVar] = 0.5*(dmRdR[iVar] - mR/fabs(mR)*dmRdR[iVar]);
        dmRMdL[iVar] = 0.5*(dmRdL[iVar] - mR/fabs(mR)*dmRdL[iVar]);
      }
    }

    /*--- Derivatives of numerical advection, mbLP & mbRM ---*/
    if (mF >= 0) {
      dmbLPdL[iVar] = dmLPdL[iVar] + dmRMdL[iVar]*((1-w)*(1+fR)-fL);
      dmbLPdR[iVar] = dmLPdR[iVar] + dmRMdR[iVar]*((1-w)*(1+fR)-fL);
      dmbRMdR[iVar] = dmRMdR[iVar]*w*(1+fR);
      dmbRMdL[iVar] = dmRMdL[iVar]*w*(1+fR);
    } else {
      dmbLPdL[iVar] = dmLPdL[iVar]*w*(1+fL);
      dmbLPdR[iVar] = dmLPdR[iVar]*w*(1+fL);
      dmbRMdR[iVar] = dmRMdR[iVar] + dmLPdR[iVar]*((1-w)*(1+fL)+fL-fR);
      dmbRMdL[iVar] = dmRMdL[iVar] + dmLPdL[iVar]*((1-w)*(1+fL)+fL-fR);
    }

    /*--- Derivatives of pressure function ---*/
    if (fabs(mL) <= 1.0) {
      fact = 0.5*(mL+1)*(2-mL) - 0.25*(mL+1)*(mL+1)
          + alpha*(mL*mL-1)*(mL*mL-1) + 4*alpha*mL*mL*(mL*mL-1);
      for (iVar = 0; iVar < nVar; iVar++) {
        dpLPdL[iVar] = dPdU_i[iVar]*pLP/P_i + P_i*fact*dmLdL[iVar];
        dpLPdR[iVar] = P_i*fact*dmLdR[iVar];
      }
    } else {
      for (iVar = 0; iVar < nVar; iVar++) {
        dpLPdL[iVar] = dPdU_i[iVar] * 0.5*(mL+fabs(mL))/mL;
        dpLPdR[iVar] = 0.0;
      }
    }
    if (fabs(mR) <= 1.0) {
      fact = 0.5*(mR-1)*(2+mR) + 0.25*(mR-1)*(mR-1)
          - alpha*(mR*mR-1)*(mR*mR-1) - 4*alpha*mR*mR*(mR*mR-1);
      for (iVar = 0; iVar < nVar; iVar++) {
        dpRMdR[iVar] = dPdU_j[iVar]*pRM/P_j + P_j*fact*dmRdR[iVar];
        dpRMdL[iVar] = P_j*fact*dmRdL[iVar];
      }
    } else {
      for (iVar = 0; iVar < nVar; iVar++) {
        dpRMdR[iVar] = dPdU_j[iVar] * 0.5*(mR+fabs(mR))/mR;
        dpRMdL[iVar] = 0.0;
      }
    }

    /*--- L Jacobian ---*/
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_i[iVar][jVar] += (dmbLPdL[jVar]*FcL[iVar] + dmbRMdL[jVar]*FcR[iVar])*aij*Area;
        val_Jacobian_i[iVar][jVar] += (mbLP*FcL[iVar] + mbRM*FcR[iVar])*daL[jVar]*Area;
      }
      val_Jacobian_i[iVar][iVar] += mbLP*aij*Area;
      val_Jacobian_i[nSpecies+nDim][iVar] += mbLP*aij*dPdU_i[iVar]*Area;

      // pressure terms
      for (iDim = 0; iDim < nDim; iDim++) {
        val_Jacobian_i[nSpecies+iDim][iVar] += dpLPdL[iVar]*UnitNormal[iDim]*Area;
        val_Jacobian_i[nSpecies+iDim][iVar] += dpRMdL[iVar]*UnitNormal[iDim]*Area;
      }
    }
    /*--- R Jacobian ---*/
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_j[iVar][jVar] += (dmbLPdR[jVar]*FcL[iVar] + dmbRMdR[jVar]*FcR[iVar])*aij*Area;
        val_Jacobian_j[iVar][jVar] += (mbLP*FcL[iVar] + mbRM*FcR[iVar])*daR[jVar]*Area;
      }
      val_Jacobian_j[iVar][iVar] += mbRM*aij*Area;
      val_Jacobian_j[nSpecies+nDim][iVar] += mbRM*aij*dPdU_j[iVar]*Area;

      // pressure terms
      for (iDim = 0; iDim < nDim; iDim++) {
        val_Jacobian_j[nSpecies+iDim][iVar] += dpLPdR[iVar]*UnitNormal[iDim]*Area;
        val_Jacobian_j[nSpecies+iDim][iVar] += dpRMdR[iVar]*UnitNormal[iDim]*Area;
      }
    }
  }
}

CCentLax_TNE2::CCentLax_TNE2(unsigned short val_nDim,
                             unsigned short val_nVar,
                             unsigned short val_nPrimVar,
                             unsigned short val_nPrimVarGrad,
                             CConfig *config) : CNumerics(val_nDim,
                                                          val_nVar,
                                                          config) {

  /*--- Read configuration parameters ---*/
  implicit = (config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT);
  ionization = config->GetIonization();

  /*--- Define useful constants ---*/
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();

  /*--- Artifical dissipation part ---*/
  Param_p = 0.3;
  Param_Kappa_0 = config->GetKappa_1st_TNE2();

  /*--- Allocate some structures ---*/
  Diff_U   = new su2double[nVar];
  MeanU    = new su2double[nVar];
  MeanV    = new su2double[nPrimVar];
  MeanEve  = new su2double[nSpecies];
  MeandPdU = new su2double[nVar];
  ProjFlux = new su2double [nVar];

}

CCentLax_TNE2::~CCentLax_TNE2(void) {
  delete [] Diff_U;
  delete [] MeanU;
  delete [] MeanV;
  delete [] MeanEve;
  delete [] MeandPdU;
  delete [] ProjFlux;
}

void CCentLax_TNE2::ComputeResidual(su2double *val_resconv,
                                    su2double **val_Jacobian_i,
                                    su2double **val_Jacobian_j,
                                    CConfig *config) {

  unsigned short iDim, iSpecies, iVar;
  su2double rho_i, rho_j, h_i, h_j, a_i, a_j;
  su2double ProjVel_i, ProjVel_j, RuSI, Ru;

  /*--- Calculate geometrical quantities ---*/
  Area = 0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  /*--- Rename for convenience ---*/
  rho_i = V_i[RHO_INDEX]; rho_j = V_j[RHO_INDEX];
  h_i   = V_i[H_INDEX];   h_j   = V_j[H_INDEX];
  a_i   = V_i[A_INDEX];   a_j   = V_j[A_INDEX];
  RuSI = UNIVERSAL_GAS_CONSTANT;
  Ru   = 1000.0*RuSI;

  /*--- Compute mean quantities for the variables ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    MeanU[iVar] = 0.5*(U_i[iVar]+U_j[iVar]);
  for (iVar = 0; iVar < nPrimVar; iVar++)
    MeanV[iVar] = 0.5*(V_i[iVar]+V_j[iVar]);
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    MeanEve[iSpecies] = var->CalcEve(config, MeanV[TVE_INDEX], iSpecies);

  var->CalcdPdU(MeanV, MeanEve, config, MeandPdU);

  /*--- Get projected flux tensor ---*/
  GetInviscidProjFlux(MeanU, MeanV, Normal, ProjFlux);

  /*--- Compute inviscid residual ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_resconv[iVar] = ProjFlux[iVar];
  }

  /*--- Jacobians of the inviscid flux, scale = 0.5 because val_resconv ~ 0.5*(fc_i+fc_j)*Normal ---*/
  if (implicit) {
    GetInviscidProjJac(MeanU, MeanV, MeandPdU, Normal, 0.5, val_Jacobian_i);

    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        val_Jacobian_j[iVar][jVar] = val_Jacobian_i[iVar][jVar];
  }

  /*--- Computes differences btw. conservative variables ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    Diff_U[iVar] = U_i[iVar] - U_j[iVar];
  Diff_U[nSpecies+nDim] = rho_i*h_i - rho_j*h_j;

  /*--- Compute the local spectral radius and the stretching factor ---*/
  ProjVel_i = 0; ProjVel_j = 0; Area = 0;
  for (iDim = 0; iDim < nDim; iDim++) {
    ProjVel_i += V_i[VEL_INDEX+iDim]*Normal[iDim];
    ProjVel_j += V_j[VEL_INDEX+iDim]*Normal[iDim];
  }
  Area = sqrt(Area);
  Local_Lambda_i = (fabs(ProjVel_i)+a_i*Area);
  Local_Lambda_j = (fabs(ProjVel_j)+a_j*Area);
  MeanLambda = 0.5*(Local_Lambda_i+Local_Lambda_j);

  Phi_i = pow(Lambda_i/(4.0*MeanLambda+EPS),Param_p);
  Phi_j = pow(Lambda_j/(4.0*MeanLambda+EPS),Param_p);
  StretchingFactor = 4.0*Phi_i*Phi_j/(Phi_i+Phi_j+EPS);

  sc0 = 3.0*(su2double(Neighbor_i)+su2double(Neighbor_j))/(su2double(Neighbor_i)*su2double(Neighbor_j));
  Epsilon_0 = Param_Kappa_0*sc0*su2double(nDim)/3.0;

  /*--- Compute viscous part of the residual ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_resconv[iVar] += Epsilon_0*Diff_U[iVar]*StretchingFactor*MeanLambda;
  }

  if (implicit) {
    cte = Epsilon_0*StretchingFactor*MeanLambda;

    for (iVar = 0; iVar < nSpecies+nDim; iVar++) {
      val_Jacobian_i[iVar][iVar] += cte;
      val_Jacobian_j[iVar][iVar] -= cte;
    }

    /*--- Last rows: CAREFUL!! You have differences of \rho_Enthalpy, not differences of \rho_Energy ---*/
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
      val_Jacobian_i[nSpecies+nDim][iSpecies] += cte*dPdU_i[iSpecies];
    for (iDim = 0; iDim < nDim; iDim++)
      val_Jacobian_i[nSpecies+nDim][nSpecies+iDim]   += cte*dPdU_i[nSpecies+iDim];
    val_Jacobian_i[nSpecies+nDim][nSpecies+nDim]     += cte*(1+dPdU_i[nSpecies+nDim]);
    val_Jacobian_i[nSpecies+nDim][nSpecies+nDim+1]   += cte*dPdU_i[nSpecies+nDim+1];
    val_Jacobian_i[nSpecies+nDim+1][nSpecies+nDim+1] += cte;

    /*--- Last row of Jacobian_j ---*/
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
      val_Jacobian_j[nSpecies+nDim][iSpecies] -= cte*dPdU_j[iSpecies];
    for (iDim = 0; iDim < nDim; iDim++)
      val_Jacobian_j[nSpecies+nDim][nSpecies+iDim]   -= cte*dPdU_j[nSpecies+nDim];
    val_Jacobian_j[nSpecies+nDim][nSpecies+nDim]     -= cte*(1+dPdU_j[nSpecies+nDim]);
    val_Jacobian_j[nSpecies+nDim][nSpecies+nDim+1]   -= cte*dPdU_j[nSpecies+nDim+1];
    val_Jacobian_j[nSpecies+nDim+1][nSpecies+nDim+1] -= cte;
  }
}

CAvgGrad_TNE2::CAvgGrad_TNE2(unsigned short val_nDim,
                             unsigned short val_nVar,
                             unsigned short val_nPrimVar,
                             unsigned short val_nPrimVarGrad,
                             CConfig *config) : CNumerics(val_nDim,
                                                          val_nVar,
                                                          config) {

  implicit = (config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT);

  /*--- Rename for convenience ---*/
  nDim         = val_nDim;
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;

  /*--- Compressible flow, primitive variables nDim+3, (T,vx,vy,vz,P,rho) ---*/
  PrimVar_i    = new su2double [nPrimVar];
  PrimVar_j    = new su2double [nPrimVar];
  Mean_PrimVar = new su2double [nPrimVar];

  Mean_U      = new su2double[nVar];
  Mean_dPdU   = new su2double[nVar];
  Mean_dTdU   = new su2double[nVar];
  Mean_dTvedU = new su2double[nVar];
  Mean_Eve    = new su2double[nSpecies];
  Mean_Cvve   = new su2double[nSpecies];
  Mean_GU     = new su2double*[nVar];
  for (iVar = 0; iVar < nVar; iVar++)
    Mean_GU[iVar] = new su2double[nDim];

  Mean_Diffusion_Coeff = new su2double[nSpecies];

  /*--- Compressible flow, primitive gradient variables nDim+3, (T,vx,vy,vz) ---*/
  Mean_GradPrimVar = new su2double* [nPrimVarGrad];
  for (iVar = 0; iVar < nPrimVarGrad; iVar++)
    Mean_GradPrimVar[iVar] = new su2double [nDim];
}

CAvgGrad_TNE2::~CAvgGrad_TNE2(void) {

  delete [] PrimVar_i;
  delete [] PrimVar_j;
  delete [] Mean_PrimVar;
  delete [] Mean_Diffusion_Coeff;

  delete [] Mean_U;
  delete [] Mean_dPdU;
  delete [] Mean_dTdU;
  delete [] Mean_dTvedU;
  delete [] Mean_Eve;
  delete [] Mean_Cvve;
  for (iVar = 0; iVar < nVar; iVar++)
    delete [] Mean_GU[iVar];
  delete [] Mean_GU;

  for (iVar = 0; iVar < nPrimVarGrad; iVar++)
    delete [] Mean_GradPrimVar[iVar];
  delete [] Mean_GradPrimVar;
}

void CAvgGrad_TNE2::ComputeResidual(su2double *val_residual,
                                    su2double **val_Jacobian_i,
                                    su2double **val_Jacobian_j,
                                    CConfig *config) {

  unsigned short iSpecies, iVar, iDim;

  /*--- Normalized normal vector ---*/
  Area = 0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  /*--- Mean transport coefficients ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    Mean_Diffusion_Coeff[iSpecies] = 0.5*(Diffusion_Coeff_i[iSpecies] +
                                          Diffusion_Coeff_j[iSpecies]);
  Mean_Laminar_Viscosity = 0.5*(Laminar_Viscosity_i +
                                Laminar_Viscosity_j);
  Mean_Thermal_Conductivity = 0.5*(Thermal_Conductivity_i +
                                   Thermal_Conductivity_j);
  Mean_Thermal_Conductivity_ve = 0.5*(Thermal_Conductivity_ve_i +
                                      Thermal_Conductivity_ve_j);

  /*--- Mean gradient approximation ---*/
  // Mass fraction
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    PrimVar_i[iSpecies] = V_i[iSpecies]/V_i[RHO_INDEX];
    PrimVar_j[iSpecies] = V_j[iSpecies]/V_j[RHO_INDEX];
    Mean_PrimVar[iSpecies] = 0.5*(PrimVar_i[iSpecies] + PrimVar_j[iSpecies]);
    for (iDim = 0; iDim < nDim; iDim++) {
      Mean_GradPrimVar[iSpecies][iDim] = 0.5*(1.0/V_i[RHO_INDEX] *
                                              (PrimVar_Grad_i[iSpecies][iDim] -
                                               PrimVar_i[iSpecies] *
                                               PrimVar_Grad_i[RHO_INDEX][iDim]) +
                                              1.0/V_j[RHO_INDEX] *
                                              (PrimVar_Grad_j[iSpecies][iDim] -
                                               PrimVar_j[iSpecies] *
                                               PrimVar_Grad_j[RHO_INDEX][iDim]));

    }
  }

  for (iVar = nSpecies; iVar < nPrimVar; iVar++) {
    PrimVar_i[iVar] = V_i[iVar];
    PrimVar_j[iVar] = V_j[iVar];
    Mean_PrimVar[iVar] = 0.5*(PrimVar_i[iVar]+PrimVar_j[iVar]);
  }
  for (iVar = nSpecies; iVar < nPrimVarGrad; iVar++) {
    for (iDim = 0; iDim < nDim; iDim++) {
      Mean_GradPrimVar[iVar][iDim] = 0.5*(PrimVar_Grad_i[iVar][iDim] +
                                          PrimVar_Grad_j[iVar][iDim]);
    }
  }
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Mean_Eve[iSpecies]  = 0.5*(eve_i[iSpecies]  + eve_j[iSpecies]);
    Mean_Cvve[iSpecies] = 0.5*(Cvve_i[iSpecies] + Cvve_j[iSpecies]);
  }

  /*--- Get projected flux tensor ---*/
  GetViscousProjFlux(Mean_PrimVar, Mean_GradPrimVar, Mean_Eve, Normal,
                     Mean_Diffusion_Coeff, Mean_Laminar_Viscosity,
                     Mean_Thermal_Conductivity, Mean_Thermal_Conductivity_ve,
                     config);

  /*--- Update viscous residual ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    val_residual[iVar] = Proj_Flux_Tensor[iVar];

  /*--- Compute the implicit part ---*/
  if (implicit) {
    dist_ij = 0.0;
    for (iDim = 0; iDim < nDim; iDim++)
      dist_ij += (Coord_j[iDim]-Coord_i[iDim])*(Coord_j[iDim]-Coord_i[iDim]);
    dist_ij = sqrt(dist_ij);

    GetViscousProjJacs(Mean_PrimVar, Mean_GradPrimVar, Mean_Eve, Mean_Cvve,
                       Mean_Diffusion_Coeff, Mean_Laminar_Viscosity,
                       Mean_Thermal_Conductivity, Mean_Thermal_Conductivity_ve,
                       dist_ij, UnitNormal, Area, Proj_Flux_Tensor,
                       val_Jacobian_i, val_Jacobian_j, config);
  }
}


void CAvgGrad_TNE2::GetViscousProjFlux(su2double *val_primvar,
                                       su2double **val_gradprimvar,
                                       su2double *val_eve,
                                       su2double *val_normal,
                                       su2double *val_diffusioncoeff,
                                       su2double val_viscosity,
                                       su2double val_therm_conductivity,
                                       su2double val_therm_conductivity_ve,
                                       CConfig *config) {

  // Requires a slightly non-standard primitive vector:
  // Assumes -     V = [Y1, ... , Yn, T, Tve, ... ]
  // and gradient GV = [GY1, ... , GYn, GT, GTve, ... ]
  // rather than the standard V = [r1, ... , rn, T, Tve, ... ]

  bool ionization;
  unsigned short iSpecies, iVar, iDim, jDim, nHeavy, nEl;
  su2double *Ds, *V, **GV, mu, ktr, kve, div_vel;
  su2double Ru, RuSI;
  su2double rho, T, Tve;

  /*--- Initialize ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    Proj_Flux_Tensor[iVar] = 0.0;
    for (iDim = 0; iDim < nDim; iDim++)
      Flux_Tensor[iVar][iDim] = 0.0;
  }

  /*--- Read from CConfig ---*/
  ionization = config->GetIonization();
  if (ionization) { nHeavy = nSpecies-1; nEl = 1; }
  else            { nHeavy = nSpecies;   nEl = 0; }

  /*--- Rename for convenience ---*/
  Ds  = val_diffusioncoeff;
  mu  = val_viscosity;
  ktr = val_therm_conductivity;
  kve = val_therm_conductivity_ve;
  rho = val_primvar[RHO_INDEX];
  T   = val_primvar[T_INDEX];
  Tve = val_primvar[TVE_INDEX];
  RuSI= UNIVERSAL_GAS_CONSTANT;
  Ru  = 1000.0*RuSI;
  V   = val_primvar;
  GV  = val_gradprimvar;
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    hs[iSpecies]  = var->CalcHs(config, T, val_eve[iSpecies], iSpecies);

  /*--- Calculate the velocity divergence ---*/
  div_vel = 0.0;
  for (iDim = 0 ; iDim < nDim; iDim++)
    div_vel += GV[VEL_INDEX+iDim][iDim];

  /*--- Pre-compute mixture quantities ---*/
  for (iDim = 0; iDim < nDim; iDim++) {
    Vector[iDim] = 0.0;
    for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
      Vector[iDim] += rho*Ds[iSpecies]*GV[RHOS_INDEX+iSpecies][iDim];
    }
  }

  /*--- Compute the viscous stress tensor ---*/
  for (iDim = 0; iDim < nDim; iDim++)
    for (jDim = 0; jDim < nDim; jDim++)
      tau[iDim][jDim] = 0.0;
  for (iDim = 0 ; iDim < nDim; iDim++) {
    for (jDim = 0 ; jDim < nDim; jDim++) {
      tau[iDim][jDim] += mu * (val_gradprimvar[VEL_INDEX+jDim][iDim] +
          val_gradprimvar[VEL_INDEX+iDim][jDim]);
    }
    tau[iDim][iDim] -= TWO3*mu*div_vel;
  }

  /*--- Populate entries in the viscous flux vector ---*/
  for (iDim = 0; iDim < nDim; iDim++) {
    /*--- Species diffusion velocity ---*/
    for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
      Flux_Tensor[iSpecies][iDim] = rho*Ds[iSpecies]*GV[RHOS_INDEX+iSpecies][iDim]
          - V[RHOS_INDEX+iSpecies]*Vector[iDim];
    }
    if (ionization) {
      cout << "GetViscProjFlux -- NEED TO IMPLEMENT IONIZED FUNCTIONALITY!!!" << endl;
      exit(1);
    }
    /*--- Shear stress related terms ---*/
    Flux_Tensor[nSpecies+nDim][iDim] = 0.0;
    for (jDim = 0; jDim < nDim; jDim++) {
      Flux_Tensor[nSpecies+jDim][iDim]  = tau[iDim][jDim];
      Flux_Tensor[nSpecies+nDim][iDim] += tau[iDim][jDim]*val_primvar[VEL_INDEX+jDim];
    }

    /*--- Diffusion terms ---*/
    for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
      Flux_Tensor[nSpecies+nDim][iDim]   += Flux_Tensor[iSpecies][iDim] * hs[iSpecies];
      Flux_Tensor[nSpecies+nDim+1][iDim] += Flux_Tensor[iSpecies][iDim] * val_eve[iSpecies];
    }

    /*--- Heat transfer terms ---*/
    Flux_Tensor[nSpecies+nDim][iDim]   += ktr*GV[T_INDEX][iDim] +
        kve*GV[TVE_INDEX][iDim];
    Flux_Tensor[nSpecies+nDim+1][iDim] += kve*GV[TVE_INDEX][iDim];
  }

  for (iVar = 0; iVar < nVar; iVar++) {
    for (iDim = 0; iDim < nDim; iDim++) {
      Proj_Flux_Tensor[iVar] += Flux_Tensor[iVar][iDim]*val_normal[iDim];
    }
  }
}

void CAvgGrad_TNE2::GetViscousProjJacs(su2double *val_Mean_PrimVar,
                                       su2double **val_Mean_GradPrimVar,
                                       su2double *val_Mean_Eve,
                                       su2double *val_Mean_Cvve,
                                       su2double *val_diffusion_coeff,
                                       su2double val_laminar_viscosity,
                                       su2double val_thermal_conductivity,
                                       su2double val_thermal_conductivity_ve,
                                       su2double val_dist_ij, su2double *val_normal,
                                       su2double val_dS, su2double *val_Fv,
                                       su2double **val_Jac_i, su2double **val_Jac_j,
                                       CConfig *config) {

  bool ionization;
  unsigned short iDim, iSpecies, jSpecies, iVar, jVar, kVar, nHeavy, nEl;
  su2double rho, rho_i, rho_j, vel[3], T, Tve, *xi, *Ms;
  su2double mu, ktr, kve, *Ds, dij, Ru, RuSI;
  su2double theta, thetax, thetay, thetaz;
  su2double etax, etay, etaz;
  su2double pix, piy, piz;
  su2double sumY, sumY_i, sumY_j;

  /*--- Initialize arrays ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      dFdVi[iVar][jVar] = 0.0;
      dFdVj[iVar][jVar] = 0.0;
      dVdUi[iVar][jVar] = 0.0;
      dVdUj[iVar][jVar] = 0.0;
    }
  }

  /*--- Initialize the Jacobian matrices ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      val_Jac_i[iVar][jVar] = 0.0;
      val_Jac_j[iVar][jVar] = 0.0;
    }
  }

  /*--- Initialize storage vectors & matrices ---*/
  for (iVar = 0; iVar < nSpecies; iVar++) {
    sumdFdYjh[iVar]   = 0.0;
    sumdFdYjeve[iVar] = 0.0;
    for (jVar = 0; jVar < nSpecies; jVar++) {
      dFdYi[iVar][jVar] = 0.0;
      dFdYj[iVar][jVar] = 0.0;
      dJdr_i[iVar][jVar] = 0.0;
      dJdr_j[iVar][jVar] = 0.0;
    }
  }

  /*--- Assign booleans from CConfig ---*/
  ionization = config->GetIonization();
  if (ionization) { nHeavy = nSpecies-1; nEl = 1; }
  else            { nHeavy = nSpecies;   nEl = 0; }

  /*--- Calculate preliminary geometrical quantities ---*/
  dij = val_dist_ij;
  theta = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    theta += val_normal[iDim]*val_normal[iDim];
  }


  /*--- Rename for convenience ---*/
  rho = val_Mean_PrimVar[RHO_INDEX];
  rho_i = V_i[RHO_INDEX];
  rho_j = V_j[RHO_INDEX];
  T   = val_Mean_PrimVar[T_INDEX];
  Tve = val_Mean_PrimVar[TVE_INDEX];
  Ds  = val_diffusion_coeff;
  mu  = val_laminar_viscosity;
  ktr = val_thermal_conductivity;
  kve = val_thermal_conductivity_ve;
  RuSI= UNIVERSAL_GAS_CONSTANT;
  Ru  = 1000.0*RuSI;
  Ms  = config->GetMolar_Mass();
  xi  = config->GetRotationModes();
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Ys[iSpecies]   = val_Mean_PrimVar[RHOS_INDEX+iSpecies];
    Ys_i[iSpecies] = V_i[RHOS_INDEX+iSpecies]/V_i[RHO_INDEX];
    Ys_j[iSpecies] = V_j[RHOS_INDEX+iSpecies]/V_j[RHO_INDEX];
    hs[iSpecies]   = var->CalcHs(config, T, val_Mean_Eve[iSpecies], iSpecies);
    Cvtr[iSpecies] = (3.0/2.0 + xi[iSpecies]/2.0)*Ru/Ms[iSpecies];
  }
  for (iDim = 0; iDim < nDim; iDim++)
    vel[iDim] = val_Mean_PrimVar[VEL_INDEX+iDim];

  /*--- Calculate useful diffusion parameters ---*/
  // Summation term of the diffusion fluxes
  sumY = 0.0;
  sumY_i = 0.0;
  sumY_j = 0.0;
  for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
    sumY_i += Ds[iSpecies]*theta/dij*Ys_i[iSpecies];
    sumY_j += Ds[iSpecies]*theta/dij*Ys_j[iSpecies];
    sumY   += Ds[iSpecies]*theta/dij*(Ys_j[iSpecies]-Ys_i[iSpecies]);
  }


  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    for (jSpecies  = 0; jSpecies < nSpecies; jSpecies++) {

      // first term
      dJdr_j[iSpecies][jSpecies] +=  0.5*(Ds[iSpecies]*theta/dij *
                                          (Ys_j[iSpecies]*rho_i/rho_j +
                                           Ys_i[iSpecies]));
      dJdr_i[iSpecies][jSpecies] += -0.5*(Ds[iSpecies]*theta/dij *
                                          (Ys_j[iSpecies] +
                                           Ys_i[iSpecies]*rho_j/rho_i));

      // second term
      dJdr_j[iSpecies][jSpecies] +=
          0.25*(Ys_i[iSpecies] - rho_i/rho_j*Ys_j[iSpecies])*sumY
          + 0.25*(Ys_i[iSpecies]+Ys_j[iSpecies])*(rho_i+rho_j)*Ds[jSpecies]*theta/(dij*rho_j)
          - 0.25*(Ys_i[iSpecies]+Ys_j[iSpecies])*(rho_i+rho_j)*sumY_j/rho_j;

      dJdr_i[iSpecies][jSpecies] +=
          0.25*(-rho_j/rho_i*Ys_i[iSpecies]+Ys_j[iSpecies])*sumY
          - 0.25*(Ys_i[iSpecies]+Ys_j[iSpecies])*(rho_i+rho_j)*Ds[jSpecies]*theta/(dij*rho_i)
          + 0.25*(Ys_i[iSpecies]+Ys_j[iSpecies])*(rho_i+rho_j)*sumY_i/rho_i;
    }

    // first term
    dJdr_j[iSpecies][iSpecies] += -0.5*Ds[iSpecies]*theta/dij*(1+rho_i/rho_j);
    dJdr_i[iSpecies][iSpecies] +=  0.5*Ds[iSpecies]*theta/dij*(1+rho_j/rho_i);

    // second term
    dJdr_j[iSpecies][iSpecies] += 0.25*(1.0+rho_i/rho_j)*sumY;
    dJdr_i[iSpecies][iSpecies] += 0.25*(1.0+rho_j/rho_i)*sumY;
  }

  /*--- Calculate transformation matrix ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    dVdUi[iSpecies][iSpecies] = 1.0;
    dVdUj[iSpecies][iSpecies] = 1.0;
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      dVdUi[nSpecies+iDim][iSpecies] = -V_i[VEL_INDEX+iDim]/V_i[RHO_INDEX];
      dVdUj[nSpecies+iDim][iSpecies] = -V_j[VEL_INDEX+iDim]/V_j[RHO_INDEX];
    }
    dVdUi[nSpecies+iDim][nSpecies+iDim] = 1.0/V_i[RHO_INDEX];
    dVdUj[nSpecies+iDim][nSpecies+iDim] = 1.0/V_j[RHO_INDEX];
  }
  for (iVar = 0; iVar < nVar; iVar++) {
    dVdUi[nSpecies+nDim][iVar]   = dTdU_i[iVar];
    dVdUj[nSpecies+nDim][iVar]   = dTdU_j[iVar];
    dVdUi[nSpecies+nDim+1][iVar] = dTvedU_i[iVar];
    dVdUj[nSpecies+nDim+1][iVar] = dTvedU_j[iVar];
  }


  if (nDim == 2) {

    /*--- Geometry parameters ---*/
    thetax = theta + val_normal[0]*val_normal[0]/3.0;
    thetay = theta + val_normal[1]*val_normal[1]/3.0;
    etaz   = val_normal[0]*val_normal[1]/3.0;
    pix    = mu/dij * (thetax*vel[0] + etaz*vel[1]  );
    piy    = mu/dij * (etaz*vel[0]   + thetay*vel[1]);

    /*--- Populate primitive Jacobian ---*/

    // X-momentum
    dFdVj[nSpecies][nSpecies]     = mu*thetax/dij*val_dS;
    dFdVj[nSpecies][nSpecies+1]   = mu*etaz/dij*val_dS;

    // Y-momentum
    dFdVj[nSpecies+1][nSpecies]   = mu*etaz/dij*val_dS;
    dFdVj[nSpecies+1][nSpecies+1] = mu*thetay/dij*val_dS;

    // Energy
    dFdVj[nSpecies+2][nSpecies]   = pix*val_dS;
    dFdVj[nSpecies+2][nSpecies+1] = piy*val_dS;
    dFdVj[nSpecies+2][nSpecies+2] = ktr*theta/dij*val_dS;
    dFdVj[nSpecies+2][nSpecies+3] = kve*theta/dij*val_dS;

    // Vib-el Energy
    dFdVj[nSpecies+3][nSpecies+3] = kve*theta/dij*val_dS;

    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        dFdVi[iVar][jVar] = -dFdVj[iVar][jVar];

    // Common terms
    dFdVi[nSpecies+2][nSpecies]   += 0.5*val_Fv[nSpecies];
    dFdVj[nSpecies+2][nSpecies]   += 0.5*val_Fv[nSpecies];
    dFdVi[nSpecies+2][nSpecies+1] += 0.5*val_Fv[nSpecies+1];
    dFdVj[nSpecies+2][nSpecies+1] += 0.5*val_Fv[nSpecies+1];
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      dFdVi[nSpecies+2][nSpecies+2] += 0.5*val_Fv[iSpecies]*(Ru/Ms[iSpecies] +
                                                             Cvtr[iSpecies]   );
      dFdVj[nSpecies+2][nSpecies+2] += 0.5*val_Fv[iSpecies]*(Ru/Ms[iSpecies] +
                                                             Cvtr[iSpecies]   );
      dFdVi[nSpecies+2][nSpecies+3] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVj[nSpecies+2][nSpecies+3] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVi[nSpecies+3][nSpecies+3] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVj[nSpecies+3][nSpecies+3] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
    }

    // Unique terms
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
        dFdVj[iSpecies][jSpecies]   += -dJdr_j[iSpecies][jSpecies]*val_dS;
        dFdVi[iSpecies][jSpecies]   += -dJdr_i[iSpecies][jSpecies]*val_dS;
        dFdVj[nSpecies+2][iSpecies] += -dJdr_j[jSpecies][iSpecies]*hs[jSpecies]*val_dS;
        dFdVi[nSpecies+2][iSpecies] += -dJdr_i[jSpecies][iSpecies]*hs[jSpecies]*val_dS;
        dFdVj[nSpecies+3][iSpecies] += -dJdr_j[jSpecies][iSpecies]*val_Mean_Eve[jSpecies]*val_dS;
        dFdVi[nSpecies+3][iSpecies] += -dJdr_i[jSpecies][iSpecies]*val_Mean_Eve[jSpecies]*val_dS;
      }
    }

  } //nDim == 2
  else {

    /*--- Geometry parameters ---*/
    thetax = theta + val_normal[0]*val_normal[0]/3.0;
    thetay = theta + val_normal[1]*val_normal[1]/3.0;
    thetaz = theta + val_normal[2]*val_normal[2]/3.0;
    etax   = val_normal[1]*val_normal[2]/3.0;
    etay   = val_normal[0]*val_normal[2]/3.0;
    etaz   = val_normal[0]*val_normal[1]/3.0;
    pix    = mu/dij * (thetax*vel[0] + etaz*vel[1]   + etay*vel[2]  );
    piy    = mu/dij * (etaz*vel[0]   + thetay*vel[1] + etax*vel[2]  );
    piz    = mu/dij * (etay*vel[0]   + etax*vel[1]   + thetaz*vel[2]);

    /*--- Populate primitive Jacobian ---*/

    // X-momentum
    dFdVj[nSpecies][nSpecies]     = mu*thetax/dij*val_dS;
    dFdVj[nSpecies][nSpecies+1]   = mu*etaz/dij*val_dS;
    dFdVj[nSpecies][nSpecies+2]   = mu*etay/dij*val_dS;

    // Y-momentum
    dFdVj[nSpecies+1][nSpecies]   = mu*etaz/dij*val_dS;
    dFdVj[nSpecies+1][nSpecies+1] = mu*thetay/dij*val_dS;
    dFdVj[nSpecies+1][nSpecies+2] = mu*etax/dij*val_dS;

    // Z-momentum
    dFdVj[nSpecies+2][nSpecies]   = mu*etay/dij*val_dS;
    dFdVj[nSpecies+2][nSpecies+1] = mu*etax/dij*val_dS;
    dFdVj[nSpecies+2][nSpecies+2] = mu*thetaz/dij*val_dS;

    // Energy
    dFdVj[nSpecies+3][nSpecies]   = pix*val_dS;
    dFdVj[nSpecies+3][nSpecies+1] = piy*val_dS;
    dFdVj[nSpecies+3][nSpecies+2] = piz*val_dS;
    dFdVj[nSpecies+3][nSpecies+3] = ktr*theta/dij*val_dS;
    dFdVj[nSpecies+3][nSpecies+4] = kve*theta/dij*val_dS;

    // Vib.-el energy
    dFdVj[nSpecies+4][nSpecies+4] = kve*theta/dij*val_dS;

    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        dFdVi[iVar][jVar] = -dFdVj[iVar][jVar];

    // Common terms
    for (iDim = 0; iDim < nDim; iDim++) {
      dFdVi[nSpecies+3][nSpecies+iDim]   += 0.5*val_Fv[nSpecies+iDim];
      dFdVj[nSpecies+3][nSpecies+iDim]   += 0.5*val_Fv[nSpecies+iDim];
    }
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      dFdVi[nSpecies+3][nSpecies+3] += 0.5*val_Fv[iSpecies]*(Ru/Ms[iSpecies] +
                                                             Cvtr[iSpecies]   );
      dFdVj[nSpecies+3][nSpecies+3] += 0.5*val_Fv[iSpecies]*(Ru/Ms[iSpecies] +
                                                             Cvtr[iSpecies]   );
      dFdVi[nSpecies+3][nSpecies+4] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVj[nSpecies+3][nSpecies+4] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVi[nSpecies+4][nSpecies+4] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVj[nSpecies+4][nSpecies+4] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
    }

    // Unique terms
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
        dFdVj[iSpecies][jSpecies]   += -dJdr_j[iSpecies][jSpecies]*val_dS;
        dFdVi[iSpecies][jSpecies]   += -dJdr_i[iSpecies][jSpecies]*val_dS;
        dFdVj[nSpecies+3][iSpecies] += -dJdr_j[jSpecies][iSpecies]*hs[jSpecies]*val_dS;
        dFdVi[nSpecies+3][iSpecies] += -dJdr_i[jSpecies][iSpecies]*hs[jSpecies]*val_dS;
        dFdVj[nSpecies+4][iSpecies] += -dJdr_j[jSpecies][iSpecies]*val_Mean_Eve[jSpecies]*val_dS;
        dFdVi[nSpecies+4][iSpecies] += -dJdr_i[jSpecies][iSpecies]*val_Mean_Eve[jSpecies]*val_dS;
      }
    }

  } // nDim == 3

  /*--- dFv/dUij = dFv/dVij * dVij/dUij ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    for (jVar = 0; jVar < nVar; jVar++)
      for (kVar = 0; kVar < nVar; kVar++) {
        val_Jac_i[iVar][jVar] += dFdVi[iVar][kVar]*dVdUi[kVar][jVar];
        val_Jac_j[iVar][jVar] += dFdVj[iVar][kVar]*dVdUj[kVar][jVar];
      }
}

CAvgGradCorrected_TNE2::CAvgGradCorrected_TNE2(unsigned short val_nDim,
                                               unsigned short val_nVar,
                                               unsigned short val_nPrimVar,
                                               unsigned short val_nPrimVarGrad,
                                               CConfig *config) : CNumerics(val_nDim,
                                                                            val_nVar,
                                                                            config) {

  implicit = (config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT);

  /*--- Rename for convenience ---*/
  nDim         = val_nDim;
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;

  /*--- Compressible flow, primitive variables nDim+3, (T,vx,vy,vz,P,rho) ---*/
  PrimVar_i    = new su2double [nPrimVar];
  PrimVar_j    = new su2double [nPrimVar];
  Mean_PrimVar = new su2double [nPrimVar];

  Mean_Eve  = new su2double[nSpecies];
  Mean_Cvve = new su2double[nSpecies];

  Mean_Diffusion_Coeff = new su2double[nSpecies];

  /*--- Compressible flow, primitive gradient variables nDim+3, (T,vx,vy,vz) ---*/
  Mean_GradPrimVar = new su2double* [nPrimVarGrad];
  for (iVar = 0; iVar < nPrimVarGrad; iVar++)
    Mean_GradPrimVar[iVar] = new su2double [nDim];

  Proj_Mean_GradPrimVar_Edge = new su2double[nPrimVarGrad];
  Edge_Vector = new su2double[3];
}

CAvgGradCorrected_TNE2::~CAvgGradCorrected_TNE2(void) {

  delete [] PrimVar_i;
  delete [] PrimVar_j;
  delete [] Mean_PrimVar;

  delete [] Mean_Eve;
  delete [] Mean_Cvve;

  delete [] Mean_Diffusion_Coeff;

  for (iVar = 0; iVar < nPrimVarGrad; iVar++)
    delete [] Mean_GradPrimVar[iVar];
  delete [] Mean_GradPrimVar;

  delete [] Proj_Mean_GradPrimVar_Edge;
  delete [] Edge_Vector;
}

void CAvgGradCorrected_TNE2::GetViscousProjFlux(su2double *val_primvar,
                                                su2double **val_gradprimvar,
                                                su2double *val_eve,
                                                su2double *val_normal,
                                                su2double *val_diffusioncoeff,
                                                su2double val_viscosity,
                                                su2double val_therm_conductivity,
                                                su2double val_therm_conductivity_ve,
                                                CConfig *config) {

  // Requires a slightly non-standard primitive vector:
  // Assumes -     V = [Y1, ... , Yn, T, Tve, ... ]
  // and gradient GV = [GY1, ... , GYn, GT, GTve, ... ]
  // rather than the standard V = [r1, ... , rn, T, Tve, ... ]

  bool ionization;
  unsigned short iSpecies, iVar, iDim, jDim, nHeavy, nEl;
  su2double *Ds, *V, **GV, mu, ktr, kve, div_vel;
  su2double Ru, RuSI;
  su2double rho, T, Tve;

  /*--- Initialize ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    Proj_Flux_Tensor[iVar] = 0.0;
    for (iDim = 0; iDim < nDim; iDim++)
      Flux_Tensor[iVar][iDim] = 0.0;
  }

  /*--- Read from CConfig ---*/
  ionization = config->GetIonization();
  if (ionization) { nHeavy = nSpecies-1; nEl = 1; }
  else            { nHeavy = nSpecies;   nEl = 0; }

  /*--- Rename for convenience ---*/
  Ds  = val_diffusioncoeff;
  mu  = val_viscosity;
  ktr = val_therm_conductivity;
  kve = val_therm_conductivity_ve;
  rho = val_primvar[RHO_INDEX];
  T   = val_primvar[T_INDEX];
  Tve = val_primvar[TVE_INDEX];
  RuSI= UNIVERSAL_GAS_CONSTANT;
  Ru  = 1000.0*RuSI;
  V   = val_primvar;
  GV  = val_gradprimvar;
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    hs[iSpecies]  = var->CalcHs(config, T, val_eve[iSpecies], iSpecies);

  /*--- Calculate the velocity divergence ---*/
  div_vel = 0.0;
  for (iDim = 0 ; iDim < nDim; iDim++)
    div_vel += GV[VEL_INDEX+iDim][iDim];

  /*--- Pre-compute mixture quantities ---*/
  for (iDim = 0; iDim < nDim; iDim++) {
    Vector[iDim] = 0.0;
    for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
      Vector[iDim] += rho*Ds[iSpecies]*GV[RHOS_INDEX+iSpecies][iDim];
    }
  }

  /*--- Compute the viscous stress tensor ---*/
  for (iDim = 0; iDim < nDim; iDim++)
    for (jDim = 0; jDim < nDim; jDim++)
      tau[iDim][jDim] = 0.0;
  for (iDim = 0 ; iDim < nDim; iDim++) {
    for (jDim = 0 ; jDim < nDim; jDim++) {
      tau[iDim][jDim] += mu * (val_gradprimvar[VEL_INDEX+jDim][iDim] +
          val_gradprimvar[VEL_INDEX+iDim][jDim]);
    }
    tau[iDim][iDim] -= TWO3*mu*div_vel;
  }

  /*--- Populate entries in the viscous flux vector ---*/
  for (iDim = 0; iDim < nDim; iDim++) {
    /*--- Species diffusion velocity ---*/
    for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
      Flux_Tensor[iSpecies][iDim] = rho*Ds[iSpecies]*GV[RHOS_INDEX+iSpecies][iDim]
          - V[RHOS_INDEX+iSpecies]*Vector[iDim];
    }
    if (ionization) {
      cout << "GetViscProjFlux -- NEED TO IMPLEMENT IONIZED FUNCTIONALITY!!!" << endl;
      exit(1);
    }
    /*--- Shear stress related terms ---*/
    Flux_Tensor[nSpecies+nDim][iDim] = 0.0;
    for (jDim = 0; jDim < nDim; jDim++) {
      Flux_Tensor[nSpecies+jDim][iDim]  = tau[iDim][jDim];
      Flux_Tensor[nSpecies+nDim][iDim] += tau[iDim][jDim]*val_primvar[VEL_INDEX+jDim];
    }

    /*--- Diffusion terms ---*/
    for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
      Flux_Tensor[nSpecies+nDim][iDim]   += Flux_Tensor[iSpecies][iDim] * hs[iSpecies];
      Flux_Tensor[nSpecies+nDim+1][iDim] += Flux_Tensor[iSpecies][iDim] * val_eve[iSpecies];
    }

    /*--- Heat transfer terms ---*/
    Flux_Tensor[nSpecies+nDim][iDim]   += ktr*GV[T_INDEX][iDim] +
        kve*GV[TVE_INDEX][iDim];
    Flux_Tensor[nSpecies+nDim+1][iDim] += kve*GV[TVE_INDEX][iDim];
  }

  for (iVar = 0; iVar < nVar; iVar++) {
    for (iDim = 0; iDim < nDim; iDim++) {
      Proj_Flux_Tensor[iVar] += Flux_Tensor[iVar][iDim]*val_normal[iDim];
    }
  }
}

void CAvgGradCorrected_TNE2::GetViscousProjJacs(su2double *val_Mean_PrimVar,
                                                su2double **val_Mean_GradPrimVar,
                                                su2double *val_Mean_Eve,
                                                su2double *val_Mean_Cvve,
                                                su2double *val_diffusion_coeff,
                                                su2double val_laminar_viscosity,
                                                su2double val_thermal_conductivity,
                                                su2double val_thermal_conductivity_ve,
                                                su2double val_dist_ij, su2double *val_normal,
                                                su2double val_dS, su2double *val_Fv,
                                                su2double **val_Jac_i, su2double **val_Jac_j,
                                                CConfig *config) {

  bool ionization;
  unsigned short iDim, iSpecies, jSpecies, iVar, jVar, kVar, nHeavy, nEl;
  su2double rho, rho_i, rho_j, vel[3], T, Tve, *xi, *Ms;
  su2double mu, ktr, kve, *Ds, dij, Ru, RuSI;
  su2double theta, thetax, thetay, thetaz;
  su2double etax, etay, etaz;
  su2double pix, piy, piz;
  su2double sumY, sumY_i, sumY_j;

  /*--- Initialize arrays ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      dFdVi[iVar][jVar] = 0.0;
      dFdVj[iVar][jVar] = 0.0;
      dVdUi[iVar][jVar] = 0.0;
      dVdUj[iVar][jVar] = 0.0;
    }
  }

  /*--- Initialize the Jacobian matrices ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      val_Jac_i[iVar][jVar] = 0.0;
      val_Jac_j[iVar][jVar] = 0.0;
    }
  }

  /*--- Initialize storage vectors & matrices ---*/
  for (iVar = 0; iVar < nSpecies; iVar++) {
    sumdFdYjh[iVar]   = 0.0;
    sumdFdYjeve[iVar] = 0.0;
    for (jVar = 0; jVar < nSpecies; jVar++) {
      dFdYi[iVar][jVar] = 0.0;
      dFdYj[iVar][jVar] = 0.0;
      dJdr_i[iVar][jVar] = 0.0;
      dJdr_j[iVar][jVar] = 0.0;
    }
  }

  /*--- Assign booleans from CConfig ---*/
  ionization = config->GetIonization();
  if (ionization) { nHeavy = nSpecies-1; nEl = 1; }
  else            { nHeavy = nSpecies;   nEl = 0; }

  /*--- Calculate preliminary geometrical quantities ---*/
  dij = val_dist_ij;
  theta = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    theta += val_normal[iDim]*val_normal[iDim];
  }


  /*--- Rename for convenience ---*/
  rho = val_Mean_PrimVar[RHO_INDEX];
  rho_i = V_i[RHO_INDEX];
  rho_j = V_j[RHO_INDEX];
  T   = val_Mean_PrimVar[T_INDEX];
  Tve = val_Mean_PrimVar[TVE_INDEX];
  Ds  = val_diffusion_coeff;
  mu  = val_laminar_viscosity;
  ktr = val_thermal_conductivity;
  kve = val_thermal_conductivity_ve;
  RuSI= UNIVERSAL_GAS_CONSTANT;
  Ru  = 1000.0*RuSI;
  Ms  = config->GetMolar_Mass();
  xi  = config->GetRotationModes();
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Ys[iSpecies]   = val_Mean_PrimVar[RHOS_INDEX+iSpecies];
    Ys_i[iSpecies] = V_i[RHOS_INDEX+iSpecies]/V_i[RHO_INDEX];
    Ys_j[iSpecies] = V_j[RHOS_INDEX+iSpecies]/V_j[RHO_INDEX];
    hs[iSpecies]   = var->CalcHs(config, T, val_Mean_Eve[iSpecies], iSpecies);
    Cvtr[iSpecies] = (3.0/2.0 + xi[iSpecies]/2.0)*Ru/Ms[iSpecies];
  }
  for (iDim = 0; iDim < nDim; iDim++)
    vel[iDim] = val_Mean_PrimVar[VEL_INDEX+iDim];

  /*--- Calculate useful diffusion parameters ---*/
  // Summation term of the diffusion fluxes
  sumY = 0.0;
  sumY_i = 0.0;
  sumY_j = 0.0;
  for (iSpecies = 0; iSpecies < nHeavy; iSpecies++) {
    sumY_i += Ds[iSpecies]*theta/dij*Ys_i[iSpecies];
    sumY_j += Ds[iSpecies]*theta/dij*Ys_j[iSpecies];
    sumY   += Ds[iSpecies]*theta/dij*(Ys_j[iSpecies]-Ys_i[iSpecies]);
  }


  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    for (jSpecies  = 0; jSpecies < nSpecies; jSpecies++) {

      // first term
      dJdr_j[iSpecies][jSpecies] +=  0.5*(Ds[iSpecies]*theta/dij *
                                          (Ys_j[iSpecies]*rho_i/rho_j +
                                           Ys_i[iSpecies]));
      dJdr_i[iSpecies][jSpecies] += -0.5*(Ds[iSpecies]*theta/dij *
                                          (Ys_j[iSpecies] +
                                           Ys_i[iSpecies]*rho_j/rho_i));

      // second term
      dJdr_j[iSpecies][jSpecies] +=
          0.25*(Ys_i[iSpecies] - rho_i/rho_j*Ys_j[iSpecies])*sumY
          + 0.25*(Ys_i[iSpecies]+Ys_j[iSpecies])*(rho_i+rho_j)*Ds[jSpecies]*theta/(dij*rho_j)
          - 0.25*(Ys_i[iSpecies]+Ys_j[iSpecies])*(rho_i+rho_j)*sumY_j/rho_j;

      dJdr_i[iSpecies][jSpecies] +=
          0.25*(-rho_j/rho_i*Ys_i[iSpecies]+Ys_j[iSpecies])*sumY
          - 0.25*(Ys_i[iSpecies]+Ys_j[iSpecies])*(rho_i+rho_j)*Ds[jSpecies]*theta/(dij*rho_i)
          + 0.25*(Ys_i[iSpecies]+Ys_j[iSpecies])*(rho_i+rho_j)*sumY_i/rho_i;
    }

    // first term
    dJdr_j[iSpecies][iSpecies] += -0.5*Ds[iSpecies]*theta/dij*(1+rho_i/rho_j);
    dJdr_i[iSpecies][iSpecies] +=  0.5*Ds[iSpecies]*theta/dij*(1+rho_j/rho_i);

    // second term
    dJdr_j[iSpecies][iSpecies] += 0.25*(1.0+rho_i/rho_j)*sumY;
    dJdr_i[iSpecies][iSpecies] += 0.25*(1.0+rho_j/rho_i)*sumY;
  }

  /*--- Calculate transformation matrix ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    dVdUi[iSpecies][iSpecies] = 1.0;
    dVdUj[iSpecies][iSpecies] = 1.0;
  }
  for (iDim = 0; iDim < nDim; iDim++) {
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      dVdUi[nSpecies+iDim][iSpecies] = -V_i[VEL_INDEX+iDim]/V_i[RHO_INDEX];
      dVdUj[nSpecies+iDim][iSpecies] = -V_j[VEL_INDEX+iDim]/V_j[RHO_INDEX];
    }
    dVdUi[nSpecies+iDim][nSpecies+iDim] = 1.0/V_i[RHO_INDEX];
    dVdUj[nSpecies+iDim][nSpecies+iDim] = 1.0/V_j[RHO_INDEX];
  }
  for (iVar = 0; iVar < nVar; iVar++) {
    dVdUi[nSpecies+nDim][iVar]   = dTdU_i[iVar];
    dVdUj[nSpecies+nDim][iVar]   = dTdU_j[iVar];
    dVdUi[nSpecies+nDim+1][iVar] = dTvedU_i[iVar];
    dVdUj[nSpecies+nDim+1][iVar] = dTvedU_j[iVar];
  }


  if (nDim == 2) {

    /*--- Geometry parameters ---*/
    thetax = theta + val_normal[0]*val_normal[0]/3.0;
    thetay = theta + val_normal[1]*val_normal[1]/3.0;
    etaz   = val_normal[0]*val_normal[1]/3.0;
    pix    = mu/dij * (thetax*vel[0] + etaz*vel[1]  );
    piy    = mu/dij * (etaz*vel[0]   + thetay*vel[1]);

    /*--- Populate primitive Jacobian ---*/

    // X-momentum
    dFdVj[nSpecies][nSpecies]     = mu*thetax/dij*val_dS;
    dFdVj[nSpecies][nSpecies+1]   = mu*etaz/dij*val_dS;

    // Y-momentum
    dFdVj[nSpecies+1][nSpecies]   = mu*etaz/dij*val_dS;
    dFdVj[nSpecies+1][nSpecies+1] = mu*thetay/dij*val_dS;

    // Energy
    dFdVj[nSpecies+2][nSpecies]   = pix*val_dS;
    dFdVj[nSpecies+2][nSpecies+1] = piy*val_dS;
    dFdVj[nSpecies+2][nSpecies+2] = ktr*theta/dij*val_dS;
    dFdVj[nSpecies+2][nSpecies+3] = kve*theta/dij*val_dS;

    // Vib-el Energy
    dFdVj[nSpecies+3][nSpecies+3] = kve*theta/dij*val_dS;

    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        dFdVi[iVar][jVar] = -dFdVj[iVar][jVar];

    // Common terms
    dFdVi[nSpecies+2][nSpecies]   += 0.5*val_Fv[nSpecies];
    dFdVj[nSpecies+2][nSpecies]   += 0.5*val_Fv[nSpecies];
    dFdVi[nSpecies+2][nSpecies+1] += 0.5*val_Fv[nSpecies+1];
    dFdVj[nSpecies+2][nSpecies+1] += 0.5*val_Fv[nSpecies+1];
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      dFdVi[nSpecies+2][nSpecies+2] += 0.5*val_Fv[iSpecies]*(Ru/Ms[iSpecies] +
                                                             Cvtr[iSpecies]   );
      dFdVj[nSpecies+2][nSpecies+2] += 0.5*val_Fv[iSpecies]*(Ru/Ms[iSpecies] +
                                                             Cvtr[iSpecies]   );
      dFdVi[nSpecies+2][nSpecies+3] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVj[nSpecies+2][nSpecies+3] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVi[nSpecies+3][nSpecies+3] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVj[nSpecies+3][nSpecies+3] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
    }

    // Unique terms
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
        dFdVj[iSpecies][jSpecies]   += -dJdr_j[iSpecies][jSpecies]*val_dS;
        dFdVi[iSpecies][jSpecies]   += -dJdr_i[iSpecies][jSpecies]*val_dS;
        dFdVj[nSpecies+2][iSpecies] += -dJdr_j[jSpecies][iSpecies]*hs[jSpecies]*val_dS;
        dFdVi[nSpecies+2][iSpecies] += -dJdr_i[jSpecies][iSpecies]*hs[jSpecies]*val_dS;
        dFdVj[nSpecies+3][iSpecies] += -dJdr_j[jSpecies][iSpecies]*val_Mean_Eve[jSpecies]*val_dS;
        dFdVi[nSpecies+3][iSpecies] += -dJdr_i[jSpecies][iSpecies]*val_Mean_Eve[jSpecies]*val_dS;
      }
    }

  } //nDim == 2
  else {

    /*--- Geometry parameters ---*/
    thetax = theta + val_normal[0]*val_normal[0]/3.0;
    thetay = theta + val_normal[1]*val_normal[1]/3.0;
    thetaz = theta + val_normal[2]*val_normal[2]/3.0;
    etax   = val_normal[1]*val_normal[2]/3.0;
    etay   = val_normal[0]*val_normal[2]/3.0;
    etaz   = val_normal[0]*val_normal[1]/3.0;
    pix    = mu/dij * (thetax*vel[0] + etaz*vel[1]   + etay*vel[2]  );
    piy    = mu/dij * (etaz*vel[0]   + thetay*vel[1] + etax*vel[2]  );
    piz    = mu/dij * (etay*vel[0]   + etax*vel[1]   + thetaz*vel[2]);

    /*--- Populate primitive Jacobian ---*/

    // X-momentum
    dFdVj[nSpecies][nSpecies]     = mu*thetax/dij*val_dS;
    dFdVj[nSpecies][nSpecies+1]   = mu*etaz/dij*val_dS;
    dFdVj[nSpecies][nSpecies+2]   = mu*etay/dij*val_dS;

    // Y-momentum
    dFdVj[nSpecies+1][nSpecies]   = mu*etaz/dij*val_dS;
    dFdVj[nSpecies+1][nSpecies+1] = mu*thetay/dij*val_dS;
    dFdVj[nSpecies+1][nSpecies+2] = mu*etax/dij*val_dS;

    // Z-momentum
    dFdVj[nSpecies+2][nSpecies]   = mu*etay/dij*val_dS;
    dFdVj[nSpecies+2][nSpecies+1] = mu*etax/dij*val_dS;
    dFdVj[nSpecies+2][nSpecies+2] = mu*thetaz/dij*val_dS;

    // Energy
    dFdVj[nSpecies+3][nSpecies]   = pix*val_dS;
    dFdVj[nSpecies+3][nSpecies+1] = piy*val_dS;
    dFdVj[nSpecies+3][nSpecies+2] = piz*val_dS;
    dFdVj[nSpecies+3][nSpecies+3] = ktr*theta/dij*val_dS;
    dFdVj[nSpecies+3][nSpecies+4] = kve*theta/dij*val_dS;

    // Vib.-el energy
    dFdVj[nSpecies+4][nSpecies+4] = kve*theta/dij*val_dS;

    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        dFdVi[iVar][jVar] = -dFdVj[iVar][jVar];

    // Common terms
    for (iDim = 0; iDim < nDim; iDim++) {
      dFdVi[nSpecies+3][nSpecies+iDim]   += 0.5*val_Fv[nSpecies+iDim];
      dFdVj[nSpecies+3][nSpecies+iDim]   += 0.5*val_Fv[nSpecies+iDim];
    }
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      dFdVi[nSpecies+3][nSpecies+3] += 0.5*val_Fv[iSpecies]*(Ru/Ms[iSpecies] +
                                                             Cvtr[iSpecies]   );
      dFdVj[nSpecies+3][nSpecies+3] += 0.5*val_Fv[iSpecies]*(Ru/Ms[iSpecies] +
                                                             Cvtr[iSpecies]   );
      dFdVi[nSpecies+3][nSpecies+4] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVj[nSpecies+3][nSpecies+4] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVi[nSpecies+4][nSpecies+4] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
      dFdVj[nSpecies+4][nSpecies+4] += 0.5*val_Fv[iSpecies]*val_Mean_Cvve[iSpecies];
    }

    // Unique terms
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
        dFdVj[iSpecies][jSpecies]   += -dJdr_j[iSpecies][jSpecies]*val_dS;
        dFdVi[iSpecies][jSpecies]   += -dJdr_i[iSpecies][jSpecies]*val_dS;
        dFdVj[nSpecies+3][iSpecies] += -dJdr_j[jSpecies][iSpecies]*hs[jSpecies]*val_dS;
        dFdVi[nSpecies+3][iSpecies] += -dJdr_i[jSpecies][iSpecies]*hs[jSpecies]*val_dS;
        dFdVj[nSpecies+4][iSpecies] += -dJdr_j[jSpecies][iSpecies]*val_Mean_Eve[jSpecies]*val_dS;
        dFdVi[nSpecies+4][iSpecies] += -dJdr_i[jSpecies][iSpecies]*val_Mean_Eve[jSpecies]*val_dS;
      }
    }

  } // nDim == 3

  /*--- dFv/dUij = dFv/dVij * dVij/dUij ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    for (jVar = 0; jVar < nVar; jVar++)
      for (kVar = 0; kVar < nVar; kVar++) {
        val_Jac_i[iVar][jVar] += dFdVi[iVar][kVar]*dVdUi[kVar][jVar];
        val_Jac_j[iVar][jVar] += dFdVj[iVar][kVar]*dVdUj[kVar][jVar];
      }
}

void CAvgGradCorrected_TNE2::ComputeResidual(su2double *val_residual,
                                             su2double **val_Jacobian_i,
                                             su2double **val_Jacobian_j,
                                             CConfig *config) {

  unsigned short iSpecies;
  su2double dist_ij_2;

  /*--- Normalized normal vector ---*/
  Area = 0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);

  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;

  /*--- Compute vector going from iPoint to jPoint ---*/
  dist_ij_2 = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    Edge_Vector[iDim] = Coord_j[iDim]-Coord_i[iDim];
    dist_ij_2 += Edge_Vector[iDim]*Edge_Vector[iDim];
  }

  /*--- Make a local copy of the primitive variables ---*/
  // NOTE: We are transforming the species density terms to species mass fractions
  // Mass fraction
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    PrimVar_i[iSpecies] = V_i[iSpecies]/V_i[RHO_INDEX];
    PrimVar_j[iSpecies] = V_j[iSpecies]/V_j[RHO_INDEX];
    Mean_PrimVar[iSpecies] = 0.5*(PrimVar_i[iSpecies] + PrimVar_j[iSpecies]);
    for (iDim = 0; iDim < nDim; iDim++) {
      Mean_GradPrimVar[iSpecies][iDim] = 0.5*(1.0/V_i[RHO_INDEX] *
                                              (PrimVar_Grad_i[iSpecies][iDim] -
                                               PrimVar_i[iSpecies] *
                                               PrimVar_Grad_i[RHO_INDEX][iDim]) +
                                              1.0/V_j[RHO_INDEX] *
                                              (PrimVar_Grad_j[iSpecies][iDim] -
                                               PrimVar_j[iSpecies] *
                                               PrimVar_Grad_j[RHO_INDEX][iDim]));

    }
  }
  for (iVar = nSpecies; iVar < nPrimVar; iVar++) {
    PrimVar_i[iVar] = V_i[iVar];
    PrimVar_j[iVar] = V_j[iVar];
    Mean_PrimVar[iVar] = 0.5*(PrimVar_i[iVar]+PrimVar_j[iVar]);
  }
  for (iVar = nSpecies; iVar < nPrimVarGrad; iVar++) {
    for (iDim = 0; iDim < nDim; iDim++) {
      Mean_GradPrimVar[iVar][iDim] = 0.5*(PrimVar_Grad_i[iVar][iDim] +
                                          PrimVar_Grad_j[iVar][iDim]);
    }
  }

  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Mean_Eve[iSpecies] = 0.5*(eve_i[iSpecies] + eve_j[iSpecies]);
    Mean_Cvve[iSpecies] = 0.5*(Cvve_i[iSpecies] + Cvve_j[iSpecies]);
  }

  /*--- Mean transport coefficients ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    Mean_Diffusion_Coeff[iSpecies] = 0.5*(Diffusion_Coeff_i[iSpecies] +
                                          Diffusion_Coeff_j[iSpecies]);
  Mean_Laminar_Viscosity           = 0.5*(Laminar_Viscosity_i +
                                          Laminar_Viscosity_j);
  Mean_Thermal_Conductivity        = 0.5*(Thermal_Conductivity_i +
                                          Thermal_Conductivity_j);
  Mean_Thermal_Conductivity_ve     = 0.5*(Thermal_Conductivity_ve_i +
                                          Thermal_Conductivity_ve_j);


  /*--- Projection of the mean gradient in the direction of the edge ---*/
  for (iVar = 0; iVar < nPrimVarGrad; iVar++) {
    Proj_Mean_GradPrimVar_Edge[iVar] = 0.0;
    for (iDim = 0; iDim < nDim; iDim++)
      Proj_Mean_GradPrimVar_Edge[iVar] += Mean_GradPrimVar[iVar][iDim]*Edge_Vector[iDim];
    for (iDim = 0; iDim < nDim; iDim++) {
      Mean_GradPrimVar[iVar][iDim] -= (Proj_Mean_GradPrimVar_Edge[iVar] -
                                       (PrimVar_j[iVar]-PrimVar_i[iVar]))*Edge_Vector[iDim] / dist_ij_2;
    }
  }

  /*--- Get projected flux tensor ---*/
  GetViscousProjFlux(Mean_PrimVar, Mean_GradPrimVar, Mean_Eve,
                     Normal, Mean_Diffusion_Coeff,
                     Mean_Laminar_Viscosity,
                     Mean_Thermal_Conductivity,
                     Mean_Thermal_Conductivity_ve,
                     config);

  /*--- Update viscous residual ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    val_residual[iVar] = Proj_Flux_Tensor[iVar];

  /*--- Compute the implicit part ---*/
  if (implicit) {
    dist_ij = 0.0;
    for (iDim = 0; iDim < nDim; iDim++)
      dist_ij += (Coord_j[iDim]-Coord_i[iDim])*(Coord_j[iDim]-Coord_i[iDim]);
    dist_ij = sqrt(dist_ij);

    GetViscousProjJacs(Mean_PrimVar, Mean_GradPrimVar, Mean_Eve, Mean_Cvve,
                       Mean_Diffusion_Coeff, Mean_Laminar_Viscosity,
                       Mean_Thermal_Conductivity, Mean_Thermal_Conductivity_ve,
                       dist_ij, UnitNormal, Area, Proj_Flux_Tensor,
                       val_Jacobian_i, val_Jacobian_j, config);

  }
}

CSource_TNE2::CSource_TNE2(unsigned short val_nDim,
                           unsigned short val_nVar,
                           unsigned short val_nPrimVar,
                           unsigned short val_nPrimVarGrad,
                           CConfig *config) : CNumerics(val_nDim,
                                                        val_nVar,
                                                        config) {

  unsigned short iVar, iSpecies;

  /*--- Assign booleans from CConfig ---*/
  implicit   = config->GetKind_TimeIntScheme_TNE2() == EULER_IMPLICIT;
  ionization = config->GetIonization();

  /*--- Define useful constants ---*/
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();

  /*--- Allocate arrays ---*/
  RxnConstantTable = new su2double*[6];
  for (iVar = 0; iVar < 6; iVar++)
    RxnConstantTable[iVar] = new su2double[5];

  tau_sr = new su2double*[nSpecies];
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    tau_sr[iSpecies] = new su2double[nSpecies];

  alphak = new int[nSpecies];
  betak  = new int[nSpecies];
  A      = new su2double[5];
  X      = new su2double[nSpecies];
  Y      = new su2double[nSpecies];
  estar  = new su2double[nSpecies];
  evib   = new su2double[nSpecies];
  Cvvs   = new su2double[nSpecies];
  Cves   = new su2double[nSpecies];
  Cvvsst = new su2double[nSpecies];
  tauP   = new su2double[nSpecies];
  tauMW  = new su2double[nSpecies];
  taus   = new su2double[nSpecies];
  dkf    = new su2double[nVar];
  dkb    = new su2double[nVar];
  dRfok  = new su2double[nVar];
  dRbok  = new su2double[nVar];

  dYdr = new su2double*[nSpecies];
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    dYdr[iSpecies] = new su2double[nSpecies];
  }
}

CSource_TNE2::~CSource_TNE2(void) {
  unsigned short iVar, iSpecies;

  /*--- Deallocate arrays ---*/

  for (iVar = 0; iVar < 6; iVar++)
    delete [] RxnConstantTable[iVar];
  delete [] RxnConstantTable;

  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    delete [] dYdr[iSpecies];
  delete [] dYdr;

  for (iSpecies = 0;iSpecies < nSpecies; iSpecies++)
    delete [] tau_sr[iSpecies];
  delete [] tau_sr;

  delete [] A;
  delete [] X;
  delete [] Y;
  delete [] estar;
  delete [] evib;
  delete [] Cvvs;
  delete [] Cves;
  delete [] Cvvsst;
  delete [] tauP;
  delete [] tauMW;
  delete [] taus;
  delete [] alphak;
  delete [] betak;
  delete [] dkf;
  delete [] dkb;
  delete [] dRfok;
  delete [] dRbok;

}

void CSource_TNE2::GetKeqConstants(su2double *A, unsigned short val_Reaction,
                                   CConfig *config) {
  unsigned short ii, iSpecies, iIndex, tbl_offset, pwr;
  su2double N;
  su2double *Ms;
  su2double tmp1, tmp2;
  /*--- Acquire database constants from CConfig ---*/
  Ms = config->GetMolar_Mass();
  config->GetChemistryEquilConstants(RxnConstantTable, val_Reaction);

  /*--- Calculate mixture number density ---*/
  N = 0.0;
  for (iSpecies =0 ; iSpecies < nSpecies; iSpecies++) {
    N += V_i[iSpecies]/Ms[iSpecies]*AVOGAD_CONSTANT;
  }

  /*--- Convert number density from 1/m^3 to 1/cm^3 for table look-up ---*/
  N = N*(1E-6);

  /*--- Determine table index based on mixture N ---*/
  tbl_offset = 14;
  pwr        = floor(log10(N));

  /*--- Bound the interpolation to table limit values ---*/
  iIndex = pwr - tbl_offset;
  if (iIndex <= 0) {
    for (ii = 0; ii < 5; ii++)
      A[ii] = RxnConstantTable[0][ii];
    return;
  } else if (iIndex >= 5) {
    for (ii = 0; ii < 5; ii++)
      A[ii] = RxnConstantTable[5][ii];
    return;
  }

  /*--- Calculate interpolation denominator terms avoiding pow() ---*/
  tmp1 = 1.0;
  tmp2 = 1.0;
  for (ii = 0; ii < pwr; ii++) {
    tmp1 *= 10.0;
    tmp2 *= 10.0;
  }
  tmp2 *= 10.0;

  /*--- Interpolate ---*/
  for (ii = 0; ii < 5; ii++) {
    A[ii] =  (RxnConstantTable[iIndex+1][ii] - RxnConstantTable[iIndex][ii])
        / (tmp2 - tmp1) * (N - tmp1)
        + RxnConstantTable[iIndex][ii];
  }
  return;
}

void CSource_TNE2::ComputeChemistry(su2double *val_residual,
                                    su2double *val_source,
                                    su2double **val_Jacobian_i,
                                    CConfig *config) {

  /*--- Nonequilibrium chemistry ---*/
  unsigned short iSpecies, jSpecies, ii, iReaction, nReactions, iVar, jVar;
  unsigned short nHeavy, nEl, nEve;
  int ***RxnMap;
  su2double T_min, epsilon;
  su2double T, Tve, Thf, Thb, Trxnf, Trxnb, Keq, Cf, eta, theta, kf, kb, kfb;
  su2double rho, rhoCvtr, rhoCvve, P;
  su2double *Ms, fwdRxn, bkwRxn, alpha, RuSI, Ru;
  su2double *Tcf_a, *Tcf_b, *Tcb_a, *Tcb_b;
  su2double *hf, *Tref, *xi;
  su2double af, bf, ab, bb, coeff;
  su2double dThf, dThb;

  /*--- Initialize residual and Jacobian arrays ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_residual[iVar] = 0.0;
  }
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        val_Jacobian_i[iVar][jVar] = 0.0;
  }

  /*--- Define artificial chemistry parameters ---*/
  // Note: These parameters artificially increase the rate-controlling reaction
  //       temperature.  This relaxes some of the stiffness in the chemistry
  //       source term.
  T_min   = 800.0;
  epsilon = 80;

  /*--- Define preferential dissociation coefficient ---*/
  alpha = 0.3;

  /*--- Determine the number of heavy particle species ---*/
  if (ionization) { nHeavy = nSpecies-1; nEl = 1; }
  else            { nHeavy = nSpecies;   nEl = 0; }

  /*--- Rename for convenience ---*/
  RuSI    = UNIVERSAL_GAS_CONSTANT;
  Ru      = 1000.0*RuSI;
  rho     = V_i[RHO_INDEX];
  P       = V_i[P_INDEX];
  T       = V_i[T_INDEX];
  Tve     = V_i[TVE_INDEX];
  rhoCvtr = V_i[RHOCVTR_INDEX];
  rhoCvve = V_i[RHOCVVE_INDEX];

  /*--- Acquire parameters from the configuration file ---*/
  nReactions = config->GetnReactions();
  Ms         = config->GetMolar_Mass();
  RxnMap     = config->GetReaction_Map();
  hf         = config->GetEnthalpy_Formation();
  xi         = config->GetRotationModes();
  Tref       = config->GetRefTemperature();
  Tcf_a      = config->GetRxnTcf_a();
  Tcf_b      = config->GetRxnTcf_b();
  Tcb_a      = config->GetRxnTcb_a();
  Tcb_b      = config->GetRxnTcb_b();

  for (iReaction = 0; iReaction < nReactions; iReaction++) {

    /*--- Determine the rate-controlling temperature ---*/
    af = Tcf_a[iReaction];
    bf = Tcf_b[iReaction];
    ab = Tcb_a[iReaction];
    bb = Tcb_b[iReaction];
    Trxnf = pow(T, af)*pow(Tve, bf);
    Trxnb = pow(T, ab)*pow(Tve, bb);

    /*--- Calculate the modified temperature ---*/
    Thf = 0.5 * (Trxnf+T_min + sqrt((Trxnf-T_min)*(Trxnf-T_min)+epsilon*epsilon));
    Thb = 0.5 * (Trxnb+T_min + sqrt((Trxnb-T_min)*(Trxnb-T_min)+epsilon*epsilon));

    /*--- Get the Keq & Arrhenius coefficients ---*/
    GetKeqConstants(A, iReaction, config);
    Cf    = config->GetArrheniusCoeff(iReaction);
    eta   = config->GetArrheniusEta(iReaction);
    theta = config->GetArrheniusTheta(iReaction);

    /*--- Calculate Keq ---*/
    Keq = exp(  A[0]*(Thb/1E4) + A[1] + A[2]*log(1E4/Thb)
        + A[3]*(1E4/Thb) + A[4]*(1E4/Thb)*(1E4/Thb) );

    /*--- Calculate rate coefficients ---*/
    kf  = Cf * exp(eta*log(Thf)) * exp(-theta/Thf);
    kfb = Cf * exp(eta*log(Thb)) * exp(-theta/Thb);
    kb  = kfb / Keq;

    /*--- Determine production & destruction of each species ---*/
    fwdRxn = 1.0;
    bkwRxn = 1.0;
    for (ii = 0; ii < 3; ii++) {

      /*--- Reactants ---*/
      iSpecies = RxnMap[iReaction][0][ii];
      if ( iSpecies != nSpecies) {
        fwdRxn *= 0.001*U_i[iSpecies]/Ms[iSpecies];
      }

      /*--- Products ---*/
      jSpecies = RxnMap[iReaction][1][ii];
      if (jSpecies != nSpecies) {
        bkwRxn *= 0.001*U_i[jSpecies]/Ms[jSpecies];
      }
    }
    fwdRxn = 1000.0 * kf * fwdRxn;
    bkwRxn = 1000.0 * kb * bkwRxn;

    for (ii = 0; ii < 3; ii++) {

      /*--- Products ---*/
      iSpecies = RxnMap[iReaction][1][ii];
      if (iSpecies != nSpecies) {
        val_residual[iSpecies] += Ms[iSpecies] * (fwdRxn-bkwRxn) * Volume;
        val_residual[nSpecies+nDim+1] += Ms[iSpecies] * (fwdRxn-bkwRxn)
            * eve_i[iSpecies] * Volume;
      }

      /*--- Reactants ---*/
      iSpecies = RxnMap[iReaction][0][ii];
      if (iSpecies != nSpecies) {
        val_residual[iSpecies] -= Ms[iSpecies] * (fwdRxn-bkwRxn) * Volume;
        val_residual[nSpecies+nDim+1] -= Ms[iSpecies] * (fwdRxn-bkwRxn)
            * eve_i[iSpecies] * Volume;
      }
    }

    /*---Set source term ---*/
    for (iVar = 0; iVar < nVar; iVar++)
      val_source[iVar] = val_source[iVar]+val_residual[iVar]/Volume;

    if (implicit) {

      /*--- Initializing derivative variables ---*/
      for (iVar = 0; iVar < nVar; iVar++) {
        dkf[iVar] = 0.0;
        dkb[iVar] = 0.0;
        dRfok[iVar] = 0.0;
        dRbok[iVar] = 0.0;
      }
      for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
        alphak[iSpecies] = 0;
        betak[iSpecies]  = 0;
      }

      /*--- Derivative of modified temperature wrt Trxnf ---*/
      dThf = 0.5 * (1.0 + (Trxnf-T_min)/sqrt((Trxnf-T_min)*(Trxnf-T_min)
                                             + epsilon*epsilon          ));
      dThb = 0.5 * (1.0 + (Trxnb-T_min)/sqrt((Trxnb-T_min)*(Trxnb-T_min)
                                             + epsilon*epsilon          ));

      /*--- Fwd rate coefficient derivatives ---*/
      coeff = kf * (eta/Thf+theta/(Thf*Thf)) * dThf;
      for (iVar = 0; iVar < nVar; iVar++) {
        dkf[iVar] = coeff * (  af*Trxnf/T*dTdU_i[iVar]
                               + bf*Trxnf/Tve*dTvedU_i[iVar] );
      }

      /*--- Bkwd rate coefficient derivatives ---*/
      coeff = kb * (eta/Thb+theta/(Thb*Thb)) * dThb;
      for (iVar = 0; iVar < nVar; iVar++) {
        dkb[iVar] = coeff*(  ab*Trxnb/T*dTdU_i[iVar]
                             + bb*Trxnb/Tve*dTvedU_i[iVar])
            - kb*((A[0]*Thb/1E4 - A[2] - A[3]*1E4/Thb
            - 2*A[4]*(1E4/Thb)*(1E4/Thb))/Thb) * dThb * (  ab*Trxnb/T*dTdU_i[iVar]
                                                           + bb*Trxnb/Tve*dTvedU_i[iVar]);
      }

      /*--- Rxn rate derivatives ---*/
      for (ii = 0; ii < 3; ii++) {

        /*--- Products ---*/
        iSpecies = RxnMap[iReaction][1][ii];
        if (iSpecies != nSpecies)
          betak[iSpecies]++;

        /*--- Reactants ---*/
        iSpecies = RxnMap[iReaction][0][ii];
        if (iSpecies != nSpecies)
          alphak[iSpecies]++;
      }

      for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {

        // Fwd
        dRfok[iSpecies] =  0.001*alphak[iSpecies]/Ms[iSpecies]
            * pow(0.001*U_i[iSpecies]/Ms[iSpecies],
                  max(0, alphak[iSpecies]-1)      );
        for (jSpecies = 0; jSpecies < nSpecies; jSpecies++)
          if (jSpecies != iSpecies)
            dRfok[iSpecies] *= pow(0.001*U_i[jSpecies]/Ms[jSpecies],
                                   alphak[jSpecies]                );
        dRfok[iSpecies] *= 1000.0;

        // Bkw
        dRbok[iSpecies] =  0.001*betak[iSpecies]/Ms[iSpecies]
            * pow(0.001*U_i[iSpecies]/Ms[iSpecies],
                  max(0, betak[iSpecies]-1)       );
        for (jSpecies = 0; jSpecies < nSpecies; jSpecies++)
          if (jSpecies != iSpecies)
            dRbok[iSpecies] *= pow(0.001*U_i[jSpecies]/Ms[jSpecies],
                                   betak[jSpecies]                 );
        dRbok[iSpecies] *= 1000.0;
      }

      nEve = nSpecies+nDim+1;
      for (ii = 0; ii < 3; ii++) {

        /*--- Products ---*/
        iSpecies = RxnMap[iReaction][1][ii];
        if (iSpecies != nSpecies) {
          for (iVar = 0; iVar < nVar; iVar++) {
            val_Jacobian_i[iSpecies][iVar] +=
                Ms[iSpecies] * ( dkf[iVar]*(fwdRxn/kf) + kf*dRfok[iVar]
                                 -dkb[iVar]*(bkwRxn/kb) - kb*dRbok[iVar]) * Volume;
            val_Jacobian_i[nEve][iVar] +=
                Ms[iSpecies] * ( dkf[iVar]*(fwdRxn/kf) + kf*dRfok[iVar]
                                 -dkb[iVar]*(bkwRxn/kb) - kb*dRbok[iVar])
                * eve_i[iSpecies] * Volume;
          }

          for (jVar = 0; jVar < nVar; jVar++) {
            val_Jacobian_i[nEve][jVar] += Ms[iSpecies] * (fwdRxn-bkwRxn)
                * Cvve_i[iSpecies] * dTvedU_i[jVar] * Volume;
          }
        }

        /*--- Reactants ---*/
        iSpecies = RxnMap[iReaction][0][ii];
        if (iSpecies != nSpecies) {
          for (iVar = 0; iVar < nVar; iVar++) {
            val_Jacobian_i[iSpecies][iVar] -=
                Ms[iSpecies] * ( dkf[iVar]*(fwdRxn/kf) + kf*dRfok[iVar]
                                 -dkb[iVar]*(bkwRxn/kb) - kb*dRbok[iVar]) * Volume;
            val_Jacobian_i[nEve][iVar] -=
                Ms[iSpecies] * ( dkf[iVar]*(fwdRxn/kf) + kf*dRfok[iVar]
                                 -dkb[iVar]*(bkwRxn/kb) - kb*dRbok[iVar])
                * eve_i[iSpecies] * Volume;

          }

          for (jVar = 0; jVar < nVar; jVar++) {
            val_Jacobian_i[nEve][jVar] -= Ms[iSpecies] * (fwdRxn-bkwRxn)
                * Cvve_i[iSpecies] * dTvedU_i[jVar] * Volume;
          }
        } // != nSpecies
      } // ii
    } // implicit
  } // iReaction
}

void CSource_TNE2::ComputeVibRelaxation(su2double *val_residual,
                                        su2double *val_source,
                                        su2double **val_Jacobian_i,
                                        CConfig *config) {

  /*--- Trans.-rot. & vibrational energy exchange via inelastic collisions ---*/
  // Note: Electronic energy not implemented
  // Note: Landau-Teller formulation
  // Note: Millikan & White relaxation time (requires P in Atm.)
  // Note: Park limiting cross section
  unsigned short iSpecies, jSpecies, iVar, jVar;
  unsigned short nEv, nHeavy, nEl;
  su2double rhos, P, T, Tve, rhoCvtr, rhoCvve, RuSI, Ru, conc, N;
  su2double Qtv, taunum, taudenom;
  su2double mu, A_sr, B_sr, num, denom;
  su2double Cs, sig_s;
  su2double *Ms, *thetav;

  /*--- Initialize residual and Jacobian arrays ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_residual[iVar] = 0.0;
  }
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        val_Jacobian_i[iVar][jVar] = 0.0;
  }

  /*--- Determine the number of heavy particle species ---*/
  if (ionization) { nHeavy = nSpecies-1; nEl = 1; }
  else            { nHeavy = nSpecies;   nEl = 0; }

  /*--- Rename for convenience ---*/
  RuSI    = UNIVERSAL_GAS_CONSTANT;
  Ru      = 1000.0*RuSI;
  P       = V_i[P_INDEX];
  T       = V_i[T_INDEX];
  Tve     = V_i[TVE_INDEX];
  rhoCvtr = V_i[RHOCVTR_INDEX];
  rhoCvve = V_i[RHOCVVE_INDEX];
  nEv     = nSpecies+nDim+1;

  /*--- Read from CConfig ---*/
  Ms        = config->GetMolar_Mass();
  thetav    = config->GetCharVibTemp();

  /*--- Calculate mole fractions ---*/
  N    = 0.0;
  conc = 0.0;
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    conc += V_i[RHOS_INDEX+iSpecies] / Ms[iSpecies];
    N    += V_i[RHOS_INDEX+iSpecies] / Ms[iSpecies] * AVOGAD_CONSTANT;
  }
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    X[iSpecies] = (V_i[RHOS_INDEX+iSpecies] / Ms[iSpecies]) / conc;

  /*--- Loop over species to calculate source term --*/
  Qtv      = 0.0;
  taunum   = 0.0;
  taudenom = 0.0;
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {

    /*--- Rename for convenience ---*/
    rhos   = V_i[RHOS_INDEX+iSpecies];

    /*--- Millikan & White relaxation time ---*/
    num   = 0.0;
    denom = 0.0;
    for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
      mu     = Ms[iSpecies]*Ms[jSpecies] / (Ms[iSpecies] + Ms[jSpecies]);
      A_sr   = 1.16 * 1E-3 * sqrt(mu) * pow(thetav[iSpecies], 4.0/3.0);
      B_sr   = 0.015 * pow(mu, 0.25);
      tau_sr[iSpecies][jSpecies] = 101325.0/P * exp(A_sr*(pow(T,-1.0/3.0) - B_sr) - 18.42);
      num   += X[jSpecies];
      denom += X[jSpecies] / tau_sr[iSpecies][jSpecies];
    }
    tauMW[iSpecies] = num / denom;

    /*--- Park limiting cross section ---*/
    Cs    = sqrt((8.0*Ru*T)/(PI_NUMBER*Ms[iSpecies]));
    sig_s = 1E-20*(5E4*5E4)/(T*T);

    tauP[iSpecies] = 1/(sig_s*Cs*N);

    /*--- Species relaxation time ---*/
    taus[iSpecies] = tauMW[iSpecies] + tauP[iSpecies];

    /*--- Calculate vib.-el. energies ---*/
    estar[iSpecies] = var->CalcEve(config, T, iSpecies);

    /*--- Add species contribution to residual ---*/
    val_residual[nEv] += rhos * (estar[iSpecies] -
                                 eve_i[iSpecies]) / taus[iSpecies] * Volume;
  }

  /*---Set source term ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    val_source[iVar] = val_source[iVar]+val_residual[iVar]/Volume;

  if (implicit) {
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {

      /*--- Rename ---*/
      rhos = V_i[RHOS_INDEX+iSpecies];
      Cvvsst[iSpecies] = var->CalcCvve(T, config, iSpecies);

      for (iVar = 0; iVar < nVar; iVar++) {
        val_Jacobian_i[nEv][iVar] += rhos/taus[iSpecies]*(Cvvsst[iSpecies]*dTdU_i[iVar] -
                                                          Cvve_i[iSpecies]*dTvedU_i[iVar])*Volume;
      }
    }
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
      val_Jacobian_i[nEv][iSpecies] += (estar[iSpecies]-eve_i[iSpecies])/taus[iSpecies]*Volume;

    /*--- Relaxation time derivatives ---*/
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      /*--- tauP terms ---*/
      /*--- (dR/dtau)(dtau/dtauP)(dtauP/dT)(dT/dU) ---*/
      rhos = V_i[RHOS_INDEX+iSpecies];
      for (iVar = 0; iVar < nVar; iVar++) {
        val_Jacobian_i[nEv][iVar] -= rhos*(estar[iSpecies]-eve_i[iSpecies])/(pow(taus[iSpecies], 2.0)) * Volume *
                                      (1.5*PI_NUMBER*Ms[iSpecies]*N)/(1E-20*(5E4*5E4))*sqrt(T)*dTdU_i[iVar];
      }
      /*--- (dR/dtau)(dtau/dtauP)(dtauP/drhos) ---*/
      Cs    = sqrt((8.0*Ru*T)/(PI_NUMBER*Ms[iSpecies]));
      sig_s = 1E-20*(5E4*5E4)/(T*T);
      val_Jacobian_i[nEv][iSpecies] -= rhos*(estar[iSpecies]-eve_i[iSpecies])/(pow(taus[iSpecies], 2.0)) * Volume *
                                       (-1./(Cs*sig_s*N*N*Ms[iSpecies]));

      /*--- tauMW terms ---*/
      num   = 0.0;
      denom = 0.0;
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
        mu     = Ms[iSpecies]*Ms[jSpecies] / (Ms[iSpecies] + Ms[jSpecies]);
        A_sr   = 1.16 * 1E-3 * sqrt(mu) * pow(thetav[iSpecies], 4.0/3.0);
        B_sr   = 0.015 * pow(mu, 0.25);
        tau_sr[iSpecies][jSpecies] = 101325.0/P * exp(A_sr*(pow(T,-1.0/3.0) - B_sr) - 18.42);
        num   += X[jSpecies];
        denom += X[jSpecies] / tau_sr[iSpecies][jSpecies];
      }
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
        const su2double dTauMWdTauSR = num/pow(denom, 2.0)*Ms[jSpecies]/tau_sr[iSpecies][jSpecies];
        const su2double dTauSRdP = -tau_sr[iSpecies][jSpecies]/P;
        const su2double dTauSRdT = -tau_sr[iSpecies][jSpecies]*(1./3.)*A_sr*pow(T, -4./3.);
        for (iVar = 0; iVar < nVar; iVar++) {
          /*--- (dR/dtauMW)(dtau/dtauMW)(dtauMW/dtausp)(dtausp/dP)(dP/dU) ---*/
          val_Jacobian_i[nEv][iSpecies] -= rhos*(estar[iSpecies]-eve_i[iSpecies])/(pow(taus[iSpecies], 2.0)) * Volume *
                                           dTauMWdTauSR*dTauSRdP*dPdU_i[iVar];
          /*--- (dR/dtauMW)(dtau/dtauMW)(dtauMW/dtausp)(dtausp/dT)(dT/dU) ---*/
          val_Jacobian_i[nEv][iSpecies] -= rhos*(estar[iSpecies]-eve_i[iSpecies])/(pow(taus[iSpecies], 2.0)) * Volume *
                                           dTauMWdTauSR*dTauSRdT*dTdU_i[iVar];
        }
      }
    }
  }
}

void CSource_TNE2::ComputeAxisymmetric(su2double *val_residual,
                                       su2double *val_source,
                                       su2double **val_Jacobian,
                                       CConfig *config) {

  unsigned short iDim, iSpecies, jSpecies, iVar, jVar;
  su2double rho, rhou, rhov, rhoEve, vel2, H, yinv;

  /*--- Calculate inverse of y coordinate ---*/
  if (Coord_i[1]!= 0.0) yinv = 1.0/Coord_i[1];
  else yinv = 0.0;

  /*--- Rename for convenience ---*/
  rho    = V_i[RHO_INDEX];
  rhou   = U_i[nSpecies];
  rhov   = U_i[nSpecies+1];
  rhoEve = U_i[nSpecies+nDim+1];
  H      = V_i[H_INDEX];
  vel2   = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    vel2 += V_i[VEL_INDEX+iDim]*V_i[VEL_INDEX+iDim];
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    Y[iSpecies] = V_i[RHOS_INDEX+iSpecies] / rho;

  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    val_residual[iSpecies] = yinv*rhov*Y[iSpecies]*Volume;
  val_residual[nSpecies]   = yinv*rhov*U_i[nSpecies]/rho*Volume;
  val_residual[nSpecies+1] = yinv*rhov*U_i[nSpecies+1]/rho*Volume;
  val_residual[nSpecies+2] = yinv*rhov*H*Volume;
  val_residual[nSpecies+3] = yinv*rhov*U_i[nSpecies+nDim+1]/rho*Volume;

  /*---Set source term ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    val_source[iVar] = val_source[iVar]+val_residual[iVar]/Volume;

  if (implicit) {

    /*--- Initialize ---*/
    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        val_Jacobian[iVar][jVar] = 0.0;
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++)
        dYdr[iSpecies][jSpecies] = 0.0;

    /*--- Calculate additional quantities ---*/
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
        dYdr[iSpecies][jSpecies] += -1/rho*Ys[iSpecies];
      }
      dYdr[iSpecies][iSpecies] += 1/rho;
    }

    /*--- Populate Jacobian ---*/

    // Species density
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      for (jSpecies = 0; jSpecies < nSpecies; jSpecies++) {
        val_Jacobian[iSpecies][jSpecies] = dYdr[iSpecies][jSpecies]*rhov;
      }
      val_Jacobian[iSpecies][nSpecies+1] = Y[iSpecies];
    }

    // X-momentum
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
      val_Jacobian[nSpecies][iSpecies] = -rhou*rhov/(rho*rho);
    val_Jacobian[nSpecies][nSpecies] = rhov/rho;
    val_Jacobian[nSpecies][nSpecies+1] = rhou/rho;

    // Y-momentum
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
      val_Jacobian[nSpecies+1][iSpecies] = -rhov*rhov/(rho*rho);
    val_Jacobian[nSpecies+1][nSpecies+1] = 2*rhov/rho;

    // Energy
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
      val_Jacobian[nSpecies+nDim][iSpecies]      = -H*rhov/rho + dPdU_i[iSpecies]*rhov/rho;
    val_Jacobian[nSpecies+nDim][nSpecies]        = dPdU_i[nSpecies]*rhov/rho;
    val_Jacobian[nSpecies+nDim][nSpecies+1]      = H + dPdU_i[nSpecies+1]*rhov/rho;
    val_Jacobian[nSpecies+nDim][nSpecies+nDim]   = (1+dPdU_i[nSpecies+nDim])*rhov/rho;
    val_Jacobian[nSpecies+nDim][nSpecies+nDim+1] = dPdU_i[nSpecies+nDim+1]*rhov/rho;

    // Vib-el energy
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
      val_Jacobian[nSpecies+nDim+1][iSpecies] = -rhoEve*rhov/(rho*rho);
    val_Jacobian[nSpecies+nDim+1][nSpecies+1] = rhoEve/rho;
    val_Jacobian[nSpecies+nDim+1][nSpecies+nDim+1] = rhov/rho;

    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++)
        val_Jacobian[iVar][jVar] *= yinv*Volume;
  }
}
