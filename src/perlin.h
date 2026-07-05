// @region:ligase_pd.utils.random.perlin Perlin Noise Generators

#ifndef LIGASE_PERLIN_H
#define LIGASE_PERLIN_H

#include "types.h"

// @region:ligase_pd.utils.random.perlin.tables Precomputed Permutation Tables

// Permutation table for Perlin noise (256 entries, duplicated for wrapping)
extern unsigned char perlin_perm[512];

// Initialize permutation table with seed
void perlin_init(unsigned int seed);

// Reset Perlin noise coordinates for a specific instance (0-3)
void perlin_reset_coords(perlin_state_t *state, int instance);

// @endregion:ligase_pd.utils.random.perlin.tables

// @region:ligase_pd.utils.random.perlin.perlin_1d 1D Perlin Noise

// Optimized 1D Perlin noise function
// Returns value in range [-1.0, 1.0]
// Uses single-precision float math for efficiency
float perlin1d(float x);

// @endregion:ligase_pd.utils.random.perlin.perlin_1d

// @region:ligase_pd.utils.random.perlin.perlin_2d 2D Perlin Noise

// Optimized 2D Perlin noise function
// Returns value in range [-1.0, 1.0]
// Uses single-precision float math and bitwise operations for efficiency
float perlin2d(float x, float y);

// @endregion:ligase_pd.utils.random.perlin.perlin_2d

// @region:ligase_pd.utils.random.lorenz Lorenz Attractor

// Initialize Lorenz attractor state
// Parameters: sigma (typically 10.0), rho (typically 28.0), beta (typically 8/3)
void lorenz_init(lorenz_state_t *state, float x0, float y0, float z0, float dt);

// Reset Lorenz attractor to initial state
void lorenz_reset(lorenz_state_t *state);

// Update Lorenz attractor state using Forward Euler method
// Very efficient: only ~10 operations per call
void lorenz_update(lorenz_state_t *state);

// Get normalized output from Lorenz attractor
// Returns X, Y, or Z coordinate normalized to [0.0, 1.0]
// axis: 0=X, 1=Y, 2=Z
float lorenz_get_normalized(lorenz_state_t *state, int axis);

// @endregion:ligase_pd.utils.random.lorenz

// @region:ligase_pd.utils.random.nbody N-Body Gravitational Simulation

// Initialize N-body system with hierarchical masses for stable chaos
// Instance parameter: 0-3 for different starting configurations
// dt: integration timestep (typically 0.005)
void nbody_init(nbody_state_t *state, int instance, float dt);

// Reset N-body system to its initial configuration
void nbody_reset(nbody_state_t *state);

// Update N-body system using Forward Euler with softened potential
// Applies gravitational forces, damping, and periodic energy pump
// Cost: ~30-40 float operations per call
void nbody_update(nbody_state_t *state);

// Get normalized output from N-body system
// mode: 0=Body0 X, 1=Body1 Y, 2=Body2 X, 3=Dist 0-1, 4=Vel0, 5=Vel1, 6=Vel2,
//       7=Dist 0-2, 8=Dist 1-2, 9=AngMom, 10=Energy
// Returns value normalized to [0.0, 1.0] using auto-adjusting bounds
float nbody_get_normalized(nbody_state_t *state, int mode);

// Get one chosen body's position, all three axes normalized to [-1,1] via pos_min/pos_max.
// Single point of truth for "nbody 3D -> normalized" used by spatial granulation (pan_mode 2).
// out[0]=x (L..R), out[1]=y (down..up), out[2]=z (back..front); centered so 0 = box center.
void nbody_get_normalized_vec3(const nbody_state_t *state, int body, float out[3]);

// @endregion:ligase_pd.utils.random.nbody

#endif // LIGASE_PERLIN_H

// @endregion:ligase_pd.utils.random.perlin
