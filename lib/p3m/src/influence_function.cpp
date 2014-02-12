/*
  Copyright (C) 2013 Olaf Lenz
  Copyright (C) 2010,2011 The ESPResSo project
  Copyright (C) 2002,2003,2004,2005,2006,2007,2008,2009,2010 
  Max-Planck-Institute for Polymer Research, Theory Group
  
  This file is part of ScaFaCoS.
  
  ScaFaCoS is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  ScaFaCoS is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>. 
*/

#include "Solver.hpp"
#include "utils.hpp"
#include <stdlib.h>

namespace P3M {
  /***************************************************/
  /* FORWARD DECLARATIONS OF INTERNAL FUNCTIONS */
  /***************************************************/
  static void 
  perform_aliasing_sums_ik(Solver *d, p3m_int n[3], 
                           p3m_float numerator_force[3], 
                           p3m_float* denominator_force, 
                           p3m_float* aliasing_sum_energy);

  static void
  perform_aliasing_sums_iki(Solver *d, p3m_int n[3], 
                            p3m_float numerator_force[3], 
                            p3m_float* denominator_force, 
                            p3m_float* aliasing_sum_energy);

  static void
  perform_aliasing_sums_adi(Solver *d, p3m_int n[3], 
                            p3m_float *numerator_force, 
                            p3m_float* numerator_energy,
                            p3m_float* denominator);

  static void 
  calc_meshift(Solver *d);

  /***************************************************/
  /* IMPLEMENTATION */
  /***************************************************/
  /** Calculates the optimal influence function of Hockney and Eastwood 
   * for force calculations and energy calculations.
   *
   *  Each node calculates only the values for its domain in k-space
   *  (see fft.plan[3].grid and fft.plan[3].start).
   *
   *  See also: Hockney/Eastwood 8-22 (p275). Note the somewhat
   *  different convention for the prefactors, which is described in
   *  Deserno/Holm. */
  void calc_influence_function_ik(Solver *d) {
    P3M_DEBUG(printf("  calc_influence_function_ik() started...\n"));
    calc_meshift(d);

    int size = 1;
    int end[3];
    int *new_grid = d->fft.plan[3].new_grid;;
    int *start = d->fft.plan[3].start;
    for (p3m_int i=0;i<3;i++) {
      size *= new_grid[i];
      end[i] = d->fft.plan[3].start[i] + new_grid[i];
    }
    d->g_force = (p3m_float *) realloc(d->g_force, size*sizeof(p3m_float));
    d->g_energy = (p3m_float *) realloc(d->g_energy, size*sizeof(p3m_float));

    int n[3];
    for (n[0]=start[0]; n[0]<end[0]; n[0]++) {
      for (n[1]=start[1]; n[1]<end[1]; n[1]++) {
        for (n[2]=start[2]; n[2]<end[2]; n[2]++) {
          p3m_int ind = (n[2]-start[2]) + new_grid[2] * 
            ((n[1]-start[1]) + new_grid[1]*(n[0]-start[0]));
          if ((n[KX]%(d->grid[RX]/2)==0) && 
              (n[KY]%(d->grid[RY]/2)==0) && 
              (n[KZ]%(d->grid[RZ]/2)==0) ) {
            d->g_force[ind] = 0.0;
            d->g_energy[ind] = 0.0;
          } else {
            p3m_float numerator_force[3];
            p3m_float numerator_energy;
            p3m_float denominator;
            perform_aliasing_sums_ik(d, n, 
                                     numerator_force, &numerator_energy, 
                                     &denominator);

            p3m_float fak1 = 
              d->d_op[RX][n[KX]]*numerator_force[RX]/d->box_l[RX] + 
              d->d_op[RY][n[KY]]*numerator_force[RY]/d->box_l[RY] + 
              d->d_op[RZ][n[KZ]]*numerator_force[RZ]/d->box_l[RZ];
            p3m_float fak2 = 
              SQR(d->d_op[RX][n[KX]]/d->box_l[RX]) +
              SQR(d->d_op[RY][n[KY]]/d->box_l[RY]) +
              SQR(d->d_op[RZ][n[KZ]]/d->box_l[RZ]);
            p3m_float fak3 = fak1/(fak2 * SQR(denominator));
            d->g_force[ind] = M_2_PI*fak3;
            d->g_energy[ind] = 0.5 * d->g_force[ind];
            /* d->g_energy[ind] = M_1_PI*numerator_energy/SQR(denominator); */
            /* if (fabs(d->g_energy[ind]) > 1.e-5) */
            /*   printf("%d: %15.10e\n", ind, d->g_energy[ind]/d->g_force[ind]); */
          }
        }
      }
    }
    P3M_DEBUG(printf("  calc_influence_function_ik() finished.\n"));
  }

  static void
  perform_aliasing_sums_ik(Solver *d, p3m_int n[3], 
                           p3m_float numerator_force[3], 
                           p3m_float* numerator_energy,
                           p3m_float* denominator) {
    *denominator = 0.0;
    *numerator_energy = 0.0;
    for (p3m_int i = 0; i < 3; i++)
      numerator_force[i] = 0.0;

    p3m_float prefactor = SQR(M_PI/d->alpha);

    for (p3m_int mx = -P3M_BRILLOUIN; mx <= P3M_BRILLOUIN; mx++) {
      const p3m_int nmx = d->meshift_x[n[KX]] + d->grid[RX]*mx;
      const p3m_float sx  = pow(sinc(nmx/(p3m_float)d->grid[RX]),2.0*d->cao);
      for (p3m_int my = -P3M_BRILLOUIN; my <= P3M_BRILLOUIN; my++) {
        const p3m_int nmy = d->meshift_y[n[KY]] + d->grid[RY]*my;
        const p3m_float sy  = sx*pow(sinc(nmy/(p3m_float)d->grid[RY]),2.0*d->cao);
        for (p3m_int mz = -P3M_BRILLOUIN; mz <= P3M_BRILLOUIN; mz++) {
          const p3m_int nmz = d->meshift_z[n[KZ]] + d->grid[RZ]*mz;
          const p3m_float sz  = sy*pow(sinc(nmz/(p3m_float)d->grid[RZ]),2.0*d->cao);
          const p3m_float nm2 = 
            SQR(nmx/d->box_l[RX]) + 
            SQR(nmy/d->box_l[RY]) + 
            SQR(nmz/d->box_l[RZ]);
          const p3m_float prefactor2 = sz*exp(-prefactor*nm2)/nm2;

          *numerator_energy += prefactor2;
          numerator_force[RX] += prefactor2*nmx/d->box_l[RX];
          numerator_force[RY] += prefactor2*nmy/d->box_l[RY];
          numerator_force[RZ] += prefactor2*nmz/d->box_l[RZ];

          *denominator  += sz;
        }
      }
    }
  }

  void calc_influence_function_iki(Solver *d) {
    P3M_DEBUG(printf("  calc_influence_function_iki() started...\n"));
    calc_meshift(d);

    int size = 1;
    int end[3];
    int *new_grid = d->fft.plan[3].new_grid;;
    int *start = d->fft.plan[3].start;
    for (p3m_int i=0;i<3;i++) {
      size *= new_grid[i];
      end[i] = d->fft.plan[3].start[i] + new_grid[i];
    }
    d->g_force = (p3m_float *) realloc(d->g_force, size*sizeof(p3m_float));
    d->g_energy = (p3m_float *) realloc(d->g_energy, size*sizeof(p3m_float));

    int n[3];
    for (n[0]=start[0]; n[0]<end[0]; n[0]++) {
      for (n[1]=start[1]; n[1]<end[1]; n[1]++) {
        for (n[2]=start[2]; n[2]<end[2]; n[2]++) {
          p3m_int ind = (n[2]-start[2]) + new_grid[2] * 
            ((n[1]-start[1]) + new_grid[1]*(n[0]-start[0]));
          if ((n[KX]%(d->grid[RX]/2)==0) && 
              (n[KY]%(d->grid[RY]/2)==0) && 
              (n[KZ]%(d->grid[RZ]/2)==0) ) {
            d->g_force[ind] = 0.0;
            d->g_energy[ind] = 0.0;
          } else {
            p3m_float numerator_force[3];
            p3m_float numerator_energy;
            p3m_float denominator[2];
            perform_aliasing_sums_iki(d, n, 
                                      numerator_force, &numerator_energy, 
                                      denominator);

            p3m_float fak1 = 
              d->d_op[RX][n[KX]]*numerator_force[RX]/d->box_l[RX] + 
              d->d_op[RY][n[KY]]*numerator_force[RY]/d->box_l[RY] + 
              d->d_op[RZ][n[KZ]]*numerator_force[RZ]/d->box_l[RZ];
            p3m_float fak2 = 
              SQR(d->d_op[RX][n[KX]]/d->box_l[RX]) +
              SQR(d->d_op[RY][n[KY]]/d->box_l[RY]) +
              SQR(d->d_op[RZ][n[KZ]]/d->box_l[RZ]);
            p3m_float fak3 = fak1/(fak2 * 0.5 * (SQR(denominator[0]) + SQR(denominator[1])) );
            d->g_force[ind] = M_2_PI*fak3;
            d->g_energy[ind] = M_1_PI*numerator_energy/SQR(denominator[0]);
          }
        }
      }
    }
    P3M_DEBUG(printf("  calc_influence_function_iki() finished.\n"));
  }

  static void
  perform_aliasing_sums_iki(Solver *d, p3m_int n[3], 
                            p3m_float numerator_force[3], 
                            p3m_float* numerator_energy,
                            p3m_float* denominator) {
    denominator[0] = denominator[1] = 0.0;
    *numerator_energy = 0.0;
    for (p3m_int i = 0; i < 3; i++)
      numerator_force[i] = 0.0;

    const p3m_float prefactor = SQR(M_PI/d->alpha);

    for (p3m_int mx = -P3M_BRILLOUIN; mx <= P3M_BRILLOUIN; mx++) {
      const p3m_int nmx = d->meshift_x[n[KX]] + d->grid[RX]*mx;
      const p3m_float sx  = pow(sinc(nmx/(p3m_float)d->grid[RX]),2.0*d->cao);
      for (p3m_int my = -P3M_BRILLOUIN; my <= P3M_BRILLOUIN; my++) {
        const p3m_int nmy = d->meshift_y[n[KY]] + d->grid[RY]*my;
        const p3m_float sy  = sx*pow(sinc(nmy/(p3m_float)d->grid[RY]),2.0*d->cao);
        for (p3m_int mz = -P3M_BRILLOUIN; mz <= P3M_BRILLOUIN; mz++) {
          const p3m_int nmz = d->meshift_z[n[KZ]] + d->grid[RZ]*mz;
          const p3m_float sz  = sy*pow(sinc(nmz/(p3m_float)d->grid[RZ]),2.0*d->cao);
          const p3m_float nm2 = 
            SQR(nmx/d->box_l[RX]) + 
            SQR(nmy/d->box_l[RY]) + 
            SQR(nmz/d->box_l[RZ]);
          const p3m_float prefactor2 = sz*exp(-prefactor*nm2)/nm2;

          *numerator_energy += prefactor2;
          numerator_force[RX] += prefactor2*nmx/d->box_l[RX];
          numerator_force[RY] += prefactor2*nmy/d->box_l[RY];
          numerator_force[RZ] += prefactor2*nmz/d->box_l[RZ];

          denominator[0]  += sz;

          if(((mx+my+mz) % 2) == 0)
            denominator[1] += sz;
          else
            denominator[1] -= sz;

        }
      }
    }
  }

  void calc_influence_function_adi(Solver *d) {
    P3M_DEBUG(printf("  calc_influence_function_adi() started...\n"));
    calc_meshift(d);

    int size = 1;
    int end[3];
    int *new_grid = d->fft.plan[3].new_grid;;
    int *start = d->fft.plan[3].start;
    for (p3m_int i=0;i<3;i++) {
      size *= new_grid[i];
      end[i] = d->fft.plan[3].start[i] + new_grid[i];
    }
    d->g_force = (p3m_float *) realloc(d->g_force, size*sizeof(p3m_float));
    d->g_energy = (p3m_float *) realloc(d->g_energy, size*sizeof(p3m_float));

    int n[3];
    for (n[0]=start[0]; n[0]<end[0]; n[0]++) {
      for (n[1]=start[1]; n[1]<end[1]; n[1]++) {
        for (n[2]=start[2]; n[2]<end[2]; n[2]++) {
          p3m_int ind = (n[2]-start[2]) + new_grid[2] * 
            ((n[1]-start[1]) + new_grid[1]*(n[0]-start[0]));
          if ((n[KX]%(d->grid[RX]/2)==0) && 
              (n[KY]%(d->grid[RY]/2)==0) && 
              (n[KZ]%(d->grid[RZ]/2)==0) ) {
            d->g_force[ind] = 0.0;
            d->g_energy[ind] = 0.0;
          } else {
            p3m_float numerator_force;
            p3m_float numerator_energy;
            p3m_float denominator[4];
            perform_aliasing_sums_adi(d, n, 
                                      &numerator_force, &numerator_energy, 
                                      denominator);

            d->g_force[ind] = numerator_force / 
              (0.5 * M_PI * (denominator[0] * denominator[1] + denominator[2] * denominator[3] )) ;
            d->g_energy[ind] = M_1_PI*numerator_energy/SQR(denominator[0]);
            /* d->g_energy[ind] = 0.5 * d->g_force[ind]; */
          }
        }
      }
    }
    P3M_DEBUG(printf("  calc_influence_function_adi() finished.\n"));
  }

  static void
  perform_aliasing_sums_adi(Solver *d, p3m_int n[3], 
                            p3m_float *numerator_force, 
                            p3m_float* numerator_energy,
                            p3m_float* denominator) {
    denominator[0] = denominator[1] = denominator[2] = denominator[3] = 0.0;
    *numerator_energy = 0.0;
    *numerator_force = 0.0;

    p3m_float prefactor = SQR(M_PI/d->alpha);

    for (p3m_int mx = -P3M_BRILLOUIN; mx <= P3M_BRILLOUIN; mx++) {
      const p3m_int nmx = d->meshift_x[n[KX]] + d->grid[RX]*mx;
      const p3m_float sx  = pow(sinc(nmx/(p3m_float)d->grid[RX]),2.0*d->cao);
      for (p3m_int my = -P3M_BRILLOUIN; my <= P3M_BRILLOUIN; my++) {
        const p3m_int nmy = d->meshift_y[n[KY]] + d->grid[RY]*my;
        const p3m_float sy  = sx*pow(sinc(nmy/(p3m_float)d->grid[RY]),2.0*d->cao);
        for (p3m_int mz = -P3M_BRILLOUIN; mz <= P3M_BRILLOUIN; mz++) {
          const p3m_int nmz = d->meshift_z[n[KZ]] + d->grid[RZ]*mz;
          const p3m_float sz  = sy*pow(sinc(nmz/(p3m_float)d->grid[RZ]),2.0*d->cao);
          const p3m_float nm2 = 
            SQR(nmx/d->box_l[RX]) + 
            SQR(nmy/d->box_l[RY]) + 
            SQR(nmz/d->box_l[RZ]);
          const p3m_float prefactor2 = sz*exp(-prefactor*nm2);

          *numerator_energy += prefactor2/nm2;
          *numerator_force  += prefactor2;

          denominator[0] += sz;
          denominator[1] += sz * nm2;

          if(((mx+my+mz) % 2) == 0) {
            denominator[2] += sz;
            denominator[3] += sz * nm2;
          } else {
            denominator[2] -= sz;
            denominator[3] -= sz * nm2;
          }
        }
      }
    }
  }

  /** shifts the grid points by grid/2 */
  static void calc_meshift(Solver *d) {
    int i;
  
    d->meshift_x = (p3m_int *) realloc(d->meshift_x, d->grid[0]*sizeof(p3m_int));
    d->meshift_y = (p3m_int *) realloc(d->meshift_y, d->grid[1]*sizeof(p3m_int));
    d->meshift_z = (p3m_int *) realloc(d->meshift_z, d->grid[2]*sizeof(p3m_int));
  
    d->meshift_x[0] = d->meshift_y[0] = d->meshift_z[0] = 0;
    for (i = 1; i <= d->grid[RX]/2; i++) {
      d->meshift_x[i] = i;
      d->meshift_x[d->grid[0] - i] = -i;
    }
  
    for (i = 1; i <= d->grid[RY]/2; i++) {
      d->meshift_y[i] = i;
      d->meshift_y[d->grid[1] - i] = -i;
    }
  
    for (i = 1; i <= d->grid[RZ]/2; i++) {
      d->meshift_z[i] = i;
      d->meshift_z[d->grid[2] - i] = -i;
    }
  }
}
