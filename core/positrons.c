//******************************************************************************
//*                                                                            *
//* POSITRONS.C                                                                *
//*                                                                            *
//* Tracking electron-positron pairs in accretion disk                         *
//* Written by HS Leon Chan at 2024                                            *
//*                                                                            *
//******************************************************************************

// header files
#include "math.h"
#include "decs.h"
#include "cooling.h"
#include "positrons.h"
#include <gsl/gsl_sf_erf.h>
#include <gsl/gsl_sf_gamma.h>
#include <gsl/gsl_sf_bessel.h>

// compile only if poistrons flag is on //
#if POSITRONS

// some constants //
double i0 = 4.0505;
double i1 = 4.0505*0.40;
double i2 = 4.0505*0.5316;
double i3 = 1.8899;

//******************************************************************************

// set the unit convestion between code and cgs // 
void set_units () {

  /* black hole mass in cgs */ 
  Mbh = mbh*MSUN;

  /* Length and time scales */
  L_unit = GNEWT*Mbh/(CL*CL);
  T_unit = L_unit/CL;

  /* Now set the remaining unit */
  RHO_unit = M_unit*pow(L_unit,-3.0);
  U_unit = RHO_unit*CL*CL;
  
  /* magnetic fields */
  B_unit = CL*sqrt(4.0*M_PI*RHO_unit);

}

//******************************************************************************

//initialize positrons variables
void init_positrons(struct GridGeom *G, struct FluidState *S)
{
  
  // Set positron mass to its floor values //
  ZLOOPALL {
    S->P[RPL][k][j][i] = ZMIN*ME_MP*S->P[RHO][k][j][i];
  }

  // Necessary?  Usually called right afterward
  set_bounds(G, S);

}

//******************************************************************************

// Compute net pair production rate //
void pair_production(struct GridGeom *G, struct FluidState *Ss, struct FluidState *Sf, double dt_step)
{

  /* Then, compute the pair production rate */
#pragma omp parallel for collapse(3)
  ZLOOP {
    pair_production_1zone(G, Ss, Sf, i, j, k, dt_step);
  }

}

//******************************************************************************

// compute pair production rate per grid cells //
inline void pair_production_1zone(struct GridGeom *G, struct FluidState *Ss, struct FluidState *Sf, int i, int j, int k , double dt_step)
{

  /***********************************************************************/

  // coordinate //
  double rad, theta, X[NDIM];

  // coordinate radius //
  coord(i, j, k, CENT, X);
  bl_coord(X, &rad, &theta);

  // get state //
  get_state(G, Ss, i, j, k, CENT);

  /***********************************************************************/
  // number density, all in c.g.s unit //

  // time step in second !
  double dt_real = dt_step*T_unit;

  /***********************************************************************/
  
  // calculate proton and positron numbe density, all in cgs //
  double nprot = Ss->P[RHO][k][j][i]*RHO_unit/MP;
  double npost = Ss->P[RPL][k][j][i]*RHO_unit/ME;
  double ntot = 2*npost + nprot;

  /***********************************************************************/

  // electron temperature //
  double thetae = KBOL*t_elec/(ME*CL*CL); 
 
  // get angular velocity //
  double ang_vel = Ss->ucon[3][k][j][i]/Ss->ucon[0][k][j][i];
  
  // get coulumb coupling energy transfer rate //
  double ue = (2.0*npost + nprot)*KBOL*t_elec/(gam - 1.0);
  double up = Ss->P[UU][k][j][i]*U_unit;
  double thetap = (up*(gam - 1.0)/nprot)/(MP*CL*CL);
  double qcoul = coulomb_onezone(thetap, thetae, nprot, npost, i, j, k); // in cgs
  double tcoul = fmin(fabs(ue/qcoul), fabs(up/qcoul));
  double tomega = 1.0/fabs(ang_vel)*T_unit;
  double tratio = tcoul/tomega;
  
  /***********************************************************************/

  // optical depth and scale height //
  double tau_depth = 0.0;
  double h_th = 0.0;

  // direct integration of optical depth and scale height //
#if COMPUTE == DIRECT_1
  /*--------------------------------------------------------------------------------------------*/
  // local variable storing integrand //
  int m_start, m_end;
  double np, n_p, nt, th_loc;
  double upper = 0.0;
  double lower = 0.0; 
  double dummy;

  // integrate optical depth and scale height //
  // they are all in the C.G.S unit //
  if(theta < M_PI_2) {
    // upper hemisphere //
    m_start = NG;
    m_end = j;
  } else {
    // lower hemispehere
    m_start = j;
    m_end = N2 + NG;
  }

  /* sum over */
  for (int m = m_start; m <= m_end; m++) {
    coord(i, m, k, CENT, X);
    bl_coord(X, &dummy, &th_loc);
    np = Ss->P[RHO][k][m][i]*RHO_unit/MP;
    n_p = Ss->P[RPL][k][m][i]*RHO_unit/ME;
    nt = 2*n_p + np;
    upper = upper + nt*fabs(th_loc - M_PI_2)*G->gdet[CENT][m][i]*sqrt(G->gcov[CENT][2][2][m][i])*dx[2];
    lower = lower + nt*G->gdet[CENT][m][i]*dx[2];
  }

  // scale height, remember change to CGS //
  h_th = upper/lower*L_unit;
  tau_depth = (2.0*npost + nprot)*h_th*sigma_t;

  // direct integration of optical depth and scale height //
#elif COMPUTE == DIRECT_2
  /*--------------------------------------------------------------------------------------------*/
  // local variable storing integrand //
  int m_start, m_end;
  double np, n_p, nt, th_loc;
  double upper = 0.0;
  double lower = 0.0; 
  double dummy;

  // integrate optical depth and scale height //
  // they are all in the C.G.S unit //
  if(theta < M_PI_2) {
    // upper hemisphere //
    m_start = NG;
    m_end = j;
  } else {
    // lower hemispehere
    m_start = j;
    m_end = N2 + NG;
  }

  /* sum over */
  for (int m = m_start; m <= m_end; m++) {
    coord(i, m, k, CENT, X);
    bl_coord(X, &dummy, &th_loc);
    np = Ss->P[RHO][k][m][i]*RHO_unit/MP;
    n_p = Ss->P[RPL][k][m][i]*RHO_unit/ME;
    nt = 2*n_p + np;
    tau_depth = tau_depth + nt*(sqrt(G->gcov[CENT][2][2][m][i])*dx[2])*L_unit*sigma_t;
  }

  // scale height, remember change to CGS //
  h_th = tau_depth/sigma_t/ntot;

#elif COMPUTE == GAUSSIAN
  /*--------------------------------------------------------------------------------------------*/
  // Note: need to find asymtopic

  // local variables //
  int j_mid;
  double np, n_p, nt;
  double t1, t2;
  double dummy;

  // calculate mid plane index //
  j_mid = (int)((NG + N2 + NG)/2);

  // mid plane number density //
  np = Ss->P[RHO][k][j_mid][i]*RHO_unit/MP;
  n_p = Ss->P[RPL][k][j_mid][i]*RHO_unit/ME;
  nt = 2.0*n_p + np;

  // upper atmosphere //
  if(theta < M_PI_2) {
    t1 = - M_PI_2/h_r/sqrt(2);
    t2 = (theta - M_PI_2)/h_r/sqrt(2);
  } else {
    t1 = (theta - M_PI_2)/h_r/sqrt(2);
    t2 = M_PI_2/h_r/sqrt(2);
  }
  if(fabs(t1) > 5.0 && fabs(t2) > 5.0){
    dummy = series_asym(t2) - series_asym(t1);
  } else {
    dummy = gsl_sf_erf(t2) - gsl_sf_erf(t1);
  }
  tau_depth = fabs(nt*(h_r*rad*L_unit)*sqrt(M_PI_2)*dummy*sigma_t);
  tau_depth = fmin(tau_depth ,SMALL);
  h_th = tau_depth/sigma_t/ntot;

  /*--------------------------------------------------------------------------------------------*/
#endif

  /***********************************************************************/

  // postiron fraction //
  double zfrac = npost/nprot; 

  /***********************************************************************/
  /* now calculate pair production rate */

  // magnetic field strength //
  double bsq = bsq_calc(Ss, i, j, k);
  double bfield = sqrt(bsq)*B_unit;

  // net pair production rate, note the rate is in the CGS unit!!! //
  double net_rate = ndot_net(zfrac, tau_depth, nprot, thetae, h_th, bfield);

  /* do these steps only if the production rate is non-zero */
  if(fabs(net_rate) > 0.0) {    

    // quality factor //
    double qfac = fabs(npost/net_rate);

    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */
    /* if the source term is too steep, implement implicit solver 
    /* Basically a root finding, so use bisection mtehod */
    if(dt_real > q_alpha*qfac) {

      /* print out */
      printf("net_rate too steep %d %d %d\n", i, j, k);

      /* left state */
      double zl = zfrac;
      double ndotl = ndot_net(zl, tau_depth, nprot, thetae, h_th, bfield);
      double fl = (zl - zfrac) - dt_real*ndotl/nprot;

      /* right state */
      int o;
      double zr, ndotr, fr, steps;
      if(net_rate > 0.0) {
        steps = 10.0;
      } else{
        steps = 0.1;
      }
      zr = zl;
      for (o = 0; o < 999; o++) {
        ndotr = ndot_net(zr, tau_depth, nprot, thetae, h_th, bfield);
        fr = (zr - zfrac) - dt_real*ndotr/nprot;
        if(fr*fl < 0.0) break;
        zr = zr*steps;
      }

      /* exit condition */
      if(o == 999 || isnan(fr)) {
        printf("Failure in implicit method\n");
        exit(0);
        return;
      }

      /* define the center state */
      double zcen, fcen, zcen_old, ndotcen;

      /* bisection method counting */
      int count;

      /* now iterate until converges */
      for (count = 0; count < 99999; count++) {

        /* backup */
        if(count > 0) {
          zcen_old = zcen;
        }

        /* center state */
        zcen = 0.5*(zl + zr);
        ndotcen = ndot_net(zcen, tau_depth, nprot, thetae, h_th, bfield);
        fcen = (zcen - zfrac) - dt_real*ndotcen/nprot;
        
        /* check the sign */
        if (fl*fcen > 0.0) {
          zl = zcen;
        } else if (fr*fcen > 0.0) {
          zr = zcen;
        }

        /* determine if need to exit */
        if(count > 0) {
          if(fabs(1.0 - zcen_old/zcen) < bisects) {
            break;
          }
        }
      }

      /* exit if no solution, and print out error */
      if(count == 99999) {
        printf("No solution\n");
        return;
      }

      /* assign new positron mass, remember to convert back to code unit !!! */
      npost = zcen*nprot;
      
    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */
    /* otherwise, march forward by time */
    } else {

      // positron mass production rate, need to convert to code unit!!! //
      npost = npost + net_rate*dt_real;
    
    }
    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */

    // update positron mass //
    Sf->P[RPL][k][j][i] = npost*(ME/RHO_unit);
  }
}

//*------------------------------------------------------------------------------------------------------------------------*//
//
// Now, the remaining of the code are all about computing pair production rates
// I use a long comment to seperate out the main body of the this code for 
// better documentation and reading
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//******************************************************************************

/* net pair production rate */
inline double ndot_net(double zfrac, double taut, double nprot, double theta, double r_size, double bfield) {
  double xm = find_xm(zfrac, taut, nprot, theta);
  double ndotbr = get_ndotbr(zfrac, theta, xm, nprot);
  double y1 = comptony1(xm, taut, theta);
  double fb = fbrem(y1, taut, theta, xm);
  double n1 = flatn1(xm, theta, y1);
  double fs, ndots;
  find_ndots(theta, taut, nprot, zfrac, r_size, bfield, &fs, &ndots);
  double ng = ngamma(taut, theta, fb, ndotbr, fs, ndots, r_size);
  double nc = ncdot(ng, theta, nprot, zfrac, n1);
  double na = nadot(zfrac, nprot, theta);
  return nc - na;
}

//******************************************************************************

/* Total pair production rate due to photon-photo, photon-particle collision */
inline double ncdot(double ngamma, double theta, double nprot, double z, double n1) {
  double ndotww = get_ndotww(ngamma, theta);
  double ndotwp = get_ndotwp(ngamma, nprot, theta);
  double ndotwe = get_ndotwe(ngamma, nprot, z, theta);
  double ndotwf = get_ndotwf(n1, ngamma, theta);
  double ndotee = get_ndotee(nprot, z, theta);
  double out = ndotee + ndotww + ndotwp + ndotwe + ndotwf ;
  return out;
}

//******************************************************************************

// functions for computing e-e pair production rates //
inline double get_ndotee(double nprot, double z, double theta) {
  double ndot;
  if(theta <= 1.0e2) {
    ndot = 2.0e-4*pow(theta,1.5)*exp(-2.0/theta)*(1.0+0.015*theta);
  } else {
    ndot = (112.0/27.0/M_PI)*(alphaf*alphaf)*pow(log(theta),3.0)/(1.0 + 0.058/theta);
  }
  ndot = ndot*CL*RE*RE*(nprot*(1.0 + z))*(nprot*(1.0 + z));
  return ndot;
}

//******************************************************************************

/* This is for computing the pair annhilation rate */
inline double nadot(double z, double nprot, double theta) {
  double out = (3.0/8.0)*sigma_t*CL*(nprot*nprot*z*(z+1.0))/(1.0+2.0*theta*theta/log(1.12*theta+1.3));
  return out;
}

//*------------------------------------------------------------------------------------------------------------------------*//
//
// These sections are the analytic formula for computing photon-photon or photon-particle processes
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//******************************************************************************

/* wein - wein, Svensson 1984, White & Lightman 1989 */
inline double get_ndotww(double ngamma, double theta) {
  double out;
  if(theta <= 1.0) {
    out = (0.125)*M_PI*M_PI*exp(-2.0/theta)*(1.0 + 2.88*pow(theta,0.934))/pow(theta,3.0);
  } else {
    out = (0.5)*M_PI*log(2.0*eta*theta + 0.38)/pow(theta,2.0);
  }
  out = out*CL*RE*RE*ngamma*ngamma;
  return out;
}

//******************************************************************************

/* wein - proton, Svensson 1984, White & Lightman 1989 */
inline double get_ndotwp(double ngamma, double nprot, double theta) {
  double out;
  if(theta <= 2.0) {
    out = M_PI*theta*exp(-2.0/theta)/(1.0 + 0.9*theta);
  } else {
    out = (28.0/9.0)*log(2.0*eta*theta + 1.7) - 92.0/27.0;
  }
  out = out*alphaf*CL*RE*RE*ngamma*nprot;
  return out;
}

//******************************************************************************

/* wein - lepton, Svensson 1984, White & Lightman 1989 */
inline double get_ndotwe(double ngamma, double nprot, double z, double theta) {
  double out;
  if(theta <= 0.18) {
    out = (4.0*M_PI/27.0)*exp(-2.0/theta)*(1.0 + 27.1*pow(theta,0.949));
  } else if(theta >= 2) {
    out = (56.0/9.0*log(2.0*eta*theta) - 8.0/27.0)/(1.0 + 0.5/theta);
  } else {
    out = (4.0*M_PI/27.0)*exp(-2.0/theta)*16.1*pow(theta,0.541);
  }
  out = out*alphaf*CL*RE*RE*ngamma*(2.0*z+1.0)*nprot;
  return out;
}

//******************************************************************************

/* wein - flat, Svensson 1984, White & Lightman 1989 */
inline double get_ndotwf(double n1, double ngamma, double theta) {
  double out;
  out = CL*RE*RE*n1*ngamma*M_PI*M_PI/4.0*exp(-1.0/theta);
  return out;
}

//*------------------------------------------------------------------------------------------------------------------------*//
//
// These sections are the analytic formula for computing total photon emissivity
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//******************************************************************************

/* electron-proton bremsstrahlung total rate */
inline double rate_ep(double z, double nprot, double theta, double xm) {
  double out = (1.0+2.0*z)*(1.0+2.0*theta+2.0*theta*theta)*log(4.0*eta*(1.0+3.42*theta)*sqrt(theta/xm));
  return out;
}

//******************************************************************************

/* electron-electron bremsstrahlung */
inline double rate_ee(double z, double nprot, double theta, double xm) {
  double out = (z*z+(1.0+z)*(1.0+z))*(3.0*sqrt2/5.0*theta+2.0*theta*theta)*log(4.0*eta*(11.2+10.4*theta*theta)*sqrt(theta/xm));
  return out;
}

//******************************************************************************

/* electron-positron bremsstrahlung */
inline double rate_pm(double z, double nprot, double theta, double xm) {
  double out = z*(1.0+z)*2*(sqrt2+2.0*theta+2.0*theta*theta)*log(4.0*eta*(1.0+10.4*theta*theta)*sqrt(theta/xm));
  return out; 
}

//******************************************************************************

/* bremsstrahlung production rate */
inline double get_ndotbr(double z, double theta, double xm, double nprot) {
  double thetam1 = 1.0/theta;
  double corr;
  if(thetam1 < 500.0) {
    corr = exp(thetam1)*gsl_sf_bessel_Kn(2, thetam1);
  } else {
    corr = sqrt(M_PI_2/thetam1);
  }
  double factor = (16.0/3.0)*(alphaf)*(CL)*(RE*RE)*(nprot*nprot)/(corr)*log(theta/xm);
  double ep = rate_ep(z, nprot, theta, xm);
  double ee = rate_ee(z, nprot, theta, xm);
  double pm = rate_pm(z, nprot, theta, xm);
  double out = factor*(ep + ee + pm);
  return out;
}

//*------------------------------------------------------------------------------------------------------------------------*//
//
// These sections are the analytic formula for computing photon emissivity per frequency
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//******************************************************************************

/* black body number density */ 
inline double nbb(double x, double theta) {
  double tmp = x/theta;
  double out;
  if(tmp < 1.0e-5) {
    out = (x*theta)/(lambda_c*lambda_c*lambda_c)/(M_PI*M_PI);
  } else {
    out = (x*x)/(lambda_c*lambda_c*lambda_c)/(M_PI*M_PI)/(exp(tmp) - 1.0);
  }
  return out;
}

//******************************************************************************

/* d(n0)/dt factor */
inline double n0dot(double x, double nprot, double theta) {
  double thetam1 = 1.0/theta;
  double corr;
  if(thetam1 < 500.0) {
    corr = exp(thetam1)*gsl_sf_bessel_Kn(2, thetam1);
  } else {
    corr = sqrt(M_PI_2/thetam1);
  }
  double out = (16.0/3.0)*(alphaf)*(CL)*(RE*RE)*(nprot*nprot)/(corr)*(exp(-x*thetam1)/x);
  return out;
}

//******************************************************************************

/* electron-proton bremsstrahlung */
inline double ndotep(double x, double z, double nprot, double theta) {
  double out = (1.0+2.0*z)*log(4.0*eta*(1.0+3.42*theta)*(theta/x))*(1.0+2.0*theta+2.0*theta*theta);
  return out;
}

//******************************************************************************

/* electron-electron bremsstrahlung */
inline double ndotee(double x, double z, double nprot, double theta) {
  double out = (z*z+(1.0+z)*(1.0+z))*log(4.0*eta*(11.2+10.4*theta*theta)*(theta/x))*(3.0*sqrt2/5.0*theta+2.0*theta*theta);
  return out;
}

//******************************************************************************

/* electron-positron bremsstrahlung */
inline double ndotpm(double x, double z, double nprot, double theta) {
  double out = z*(1.0+z)*log(4.0*eta*(1.0+10.4*theta*theta)*(theta/x))*2.0*(sqrt2+2.0*theta+2.0*theta*theta);
  return out;
}

//******************************************************************************

/* bremsstrahlung absorbtion coefficient */
inline double brem_abs(double x, double z, double nprot, double theta) {
  double bb = nbb(x, theta);
  double dn0dt = n0dot(x, nprot, theta);
  double ep = ndotep(x, z, nprot, theta);
  double ee = ndotee(x, z, nprot, theta);
  double pm = ndotpm(x, z, nprot, theta);
  double out = dn0dt*(ep + ee + pm)/(CL*bb);
  return out;
}

//*------------------------------------------------------------------------------------------------------------------------*//
//
// These sections related to the synchroton emission 
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//******************************************************************************

/* root finding for xs */
inline double ix(double x, double A_in) {
  double out = (i0/pow(x,1.0/6.0) + i1/pow(x,5.0/12.0) + i2/pow(x,2.0/3.0))*exp(-i3*pow(x,1.0/3.0)) - A_in*x;
  return out;
}

//******************************************************************************

/* newton raphson derivative */
inline double didx(double x, double A_in) {
  double out = (- i0/6.0/pow(x,7.0/6.0) - i1*5.0/12.0/pow(x,17.0/12.0) - i2*2.0/3.0/pow(x,5.0/3.0) - i0*i3/3.0/pow(x,5.0/6.0) - i1*i3/3.0/pow(x,13.0/12.0) - i2*i3/3.0/pow(x,4.0/3.0))*exp(-i3*pow(x,1.0/3.0));
  out = out - A_in;
  return out;
}

//******************************************************************************

/* finding self absorbption frequency */
inline double find_xs(double thetae, double nprot, double zfrac, double v0, double h_scale) {

  // prefactor A 
  double A_fac = 2.0*sqrt(3.0)*ME*CL*thetae*(2.0*thetae*thetae)*(3.0*v0*pow(thetae,2.0)/2.0)/4.0/pow(QE,2.0)/nprot/(2.0*zfrac+1.0)/h_scale;

  // do newton rapshon 
  int n;
  double xnew;
  double xold = 1.0;
  for (n = 0; n <= 999; n++) {
    xnew = xold - ix(xold, A_fac)/didx(xold, A_fac);
    if(fabs(1.0 - xnew/xold) < bisects) break;
    xold = xnew;
  }

  // exit condition 
  if(n == 999) {
    printf("no solution in xs");
    exit(0);
  }
    
  // calculate nus 
  double nus = 1.5*xnew*v0*thetae*thetae;
    
  // return 
  return nus;

}

//******************************************************************************

/* fraction scattered to wien peak */
inline double fraction(double x, double taut, double thetae) {
  double out;
  double alpha = 3.0;
  double logthx = log(alpha*thetae/x);
  double logA = log(1.0 + 4.0*thetae + 16.0*pow(thetae,2.0));
  double jm = logthx/logA;
  if(taut > 1.0) {
    out = exp(-jm/pow(taut,2.0));
  } else {
    double stau = taut + taut*taut;
    out = gsl_sf_gamma_inc_P(jm, stau);
  }
  return out;
}

//******************************************************************************

/* find synchrotron radiation production rate */
inline void find_ndots(double thetae, double taut, double nprot, double zfrac, double h_scale, double bfield, double *fs, double *ndots) {

  // find cyclotron frequency 
  double v0 = QE*bfield/2.0/M_PI/ME/CL;
    
  // first, find self-absorption frequency 
  double nus = find_xs(thetae, nprot, zfrac, v0, h_scale);
  double xs = hplanck*nus/ME/CL/CL;

  // fraction scattered to wien peak #
  *fs = fraction(xs, taut, thetae);
    
  // then, calculate coefficient 
  double a1 = 2.0/3.0/v0/thetae/thetae;
  double a2 = 0.4/pow(a1,0.25);
  double a3 = 0.5316/sqrt(a1);
  double a4 = 1.8899*pow(a1,1.0/3.0);

  // calculate the cooling rate 
  double one = gsl_sf_gamma_inc(5.5, a4*pow(nus,1.0/3.0))/pow(a4,5.5);
  double two = gsl_sf_gamma_inc(4.75, a4*pow(nus,1.0/3.0))*a2/pow(a4,4.75);
  double three = a3*(pow(a4,3.0)*nus + 3.0*pow(a4,2.0)*pow(nus,2.0/3.0) + 6.0*a4*pow(nus,1.0/3.0) + 6.0)*exp(-a4*pow(nus,1.0/3.0))/pow(a4,4.0);
  double gnus = one + two + three;

  // cooling rate 
  double qs = 2.0*M_PI*(thetae*ME)*pow(nus,3.0)/3.0/h_scale;
  qs = qs + (6.76e-28)*nprot*(2.0*zfrac+1.0)*gnus/(2.0*thetae*thetae)/pow(a1,1.0/6.0);

  // production rate 
  *ndots = qs/hplanck/nus;

}

//*------------------------------------------------------------------------------------------------------------------------*//
//
// These sections related to the "radiative transfer"
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//******************************************************************************

// find photon frequency below witch the local spectrum is black body //
inline double find_xm(double z, double tau, double nprot, double theta) {
    
  /* backup */
  double xc_old;

  /* set the LHS of the root */
  double at = (2.0*z+1.0)*nprot*sigma_t;
  double lhs = at*(1.0+(tau*tau)*fmin(1.0,8.0*theta))/(tau*(1+tau));
    
  /* initial guess */
  double xl = -50.0, xr = log10(700.0);
  double xlp = pow(10.0, xl)*theta, xrp = pow(10.0, xr)*theta;
  double fl = brem_abs(xlp, z, nprot, theta) - lhs;
  double fr = brem_abs(xrp, z, nprot, theta) - lhs;
  double xc = 0.5*(xl+xr), xcp = pow(10.0, xc)*theta;
   
  /* poor initial guess, exit */
  if(fl*fr > 0.0) {
    printf("poor initial guess xm");
    exit(0);
  }

  /* continue */
  double fc = brem_abs(xcp, z, nprot, theta) - lhs;

  /* main loop */
  int n; 
  for (n = 0; n < 99999; n++) {
    xc_old = xc;
    if(fc*fl > 0.0) {
      fl = fc, xl = xc;
    } else if(fc*fr > 0.0){
      fr = fc, xr = xc;
    }
    xc = 0.5*(xl+xr), xcp = pow(10.0, xc)*theta;
    fc = brem_abs(xcp, z, nprot, theta) - lhs;
    if(fabs(1.0 - xc/xc_old) < bisects) break;
  }
  if(n == 99999) {
    printf("no solution in xm");
    exit(0);
  }

  /* return */
  return xcp;
}

//******************************************************************************

/* fraction of up-scattered bremsstrahlung photon */
inline double fbrem(double y, double taut, double theta, double xm) {
  double out = 0.0; 
  double alpha = 3.0;
  double log_alpha = log(alpha);
  double logA = log(1.0+4.0*theta+16.0*theta*theta);
  if(taut > 1.0) { 
    if(y <= 1e3) {
      out = 2.0*(y*y - y*(1.0+y)*exp(-1.0/y));
    } else {
      out = 1.0 - 2.0/3.0/y;
    }
    out = out*exp(-log_alpha/pow(taut,2.0)/logA);
  } else {
    double u0, u1, u2, u3, u4;
    double f0, f1, f2, f3, f4;
    int n_grid = 100;
    double stau = taut + pow(taut,2.0);
    double logthx = log(theta/xm);
    double dh = (float)logthx/n_grid;
    for (int o = 0; o <= n_grid; o += 4) {
      u0 = dh*(float)o;
      u1 = u0 + dh;
      u2 = u0 + 2.0*dh;
      u3 = u0 + 3.0*dh;
      u4 = u0 + 4.0*dh;
      f0 = u0*gsl_sf_gamma_inc_P((u0+log_alpha)/logA, stau);
      f1 = u1*gsl_sf_gamma_inc_P((u1+log_alpha)/logA, stau);
      f2 = u2*gsl_sf_gamma_inc_P((u2+log_alpha)/logA, stau);
      f3 = u3*gsl_sf_gamma_inc_P((u3+log_alpha)/logA, stau);
      f4 = u4*gsl_sf_gamma_inc_P((u4+log_alpha)/logA, stau);
      out = out + (2.0/45.0)*dh*(7.0*f0 + 32.0*f1 + 12.0*f2 + 32.0*f3 + 7.0*f4);
    }
  }
  return out;
}

//******************************************************************************

/* compton y1 parameter */
inline double comptony1(double x, double tau, double theta) {
  double out = (tau*tau)*log(1.0+4.0*theta+16.0*theta*theta)/log(theta/x);
  return out;
}

//******************************************************************************

/* flat spectrum number density */
inline double flatn1(double x, double theta, double y) {
  double out = (2.0/M_PI)*(alphaf*alphaf*alphaf)*(x*x)*theta*(1.0/log(theta/x) + y/(1.0+y))/(RE*RE*RE);
  return out;
}

//******************************************************************************

/* wien spectrum number density */
inline double ngamma(double tau, double theta, double fb, double ndotbr, double fs, double ndots, double r_size) {
  double gt;
  if(theta < 1.0) {
    gt = 1.0/(1.0 + 5.0*theta + 0.4*theta*theta);
  } else {
    gt = (0.1875)*(log(2.0*eta*theta)+0.75)/(1.0+(0.1/theta))/(theta*theta);
  }
  double ng = r_size/CL*(1.0 + gt*tau)*(fb*ndotbr+ fs*ndots);
  return ng;
}

//*------------------------------------------------------------------------------------------------------------------------*//
//
// These sections are miscellaneous
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//******************************************************************************

/* positron fractions assuming local thermal equilibrium */
inline double get_zfrac(double nprot, double thetae) {
  double kt = thetae*ME*CL*CL;
  double lam_th = hplanck/sqrt(2.0*M_PI*ME*kt);
  double u = 4.0/nprot/nprot/pow(lam_th,6.0)*exp(-2.0/thetae);
  double zfrac = 0.5*(-1.0+sqrt(1.0+4.0*u));
  return zfrac;
}

//******************************************************************************

/* Coulomb coupling, stolen from ebhlight */
/* Eventually, this subroutine should migrate back to electrons.c */
inline double coulomb_onezone(double thetap, double thetae, double nprot, double npost, int i, int j, int k)
{

  /* note that I have changed some variable decleration */
  /* this subroutine needed to be contionusly udpated */
  double logCoul = COULOMB_LOG;
  double thetam = 1.0/(1.0/thetae + 1.0/thetap);
  double Tp = thetap*ME*CL*CL/KBOL;
  double Te = thetae*ME*CL*CL/KBOL;
  double nelec = npost + nprot;

  // Sanity checks, although electron fixup routine should catch these
  double Qc;
  if (!isnan(Te) && !isnan(Tp) && Te > 0.0 && Tp > 0.0)
  {
    double term1, term2;

    // Get Coulomb heating rate.
    // Need to handle cases where thetap < 1e-2, Thetae < 1e-2, and both
    // Thetae and thetap < 1e-2 separately due to Bessel functions exploding
    double prefac = 3.0/2.0*ME/MP*(nelec + npost)*(nprot)*logCoul*CL*KBOL*sigma_t*(Tp - Te);
    double thetaCrit = 1.0e-2;
    if (thetae < thetaCrit && thetap < thetaCrit) {
      term1 = sqrt(thetam/(M_PI*thetae*thetap/2.0));                          
      term2 = sqrt(thetam/(M_PI*thetae*thetap/2.0));
    } else if (thetae < thetaCrit) {
      term1 = exp(-1.0/thetap)/safe_Kn(2, 1.0/thetap)*sqrt(thetam/thetae);
      term2 = exp(-1.0/thetap)/safe_Kn(2, 1.0/thetap)*sqrt(thetam/thetae);
    } else if (thetap < thetaCrit) {
      term1 = exp(-1.0/thetae)/safe_Kn(2, 1.0/thetae)*sqrt(thetam/thetap);
      term2 = exp(-1.0/thetae)/safe_Kn(2, 1.0/thetae)*sqrt(thetam/thetap);
    } else {
      term1 = safe_Kn(1, 1.0/thetam)/(safe_Kn(2, 1.0/thetae)*safe_Kn(2, 1.0/thetap));
      term2 = safe_Kn(0, 1.0/thetam)/(safe_Kn(2, 1.0/thetae)*safe_Kn(2, 1.0/thetap));
    }
    term1 *= (2.0*pow(thetae + thetap,2.0) + 1.0)/(thetae + thetap);
    term2 *= 2.0;
    Qc = prefac*(term1 + term2);

  } else {
    Qc = 0.0;
  }

  /* convert back to Code unit */
  //Qc = Qc *  T_unit/U_unit;
  return Qc;
}

//******************************************************************************

// Modified Bessel function of second kind with safe inputs
inline double safe_Kn(int n, double x)
{
  if (x > 100.0) {
    return exp(-x)*sqrt(M_PI/(2.0*x));
  } else {
    return gsl_sf_bessel_Kn(n, x);
  }
}

//******************************************************************************

/* series expansion for error function */
inline double series_asym(double x_in) {
  double out = 1.0/x_in - 0.5/pow(x_in,3.0) + 0.75/pow(x_in,5.0) - 1.875/pow(x_in,7.0) + 6.5625/pow(x_in,9.0);
  return out;
}

#endif
