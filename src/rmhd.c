
/*------------------------------------------------------------------------------
 * FILE: rmhd.c
 *
 * AUTHOR: Jonathan Zrake, NYU CCPP: zrake@nyu.edu
 *
 * PURPOSE: Provide a derivative operator, dUdt, for the conserved quantites
 *   of the Relativistic MHD equations. Internal memory is allocated for the
 *   set of primitive quantites, as well 4-velocites.
 *
 * REFERENCES:
 *
 *
 * USAGE: Either in Alive or Dead mode:
 *
 *   Alive Mode: initialized by a primitive variable array from the user,
 *     along with its dimensions. In this case internal memory is allocated for
 *     a primitive variable array and a buffer to hold 1-d fluxes. Purpose is
 *     for external use of the dUdt_Nd functions
 *
 *   Dead Mode: no initialization is necessary, no internal memory is used.
 *     cons_to_prim runs on estimate by default, may be set to use output
 *     primitive state as the guess. Purpose is for unit testing by external
 *     code.
 *
 *------------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <math.h>

// Enums used for indexing through primitive/conserved
// data. The state of a given cell is described by 8
// contiguous doubles.

enum { ddd, tau, Sx, Sy, Sz, Bx, By, Bz }; // Conserved
enum { rho, pre, vx, vy, vz };             // Primitive


// Modes for selecting the strategy of the solver

enum RiemannSolverMode { RiemannSolver_HLL,
                         RiemannSolver_HLLC };

enum ReconstructMode { Reconstruct_PiecewiseConstant,
                       Reconstruct_PLM3Velocity,
                       Reconstruct_PLM4Velocity };

enum SlopeLimiterMode { SlopeLimiter_Minmod,
			SlopeLimiter_MonotizedCentral,
			SlopeLimiter_HarmonicMean };

enum QuarticSolverMode { QuarticSolver_Exact,
                         QuarticSolver_Approx1,
                         QuarticSolver_Approx2,
                         QuarticSolver_None };

enum LibraryOperationMode { LibraryOperation_Alive,
                            LibraryOperation_Dead }
  libopstate = LibraryOperation_Dead;

int quiet = 0;
int dimension=1;
int stride[4];
double dx,dy,dz;
double cons_to_prim_last_W;
double (*slope_limiter)(double, double, double);

struct LibraryState
{
  int cons_to_prim_iter;
  int cons_to_prim_use_estimate;
  int cons_to_prim_verbose;

  double max_lambda;
  double adiabatic_gamma;
  double plm_theta;

  int mode_riemann_solver;
  int mode_reconstruct;
  int mode_slope_limiter;
  int mode_quartic_solver;
} lib_state = { 0,0,0,
                0.0,1.4,2.0,
                RiemannSolver_HLL,
                Reconstruct_PLM4Velocity,
		SlopeLimiter_Minmod,
                QuarticSolver_Exact };


/*------------------------------------------------------------------------------
 *
 * Function prototypes and static library variables
 *
 */

static double *PrimitiveArray;
static double *FluxInterArray_x;
static double *FluxInterArray_y;
static double *FluxInterArray_z;
static double *lib_ux, *lib_uy, *lib_uz;

int Fiph              (const double *P, double *F);
int prim_to_cons_point(const double *P, double *U);
int cons_to_prim_point(const double *U, double *P);
int prim_to_cons_array(const double *P, double *U, int N);
int cons_to_prim_array(const double *U, double *P, int N);
int rmhd_flux_and_eval(const double *U, const double *P, double *F, double *ap, double *am);

int reconstruct_use_3vel(const double *P0, double *Pl, double *Pr);
int reconstruct_use_4vel(const double *P0,
                         const double *ux, const double *uy, const double *uz,
                         double *Pl, double *Pr);

int constraint_transport_2d(double *Fx, double *Fy);
int constraint_transport_3d(double *Fx, double *Fy, double *Fz);

int new_QuarticEquation(double d4, double d3, double d2, double d1, double d0);
int solve_quartic_equation(double *r1, double *r2, double *r3, double *r4,
                           int *nr12, int *nr34);
int solve_quartic_approx1(double *x);
int solve_quartic_approx2(double *x);

int hll_flux (const double *pl, const double *pr, double *U, double *F, double s);
int hllc_flux(const double *pl, const double *pr, double *U, double *F, double s);
int hllc_set_dimension(int d);

/*------------------------------------------------------------------------------
 *
 * Inline functions, rather than macros for common arithmetic operations
 *
 */
inline double sign(double x)
{
  return (x>0)-(x<0);
}
inline double max2(double a, double b)
{
  return (a>b)?a:b;
}
inline double max3(double a, double b, double c)
{
  double ab=(a>b)?a:b;
  return (ab>c)?ab:c;
}
inline double min3(double a, double b, double c)
{
  double ab=(a<b)?a:b;
  return (ab<c)?ab:c;
}
inline void invert_2by2_matrix(const double A[2][2], double B[2][2])
{
  double det = A[0][0]*A[1][1] - A[1][0]*A[0][1];

  B[0][0] =  A[1][1] / det;
  B[1][1] =  A[0][0] / det;
  B[0][1] = -A[0][1] / det;
  B[1][0] = -A[1][0] / det;
}
inline double plm_minmod(double ul, double u0, double ur)
{
  const double a = lib_state.plm_theta * (u0 - ul);
  const double b =               0.5   * (ur - ul);
  const double c = lib_state.plm_theta * (ur - u0);

  return 0.25*fabs(sign(a) + sign(b)) * (sign(a) + sign(c))*min3(fabs(a), fabs(b), fabs(c));
}
inline double MC_limiter(double ul, double u0, double ur)
{
  const double qp = ur - u0;
  const double qm = u0 - ul;
  const double si = 0.5*(sign(qp) + sign(qm));
  return si * min3(2*fabs(qp), 2*fabs(qm), 0.5*(ur-ul));
}
inline double harmonic_mean(double ul, double u0, double ur)
{
  const double qp = ur - u0;
  const double qm = u0 - ul;
  return 2*max2(0,qp*qm) / (qp+qm);
}

/*------------------------------------------------------------------------------
 *
 * Functions describing adiabatic equation of state
 *
 */
inline double eos_pre(double Rho, double Sie)
{
  return Sie * (Rho * (lib_state.adiabatic_gamma - 1.0));
}
inline double eos_sie(double Rho, double Pre)
{
  return Pre / (Rho * (lib_state.adiabatic_gamma - 1.0));
}
inline double eos_cs2(double Rho, double Pre)
{
  double e = eos_sie(Rho, Pre);
  return lib_state.adiabatic_gamma * Pre / (Pre + Rho + Rho*e);
}


int set_state(struct LibraryState state)
{
  lib_state = state;

  switch (lib_state.mode_slope_limiter)
    {

    case SlopeLimiter_Minmod:
      slope_limiter = plm_minmod;
      break;

    case SlopeLimiter_MonotizedCentral:
      slope_limiter = MC_limiter;
      break;

    case SlopeLimiter_HarmonicMean:
      slope_limiter = harmonic_mean;
      break;

    default:
      slope_limiter = plm_minmod;
      break;
    }

  return 0;
}
struct LibraryState get_state()
{
  return lib_state;
}

int initialize(const double *P, int Nx, int Ny, int Nz,
               double Lx, double Ly, double Lz, int q)
{
  quiet = q;
  if (!quiet)
    {
      printf("\n\n\n");
      printf("\t************** Initiating RMHD back-end **************\n");
      printf("\t*                                                    *\n");
      printf("\t*                                                    *\n");
      printf("\t*                                                    *\n");
      printf("\t*                                                    *\n");
      printf("\t*                                                    *\n");
      printf("\t******************************************************\n");
      printf("\n\n");
      printf("Grid size     ............   (%3d, %3d, %3d)\n"      , Nx,Ny,Nz);
      printf("Domain size   ............   (%2.1f, %2.1f, %2.1f)\n", Lx,Ly,Lz);
      printf("\n\n");
    }

  libopstate = LibraryOperation_Alive;

  stride[0] = Nx*Ny*Nz*8;
  stride[1] =    Ny*Nz*8;
  stride[2] =       Nz*8;
  stride[3] =          8;
  dimension = 1;

  int Ng = 2; // Number of guard cells required for the scheme

  dx = Lx / (Nx-2*Ng);
  dy = Ly / (Ny-2*Ng);
  dz = Lz / (Nz-2*Ng);

  PrimitiveArray = (double*) malloc(stride[0]*sizeof(double));
  memcpy(PrimitiveArray, P, stride[0]*sizeof(double));

  FluxInterArray_x = (double*) malloc(stride[0]*sizeof(double));
  FluxInterArray_y = (double*) malloc(stride[0]*sizeof(double));
  FluxInterArray_z = (double*) malloc(stride[0]*sizeof(double));

  lib_ux = (double*) malloc(stride[0]/8*sizeof(double));
  lib_uy = (double*) malloc(stride[0]/8*sizeof(double));
  lib_uz = (double*) malloc(stride[0]/8*sizeof(double));

  return 0;
}
int finalize()
{
  if (!quiet)
    {
      printf("\n\n\n");
      printf("\t************** Finalizing RMHD back-end **************\n");
      printf("\t*                                                    *\n");
      printf("\t*                                                    *\n");
      printf("\t*                                                    *\n");
      printf("\t*                                                    *\n");
      printf("\t*                                                    *\n");
      printf("\t******************************************************\n");
      printf("\n\n\n");
    }

  libopstate = LibraryOperation_Dead;
  free(PrimitiveArray);

  free(FluxInterArray_x);
  free(FluxInterArray_y);
  free(FluxInterArray_z);

  free(lib_ux);
  free(lib_uy);
  free(lib_uz);

  return 0;
}

int hll_flux(const double *pl, const double *pr, double *U, double *F, double s)
{
  int i;
  double epl, epr, eml, emr;
  double Ul[8], Ur[8];
  double Pl[8], Pr[8];
  double Fl[8], Fr[8];

  memcpy(Pl,pl,8*sizeof(double));
  memcpy(Pr,pr,8*sizeof(double));

  prim_to_cons_point(Pl,Ul);
  prim_to_cons_point(Pr,Ur);

  rmhd_flux_and_eval(Ul, Pl, Fl, &epl, &eml);
  rmhd_flux_and_eval(Ur, Pr, Fr, &epr, &emr);

  double ap = (epl>epr) ? epl : epr;
  double am = (eml<emr) ? eml : emr;

  double ml = (fabs(am)<fabs(ap)) ? fabs(ap) : fabs(am);
  if (lib_state.max_lambda < ml) lib_state.max_lambda = ml;

  double F_hll[8], U_hll[8];
  for (i=0; i<8; ++i)
    {
      U_hll[i] = (ap*Ur[i] - am*Ul[i] +       (Fl[i] - Fr[i])) / (ap - am);
      F_hll[i] = (ap*Fl[i] - am*Fr[i] + ap*am*(Ur[i] - Ul[i])) / (ap - am);
    }

  if      (         s<=am ) for (i=0; i<8; ++i) U[i] = Ul   [i];
  else if ( am<s && s<=ap ) for (i=0; i<8; ++i) U[i] = U_hll[i];
  else if ( ap<s          ) for (i=0; i<8; ++i) U[i] = Ur   [i];

  if      (         s<=am ) for (i=0; i<8; ++i) F[i] = Fl   [i];
  else if ( am<s && s<=ap ) for (i=0; i<8; ++i) F[i] = F_hll[i];
  else if ( ap<s          ) for (i=0; i<8; ++i) F[i] = Fr   [i];

  return 0;
}

int reconstruct_use_3vel(const double *P0, double *Pl, double *Pr)
{
  const size_t S = stride[dimension];
  const size_t T = 2*S;
  int i;

  // Here, Pr refers to the left edge of cell i+1
  //       Pl refers to the rght edge of cell i

  for (i=0; i<8; ++i)
    {
      Pr[i] = P0[S+i] - 0.5 * slope_limiter(P0[ 0+i], P0[S+i], P0[T+i]);
      Pl[i] = P0[0+i] + 0.5 * slope_limiter(P0[-S+i], P0[0+i], P0[S+i]);
    }

  return 0;
}

int reconstruct_use_4vel(const double *P0,
                         const double *ux, const double *uy, const double *uz,
                         double *Pl, double *Pr)
{
  const size_t S = stride[dimension];
  const size_t T = 2*S;

  const size_t U = stride[dimension]/8;
  const size_t V = 2*U;
  int i;

  // Here, Pr refers to the left edge of cell i+1
  //       Pl refers to the rght edge of cell i

  for (i=0; i<8; ++i)
    if (i==rho || i==pre || i==Bx || i==By || i==Bz)
      {
        Pr[i] = P0[S+i] - 0.5 * plm_minmod(P0[ 0+i], P0[S+i], P0[T+i]);
        Pl[i] = P0[0+i] + 0.5 * plm_minmod(P0[-S+i], P0[0+i], P0[S+i]);
      }

  const double ux_r = ux[U] - 0.5 * slope_limiter(ux[ 0], ux[U], ux[V]);
  const double ux_l = ux[0] + 0.5 * slope_limiter(ux[-U], ux[0], ux[U]);

  const double uy_r = uy[U] - 0.5 * slope_limiter(uy[ 0], uy[U], uy[V]);
  const double uy_l = uy[0] + 0.5 * slope_limiter(uy[-U], uy[0], uy[U]);

  const double uz_r = uz[U] - 0.5 * slope_limiter(uz[ 0], uz[U], uz[V]);
  const double uz_l = uz[0] + 0.5 * slope_limiter(uz[-U], uz[0], uz[U]);

  const double Wr = sqrt(1.0 + ux_r*ux_r + uy_r*uy_r + uz_r*uz_r);
  const double Wl = sqrt(1.0 + ux_l*ux_l + uy_l*uy_l + uz_l*uz_l);

  Pr[vx] = ux_r/Wr;  Pr[vy] = uy_r/Wr;  Pr[vz] = uz_r/Wr;
  Pl[vx] = ux_l/Wl;  Pl[vy] = uy_l/Wl;  Pl[vz] = uz_l/Wl;

  return 0;
}
int dUdt_1d(const double *U, double *L)
{
  if (libopstate == LibraryOperation_Dead)
    return 1;

  double *P = PrimitiveArray;
  double *F = FluxInterArray_x;
  int failures,S,i;

  failures = cons_to_prim_array(U,P,stride[0]/8);

  dimension = 1;
  Fiph(P,F);

  S = stride[dimension];
  for (i=S; i<stride[0]; ++i)
    {
      L[i] = -(F[i]-F[i-S])/dx;
    }
  return failures;
}
int dUdt_2d(const double *U, double *L)
{
  if (libopstate == LibraryOperation_Dead)
    return 1;

  double *P = PrimitiveArray;
  double *F = FluxInterArray_x;
  double *G = FluxInterArray_y;
  int i,sx=stride[1],sy=stride[2];

  int failures = cons_to_prim_array(U,P,stride[0]/8);

  dimension = 1;  Fiph(P,F);
  dimension = 2;  Fiph(P,G);

  constraint_transport_2d(F,G);

  for (i=sx; i<stride[0]; ++i)
    {
      L[i] = -(F[i]-F[i-sx])/dx - (G[i]-G[i-sy])/dy;
    }
  return failures;
}
int dUdt_3d(const double *U, double *L)
{
  if (libopstate == LibraryOperation_Dead)
    return 1;

  double *P = PrimitiveArray;
  double *F = FluxInterArray_x;
  double *G = FluxInterArray_y;
  double *H = FluxInterArray_z;

  int i,sx=stride[1],sy=stride[2],sz=stride[3];
  int failures = cons_to_prim_array(U,P,stride[0]/8);

  dimension = 1;  Fiph(P,F);
  dimension = 2;  Fiph(P,G);
  dimension = 3;  Fiph(P,H);

  constraint_transport_3d(F,G,H);

  for (i=sx; i<stride[0]; ++i)
    {
      L[i] = -(F[i]-F[i-sx])/dx - (G[i]-G[i-sy])/dy - (H[i]-H[i-sz])/dz;
    }
  return failures;
}
int Fiph(const double *P, double *F)
{
  const int S = stride[dimension];
  hllc_set_dimension(dimension);

  int i;
  for (i=0; i<S; ++i)
    {
      F[i] = 0;
    }
  for (i=S; i<stride[0]-S*2; i+=8)
    {
      double Pl[8], Pr[8];
      const double *P0 = &P[i];

      switch (lib_state.mode_reconstruct)
        {

        case Reconstruct_PiecewiseConstant:
          memcpy(Pl, P0  , 8*sizeof(double));
          memcpy(Pr, P0+S, 8*sizeof(double));
          break;

        case Reconstruct_PLM3Velocity:
          reconstruct_use_3vel(P0, Pl, Pr);
          break;

        case Reconstruct_PLM4Velocity:
          reconstruct_use_4vel(P0, &lib_ux[i/8], &lib_uy[i/8], &lib_uz[i/8], Pl, Pr);
          break;

        default:
          reconstruct_use_3vel(P0, Pl, Pr);
          break;
        }

      double U_star[8];
      switch (lib_state.mode_riemann_solver)
        {
        case RiemannSolver_HLL:
          hll_flux (Pl, Pr, U_star, &F[i], 0.0);
          break;
        case RiemannSolver_HLLC:
          hllc_flux(Pl, Pr, U_star, &F[i], 0.0);
          break;
        default:
          hll_flux (Pl, Pr, U_star, &F[i], 0.0);
          break;
        }
    }
  for (i=stride[0]-S*2; i<stride[0]; ++i)
    {
      F[i] = 0;
    }
  return 0;
}
int constraint_transport_2d(double *Fx, double *Fy)
{
  double *FxBy = (double*) malloc(stride[0]/8*sizeof(double));
  double *FyBx = (double*) malloc(stride[0]/8*sizeof(double));

  double *F, *G;
  int i;

  const int sx=stride[1],sy=stride[2];
  for (i=sx; i<stride[0]-sx; i+=8)
    {
      F = &Fx[By+i];
      G = &Fy[Bx+i];

      FxBy[i/8] = (2*F[0]+F[sy]+F[-sy]-G[0]-G[sx]-G[-sy]-G[ sx-sy])*0.125;
      FyBx[i/8] = (2*G[0]+G[sx]+G[-sx]-F[0]-F[sy]-F[-sx]-F[-sx+sy])*0.125;
    }
  for (i=0; i<stride[0]; i+=8)
    {
      Fx[i+Bx] = 0.0;       Fx[i+By] = FxBy[i/8];
      Fy[i+Bx] = FyBx[i/8]; Fy[i+By] = 0.0;
    }

  free(FxBy);
  free(FyBx);

  return 0;
}
int constraint_transport_3d(double *Fx, double *Fy, double *Fz)
{
  double *FxBy = (double*) malloc(stride[0]/8*sizeof(double));
  double *FxBz = (double*) malloc(stride[0]/8*sizeof(double));

  double *FyBz = (double*) malloc(stride[0]/8*sizeof(double));
  double *FyBx = (double*) malloc(stride[0]/8*sizeof(double));

  double *FzBx = (double*) malloc(stride[0]/8*sizeof(double));
  double *FzBy = (double*) malloc(stride[0]/8*sizeof(double));

  double *F, *G, *H;
  int i;

  const int sx=stride[1],sy=stride[2],sz=stride[3];
  for (i=sx; i<stride[0]-sx; i+=8)
    {
      F = &Fx[By+i];
      G = &Fy[Bx+i];

      FxBy[i/8] = (2*F[0]+F[sy]+F[-sy]-G[0]-G[sx]-G[-sy]-G[ sx-sy])*0.125;
      FyBx[i/8] = (2*G[0]+G[sx]+G[-sx]-F[0]-F[sy]-F[-sx]-F[-sx+sy])*0.125;

      G = &Fy[Bz+i];
      H = &Fz[By+i];

      FyBz[i/8] = (2*G[0]+G[sz]+G[-sz]-H[0]-H[sy]-H[-sz]-H[ sy-sz])*0.125;
      FzBy[i/8] = (2*H[0]+H[sy]+H[-sy]-G[0]-G[sz]-G[-sy]-G[-sy+sz])*0.125;

      H = &Fz[Bx+i];
      F = &Fx[Bz+i];

      FzBx[i/8] = (2*H[0]+H[sx]+H[-sx]-F[0]-F[sz]-F[-sx]-F[ sz-sx])*0.125;
      FxBz[i/8] = (2*F[0]+F[sz]+F[-sz]-H[0]-H[sx]-H[-sz]-H[-sz+sx])*0.125;
    }
  for (i=0; i<stride[0]; i+=8)
    {
      Fx[i+Bx] = 0.0;        Fx[i+By] = FxBy[i/8];  Fx[i+Bz] = FxBz[i/8];
      Fy[i+Bx] = FyBx[i/8];  Fy[i+By] = 0.0;        Fy[i+Bz] = FyBz[i/8];
      Fz[i+Bx] = FzBx[i/8];  Fz[i+By] = FzBy[i/8];  Fz[i+Bz] = 0.0;
    }

  free(FxBy);  free(FyBz);  free(FzBx);
  free(FxBz);  free(FyBx);  free(FzBy);

  return 0;
}
int rmhd_flux_and_eval(const double *U, const double *P, double *F, double *ap, double *am)
{
  const double v2   =   P[vx]*P[vx] + P[vy]*P[vy] + P[vz]*P[vz];
  const double B2   =   P[Bx]*P[Bx] + P[By]*P[By] + P[Bz]*P[Bz];
  const double Bv   =   P[Bx]*P[vx] + P[By]*P[vy] + P[Bz]*P[vz];
  const double W    =   1.0 / sqrt(1.0 - v2);
  const double W2   =   W*W;
  const double b0   =   W * Bv;
  const double b2   =   (B2 + b0*b0) / W2;
  const double bx   =   (P[Bx] + b0 * W*P[vx]) / W;
  const double by   =   (P[By] + b0 * W*P[vy]) / W;
  const double bz   =   (P[Bz] + b0 * W*P[vz]) / W;
  const double e    =   eos_sie(P[rho], P[pre]);
  const double p    =   P[pre];
  const double h    =   1.0 + e + P[pre]/P[rho];
  const double p_   =   p + 0.5 * b2;

  switch (dimension)
    {
    case 1:
      F[ddd] = U[ddd] * P[vx];
      F[tau] = U[tau] * P[vx] - b0*P[Bx] / W + p_*P[vx];
      F[Sx ] = U[Sx]  * P[vx] - bx*P[Bx] / W + p_;
      F[Sy ] = U[Sy]  * P[vx] - by*P[Bx] / W;
      F[Sz ] = U[Sz]  * P[vx] - bz*P[Bx] / W;
      F[Bx ] = 0.0;
      F[By ] = P[vx]*P[By] - P[vy]*P[Bx];
      F[Bz ] = P[vx]*P[Bz] - P[vz]*P[Bx];
      break;
    case 2:
      F[ddd] = U[ddd] * P[vy];
      F[tau] = U[tau] * P[vy] - b0*P[By] / W + p_*P[vy];
      F[Sx ] = U[Sx]  * P[vy] - bx*P[By] / W;
      F[Sy ] = U[Sy]  * P[vy] - by*P[By] / W + p_;
      F[Sz ] = U[Sz]  * P[vy] - bz*P[By] / W;
      F[Bx ] = P[vy]*P[Bx] - P[vx]*P[By];
      F[By ] = 0.0;
      F[Bz ] = P[vy]*P[Bz] - P[vz]*P[By];
      break;
    case 3:
      F[ddd] = U[ddd] * P[vz];
      F[tau] = U[tau] * P[vz] - b0*P[Bz] / W + p_*P[vz];
      F[Sx ] = U[Sx]  * P[vz] - bx*P[Bz] / W;
      F[Sy ] = U[Sy]  * P[vz] - by*P[Bz] / W;
      F[Sz ] = U[Sz]  * P[vz] - bz*P[Bz] / W + p_;
      F[Bx ] = P[vz]*P[Bx] - P[vx]*P[Bz];
      F[By ] = P[vz]*P[By] - P[vy]*P[Bz];
      F[Bz ] = 0.0;
      break;
    }

  double vi=0, bi=0;
  switch (dimension)
    {
    case 1:
      vi = P[vx]; bi = bx;
      break;
    case 2:
      vi = P[vy]; bi = by;
      break;
    case 3:
      vi = P[vz]; bi = bz;
      break;
    }

  const double W4   =  W2*W2;
  const double cs2  =  eos_cs2(P[rho],P[pre]);
  const double V2   =  vi*vi;
  const double V3   =  vi*V2;
  const double V4   =  vi*V3;

  const double K  =    P[rho]*h * (1.0/cs2-1.0) * W4;
  const double L  =  -(P[rho]*h +   b2/cs2)     * W2;

  const double A4 =    K    - L          -   b0*b0;
  const double A3 = -4*K*vi + L*vi*2     + 2*b0*bi;
  const double A2 =  6*K*V2 + L*(1.0-V2) +   b0*b0 - bi*bi;
  const double A1 = -4*K*V3 - L*vi*2     - 2*b0*bi;
  const double A0 =    K*V4 + L*V2       +   bi*bi;

  new_QuarticEquation(A4,A3,A2,A1,A0);
  switch (lib_state.mode_quartic_solver)
    {

    case QuarticSolver_Exact:
      {
        double r1, r2, r3, r4;
        int nr12, nr34;
        int nr = solve_quartic_equation(&r1, &r2, &r3, &r4, &nr12, &nr34);

        double ap12 = (r1>r2) ? r1 : r2;
        double ap34 = (r3>r4) ? r3 : r4;

        double am12 = (r1<r2) ? r1 : r2;
        double am34 = (r3<r4) ? r3 : r4;

        *ap = (nr==2) ? ((nr12==2) ? ap12 : ap34) : ((ap12>ap34) ? ap12 : ap34);
        *am = (nr==2) ? ((nr12==2) ? am12 : am34) : ((am12<am34) ? am12 : am34);
      }
      break;

    case QuarticSolver_Approx1:
      {
        *am = -1.0; *ap = 1.0;
        solve_quartic_approx1(am);
        solve_quartic_approx1(ap);
      }
      break;

    case QuarticSolver_Approx2:
      {
        *am = -1.0; *ap = 1.0;
        solve_quartic_approx2(am);
        solve_quartic_approx2(ap);
      }
      break;

    case QuarticSolver_None:
      {
        *am = -1.0;
        *ap =  1.0;
      }
      break;
    }

  if (fabs(*ap)>1.0 || fabs(*am)>1.0)
    {
      *am = -1.0;
      *ap =  1.0;
    }

  return 0;
}

int cons_to_prim_point(const double *U, double *P)
{
  static const double PRES_FLOOR = 1e-10;
  static const double ERROR_TOLR = 1e-6;
  static const int NEWTON_MAX_ITER = 25;

  // Quantites known from conserved
  const double gamf  = (lib_state.adiabatic_gamma - 1.0) / lib_state.adiabatic_gamma;
  const double D     = U[ddd];
  const double Tau   = U[tau];
  const double S2    = U[Sx]*U[Sx] + U[Sy]*U[Sy] + U[Sz]*U[Sz];
  const double B2    = U[Bx]*U[Bx] + U[By]*U[By] + U[Bz]*U[Bz];
  const double BS    = U[Bx]*U[Sx] + U[By]*U[Sy] + U[Bz]*U[Sz];
  const double BS2   = BS*BS;

  int est            = lib_state.cons_to_prim_use_estimate;
  int verbose        = lib_state.cons_to_prim_verbose;
  int use_pres_floor = 0;
  int soln_found     = 0;
  int n_iter         = 0;

  // Quantites guessed from existing primitives, or estimated from conserved
  double v2 = P[vx]*P[vx] + P[vy]*P[vy] + P[vz]*P[vz];
  double h_guess = 1 + eos_sie(P[rho], P[pre]) + P[pre]/P[rho];
  double W_guess = 1.0 / sqrt(1 - v2);
  double W = (est) ? sqrt(S2/(D*D) + 1) : W_guess;
  double Z = (est) ? D*W                : P[rho]*h_guess*W_guess*W_guess;

  const double bigZ = 1e20;
  const double bigW = 1e12;
  const double smlZ = 0.0;
  const double smlW = 1.0;

  while (!soln_found)
    {
      const double Z2 = Z*Z;
      const double Z3 = Z*Z2;
      const double W2 = W*W;
      const double W3 = W*W2;

      const double Pre = (use_pres_floor) ? PRES_FLOOR : (D/W) * (Z/(D*W) - 1.0) * gamf;

      const double f1 = -S2  +  (Z+B2)*(Z+B2)*(W2-1)/W2 - (2*Z+B2)*BS2/Z2;      // eqn (84)
      const double f2 = -Tau +   Z+B2 - Pre - 0.5*B2/W2 -      0.5*BS2/Z2 - D;  // eqn (85)

      const double df1dZ = 2*(B2+Z)*(BS2*W2 + (W2-1)*Z3) / (W2*Z3);
      const double df1dW = 2*(B2+Z)*(B2+Z) / W3;
      const double df2dZ = 1 + BS2/Z3 - gamf/W2;
      const double df2dW = B2/W3 + (2*Z - D*W)/W3 * gamf;

      const double J[2][2] = { { df1dZ,  df1dW },   // The Jacobian matrix
                               { df2dZ,  df2dW } };
      double       G[2][2];                         // The inverse Jacobian matrix

      invert_2by2_matrix(J, G);

      const double dZ = G[0][0] * f1 + G[0][1] * f2;
      const double dW = G[1][0] * f1 + G[1][1] * f2;

      double Z_new = Z - dZ;
      double W_new = W - dW;

      Z_new = (Z_new > smlZ) ? Z_new : -Z_new;
      Z_new = (Z_new < bigZ) ? Z_new :  Z;

      W_new = (W_new > smlW) ? W_new : smlW;
      W_new = (W_new < bigW) ? W_new : bigW;

      Z = Z_new;
      W = W_new;

      if (verbose)
        {
          printf("iteration number: %d, Pre = %8.6e, W = %8.6e, Z = %8.6e, f1 = %8.6e, f2 = %8.6e\n",
                 n_iter, Pre, W, Z, f1, f2);
        }
      if (fabs(dZ/Z) + fabs(dW/W) < ERROR_TOLR)
        {
          if (Pre>=PRES_FLOOR)
            soln_found = 1;
          else
            {
              n_iter = 0;
              use_pres_floor = 1;
              W = (est) ? sqrt(S2/(D*D) + 1.0) : W_guess;
              Z = (est) ? D*W                  : P[rho]*h_guess*W_guess*W_guess;
            }
        }
      if (n_iter++ == NEWTON_MAX_ITER)
        {
          if (Pre < PRES_FLOOR)
            {
              n_iter = 0;
              use_pres_floor = 1;
              W = (est) ? sqrt(S2/(D*D) + 1.0) : W_guess;
              Z = (est) ? D*W                  : P[rho]*h_guess*W_guess*W_guess;
            }
          else
            {
              return 1;
            }
        }
    }
  lib_state.cons_to_prim_iter += n_iter;

  double b0 = BS * W / Z;
  P[rho] =   D/W;
  P[pre] =  (use_pres_floor) ? PRES_FLOOR : (D/W) * (Z/(D*W) - 1.0) * gamf;
  P[vx ] =  (U[Sx] + b0*U[Bx]/W) / (Z+B2);
  P[vy ] =  (U[Sy] + b0*U[By]/W) / (Z+B2);
  P[vz ] =  (U[Sz] + b0*U[Bz]/W) / (Z+B2);
  P[Bx ] =   U[Bx];
  P[By ] =   U[By];
  P[Bz ] =   U[Bz];

  cons_to_prim_last_W = W;

  return 0;
}
int cons_to_prim_array(const double *U, double *P, int N)
{
  int failures = 0;
  int i;

  if (libopstate == LibraryOperation_Alive &&
      P != PrimitiveArray)
    {
      memcpy(P, PrimitiveArray, stride[0]*sizeof(double));
    }

  for (i=0; i<N*8; i+=8)
    {
      const double *Ui = &U[i];
      double       *Pi = &P[i];

      failures += cons_to_prim_point(Ui,Pi);

      if (lib_state.mode_reconstruct == Reconstruct_PLM4Velocity &&
          libopstate == LibraryOperation_Alive)
        {
          double W = cons_to_prim_last_W;
          lib_ux[i/8] = W*Pi[vx];
          lib_uy[i/8] = W*Pi[vy];
          lib_uz[i/8] = W*Pi[vz];
        }
    }
  return failures;
}
int prim_to_cons_point(const double *P, double *U)
{
  const double v2   =   P[vx]*P[vx] + P[vy]*P[vy] + P[vz]*P[vz];
  const double B2   =   P[Bx]*P[Bx] + P[By]*P[By] + P[Bz]*P[Bz];
  const double Bv   =   P[Bx]*P[vx] + P[By]*P[vy] + P[Bz]*P[vz];
  const double W2   =   1.0 / (1.0 - v2);
  const double W    =   sqrt(W2);
  const double b0   =   W * Bv;
  const double b2   =   (B2 + b0*b0) / W2;
  const double bx   =   (P[Bx] + b0 * W*P[vx]) / W;
  const double by   =   (P[By] + b0 * W*P[vy]) / W;
  const double bz   =   (P[Bz] + b0 * W*P[vz]) / W;
  const double e    =   eos_sie(P[rho], P[pre]);
  const double p    =   P[pre];
  const double e_   =   e + 0.5 * b2 / P[rho];
  const double p_   =   p + 0.5 * b2;
  const double h_   =   1.0 + e_ + p_ / P[rho];

  U[ddd] = P[rho] * W;
  U[tau] = P[rho] * h_ * W2 - p_    - b0*b0 - U[ddd];
  U[Sx ] = P[rho] * h_ * W2 * P[vx] - b0*bx;
  U[Sy ] = P[rho] * h_ * W2 * P[vy] - b0*by;
  U[Sz ] = P[rho] * h_ * W2 * P[vz] - b0*bz;
  U[Bx ] = P[Bx ];
  U[By ] = P[By ];
  U[Bz ] = P[Bz ];
  return 0;
}
int prim_to_cons_array(const double *P, double *U, int N)
{
  int i;
  for (i=0; i<N*8; i+=8)
    {
      prim_to_cons_point(&P[i], &U[i]);
    }
  return 0;
}
