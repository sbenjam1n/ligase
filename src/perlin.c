// @region:ligase_pd.utils.random.perlin Perlin Noise Generators

#include "perlin.h"
#include <math.h>
#include <stdlib.h>

// @region:ligase_pd.utils.random.perlin.tables Precomputed Permutation Tables

// Ken Perlin's original permutation table (base pattern)
static unsigned char perlin_base[256] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
    35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
    134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
    55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
    18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
    250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
    189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
    172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
    228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
    107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

// Permutation table (256 entries, duplicated for wrapping)
unsigned char perlin_perm[512];

// Initialize permutation table with seed
void perlin_init(unsigned int seed) {
    // Copy base pattern
    for (int i = 0; i < 256; i++) {
        perlin_perm[i] = perlin_base[i];
    }

    // Shuffle using seed
    srand(seed);
    for (int i = 255; i > 0; i--) {
        int j = rand() % (i + 1);
        unsigned char temp = perlin_perm[i];
        perlin_perm[i] = perlin_perm[j];
        perlin_perm[j] = temp;
    }

    // Duplicate for wrapping (avoids modulo operations)
    for (int i = 0; i < 256; i++) {
        perlin_perm[256 + i] = perlin_perm[i];
    }
}

// Reset Perlin noise coordinates for a specific instance (0-3)
void perlin_reset_coords(perlin_state_t *state, int instance) {
    if (instance >= 0 && instance < 4) {
        state->noise_1d_coord[instance] = 0.0f;
        state->noise_2d_coord_x[instance] = 0.0f;
    }
}

// @endregion:ligase_pd.utils.random.perlin.tables

// @region:ligase_pd.utils.random.perlin.perlin_1d 1D Perlin Noise

// Ken Perlin's improved fade function: 6t^5 - 15t^4 + 10t^3
// Provides C2 continuity for smooth derivatives
static inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Linear interpolation
static inline float lerp(float t, float a, float b) {
    return a + t * (b - a);
}

// 1D gradient function (simple: returns -1 or 1 based on hash)
static inline float grad1d(int hash, float x) {
    // Use lowest bit to determine sign
    return (hash & 1) ? -x : x;
}

// Optimized 1D Perlin noise
float perlin1d(float x) {
    // Find unit grid cell containing point
    int X = (int)floorf(x) & 255;  // Bitwise AND for wrapping

    // Relative position within cell (0.0 to 1.0)
    x -= floorf(x);

    // Compute fade curve
    float u = fade(x);

    // Hash coordinates of the 2 corners
    int a = perlin_perm[X];
    int b = perlin_perm[X + 1];

    // Blend results from the 2 corners
    return lerp(u, grad1d(a, x), grad1d(b, x - 1.0f));
}

// @endregion:ligase_pd.utils.random.perlin.perlin_1d

// @region:ligase_pd.utils.random.perlin.perlin_2d 2D Perlin Noise

// 2D gradient function (returns dot product of gradient vector and distance vector)
static inline float grad2d(int hash, float x, float y) {
    // Use hash to select one of 8 gradient directions
    int h = hash & 7;
    float u = (h < 4) ? x : y;
    float v = (h < 4) ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

// Optimized 2D Perlin noise
float perlin2d(float x, float y) {
    // Find unit grid cell containing point
    int X = (int)floorf(x) & 255;  // Bitwise AND for wrapping
    int Y = (int)floorf(y) & 255;

    // Relative position within cell (0.0 to 1.0)
    x -= floorf(x);
    y -= floorf(y);

    // Compute fade curves for x and y
    float u = fade(x);
    float v = fade(y);

    // Hash coordinates of the 4 square corners
    int a  = perlin_perm[X] + Y;
    int aa = perlin_perm[a];
    int ab = perlin_perm[a + 1];
    int b  = perlin_perm[X + 1] + Y;
    int ba = perlin_perm[b];
    int bb = perlin_perm[b + 1];

    // Blend results from the 4 corners
    float x1 = lerp(u, grad2d(aa, x, y), grad2d(ba, x - 1.0f, y));
    float x2 = lerp(u, grad2d(ab, x, y - 1.0f), grad2d(bb, x - 1.0f, y - 1.0f));

    return lerp(v, x1, x2);
}

// @endregion:ligase_pd.utils.random.perlin.perlin_2d

// @region:ligase_pd.utils.random.lorenz Lorenz Attractor

// @region:ligase_pd.utils.random.lorenz.state Lorenz State Management

// Initialize Lorenz attractor with starting position and parameters
void lorenz_init(lorenz_state_t *state, float x0, float y0, float z0, float dt) {
    state->x = x0;
    state->y = y0;
    state->z = z0;

    // Store initial values for reset
    state->x0 = x0;
    state->y0 = y0;
    state->z0 = z0;

    // Standard chaotic parameters
    state->sigma = 10.0f;
    state->rho = 28.0f;
    state->beta = 8.0f / 3.0f;

    // Time step for integration
    state->dt = dt;
}

// Reset Lorenz attractor to initial state
void lorenz_reset(lorenz_state_t *state) {
    state->x = state->x0;
    state->y = state->y0;
    state->z = state->z0;
}

// @endregion:ligase_pd.utils.random.lorenz.state

// @region:ligase_pd.utils.random.lorenz.update Lorenz State Update

// Update Lorenz attractor using Forward Euler method
// Computational cost: ~10 float operations (very efficient)
void lorenz_update(lorenz_state_t *state) {
    // Calculate derivatives (Lorenz equations)
    float dx = state->sigma * (state->y - state->x);
    float dy = state->x * (state->rho - state->z) - state->y;
    float dz = state->x * state->y - state->beta * state->z;

    // Update state using Forward Euler integration
    state->x += dx * state->dt;
    state->y += dy * state->dt;
    state->z += dz * state->dt;

    // STABILITY FIX: Detect divergence and reset to prevent numerical instability
    // If any coordinate exceeds reasonable bounds (±100), reset to small initial values
    // This prevents the attractor from exploding due to numerical errors
    float abs_x = (state->x < 0) ? -state->x : state->x;
    float abs_y = (state->y < 0) ? -state->y : state->y;
    float abs_z = (state->z < 0) ? -state->z : state->z;

    if (abs_x > 100.0f || abs_y > 100.0f || abs_z > 100.0f) {
        // Reset to small non-zero values near the attractor
        state->x = 0.1f;
        state->y = 0.0f;
        state->z = 0.0f;
    }
}

// Get normalized output (0.0 to 1.0) from Lorenz attractor
// axis: 0=X, 1=Y, 2=Z
float lorenz_get_normalized(lorenz_state_t *state, int axis) {
    float value;

    switch (axis) {
        case 0: // X: typically ranges from -20 to 20
            value = (state->x + 20.0f) / 40.0f;
            break;
        case 1: // Y: typically ranges from -30 to 30
            value = (state->y + 30.0f) / 60.0f;
            break;
        case 2: // Z: typically ranges from 0 to 50
            value = state->z / 50.0f;
            break;
        default:
            value = 0.5f;
    }

    // Clamp to [0.0, 1.0] for safety
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    return value;
}

// @endregion:ligase_pd.utils.random.lorenz.update

// @endregion:ligase_pd.utils.random.lorenz

// @region:ligase_pd.utils.random.nbody N-Body Gravitational Simulation

// @region:ligase_pd.utils.random.nbody.state N-Body State Management

#include <math.h>
#include <stdlib.h>

// Initialize N-body system with hierarchical masses (Sun-Earth-Moon approach)
// Different instances use different starting configurations for variety
void nbody_init(nbody_state_t *state, int instance, float dt) {
    // Store initial configuration for reset capability
    state->initial_instance = instance;

    // Simulation parameters - default to stable chaos
    state->epsilon = 0.1f;          // Moderate softening
    state->G = 1.0f;                // Standard gravity
    state->damping = 0.02f;         // Gentle damping
    state->pump_amount = 0.005f;    // Small energy injections
    state->pump_interval = 10;      // Pump every 10 updates
    state->pump_counter = 0;
    state->dt = dt;

    // Initialize normalization bounds
    state->pos_min = -10.0f;
    state->pos_max = 10.0f;

    // Hierarchical masses: large central body + orbiting bodies
    state->mass[0] = 1000.0f;   // "Sun" - gravitational anchor
    state->mass[1] = 1.0f;      // "Planet" - moderate perturbations
    state->mass[2] = 0.01f;     // "Test particle" - maximum chaos

    // Different initial configurations for each instance
    switch (instance) {
        case 0:
            // Configuration 0: Tight hierarchical orbit
            state->pos[0][0] = 0.0f;  state->pos[0][1] = 0.0f;  state->pos[0][2] = 0.0f;
            state->pos[1][0] = 2.0f;  state->pos[1][1] = 0.0f;  state->pos[1][2] = 0.0f;
            state->pos[2][0] = 3.0f;  state->pos[2][1] = 0.5f;  state->pos[2][2] = 0.0f;

            state->vel[0][0] = 0.0f;   state->vel[0][1] = 0.0f;   state->vel[0][2] = 0.0f;
            state->vel[1][0] = 0.0f;   state->vel[1][1] = 22.0f;  state->vel[1][2] = 0.0f;
            state->vel[2][0] = 0.0f;   state->vel[2][1] = 18.0f;  state->vel[2][2] = 0.5f;
            break;

        case 1:
            // Configuration 1: Wide hierarchical orbit
            state->pos[0][0] = 0.0f;  state->pos[0][1] = 0.0f;  state->pos[0][2] = 0.0f;
            state->pos[1][0] = 4.0f;  state->pos[1][1] = 0.0f;  state->pos[1][2] = 0.0f;
            state->pos[2][0] = 5.5f;  state->pos[2][1] = 1.0f;  state->pos[2][2] = 0.0f;

            state->vel[0][0] = 0.0f;   state->vel[0][1] = 0.0f;   state->vel[0][2] = 0.0f;
            state->vel[1][0] = 0.0f;   state->vel[1][1] = 15.8f;  state->vel[1][2] = 0.0f;
            state->vel[2][0] = 0.0f;   state->vel[2][1] = 13.0f;  state->vel[2][2] = 1.0f;
            break;

        case 2:
            // Configuration 2: Eccentric orbit with perturbation
            state->pos[0][0] = 0.0f;  state->pos[0][1] = 0.0f;  state->pos[0][2] = 0.0f;
            state->pos[1][0] = 1.5f;  state->pos[1][1] = 0.5f;  state->pos[1][2] = 0.0f;
            state->pos[2][0] = 2.8f;  state->pos[2][1] = 0.3f;  state->pos[2][2] = 0.5f;

            state->vel[0][0] = 0.0f;   state->vel[0][1] = 0.0f;   state->vel[0][2] = 0.0f;
            state->vel[1][0] = -5.0f;  state->vel[1][1] = 25.0f;  state->vel[1][2] = 0.0f;
            state->vel[2][0] = -3.0f;  state->vel[2][1] = 20.0f;  state->vel[2][2] = 2.0f;
            break;

        case 3:
        default:
            // Configuration 3: Figure-8 inspired (modified for 3 bodies)
            state->pos[0][0] = 0.0f;   state->pos[0][1] = 0.0f;   state->pos[0][2] = 0.0f;
            state->pos[1][0] = 3.5f;   state->pos[1][1] = 0.0f;   state->pos[1][2] = 0.0f;
            state->pos[2][0] = -1.5f;  state->pos[2][1] = 2.0f;   state->pos[2][2] = 0.0f;

            state->vel[0][0] = 0.0f;   state->vel[0][1] = 0.0f;   state->vel[0][2] = 0.0f;
            state->vel[1][0] = 0.0f;   state->vel[1][1] = 17.0f;  state->vel[1][2] = 0.0f;
            state->vel[2][0] = 10.0f;  state->vel[2][1] = 5.0f;   state->vel[2][2] = 0.0f;
            break;
    }
}

// @endregion:ligase_pd.utils.random.nbody.state

// @region:ligase_pd.utils.random.nbody.reset N-Body Reset

// Reset N-body system to its initial configuration
void nbody_reset(nbody_state_t *state) {
    float dt = state->dt;  // Preserve timestep
    int instance = state->initial_instance;  // Get stored configuration

    // Re-initialize with the same configuration
    nbody_init(state, instance, dt);
}

// @endregion:ligase_pd.utils.random.nbody.reset

// @region:ligase_pd.utils.random.nbody.update N-Body Integration

// Update N-body system: calculate forces, integrate, apply damping and pump
void nbody_update(nbody_state_t *state) {
    float accel[NBODY_COUNT][3] = {{0}};  // Acceleration vectors for each body
    float epsilon_sq = state->epsilon * state->epsilon;

    // Calculate gravitational forces between all pairs using softened potential
    for (int i = 0; i < NBODY_COUNT; i++) {
        for (int j = 0; j < NBODY_COUNT; j++) {
            if (i == j) continue;

            // Calculate displacement vector
            float dx = state->pos[j][0] - state->pos[i][0];
            float dy = state->pos[j][1] - state->pos[i][1];
            float dz = state->pos[j][2] - state->pos[i][2];

            // Softened distance: r_soft = sqrt(r² + ε²)
            float dist_sq = dx*dx + dy*dy + dz*dz;
            float inv_dist = 1.0f / sqrtf(dist_sq + epsilon_sq);

            // Softened gravitational force: F = G*m*m / (r² + ε²)^(3/2)
            // This prevents singularity at r=0 (collisions)
            float force_magnitude = state->G * state->mass[j] * inv_dist * inv_dist * inv_dist;

            // Accumulate acceleration (F/m = a)
            accel[i][0] += force_magnitude * dx;
            accel[i][1] += force_magnitude * dy;
            accel[i][2] += force_magnitude * dz;
        }
    }

    // Forward Euler integration with damping
    float damping_factor = 1.0f - state->damping * state->dt;
    for (int i = 0; i < NBODY_COUNT; i++) {
        // Update velocities (v += a*dt)
        state->vel[i][0] += accel[i][0] * state->dt;
        state->vel[i][1] += accel[i][1] * state->dt;
        state->vel[i][2] += accel[i][2] * state->dt;

        // Apply damping to velocities
        state->vel[i][0] *= damping_factor;
        state->vel[i][1] *= damping_factor;
        state->vel[i][2] *= damping_factor;

        // Update positions (x += v*dt)
        state->pos[i][0] += state->vel[i][0] * state->dt;
        state->pos[i][1] += state->vel[i][1] * state->dt;
        state->pos[i][2] += state->vel[i][2] * state->dt;
    }

    // Energy pump: periodic random velocity kicks to maintain bounded chaos
    state->pump_counter++;
    if (state->pump_counter >= state->pump_interval) {
        state->pump_counter = 0;

        // Apply random kicks to smaller bodies (not the central "sun")
        for (int i = 1; i < NBODY_COUNT; i++) {
            float kick_x = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;  // -1 to 1
            float kick_y = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            float kick_z = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;

            state->vel[i][0] += state->pump_amount * kick_x;
            state->vel[i][1] += state->pump_amount * kick_y;
            state->vel[i][2] += state->pump_amount * kick_z;
        }
    }

    // Update normalization bounds (track min/max across all positions)
    float current_min = state->pos[0][0];
    float current_max = state->pos[0][0];
    for (int i = 0; i < NBODY_COUNT; i++) {
        for (int axis = 0; axis < 3; axis++) {
            if (state->pos[i][axis] < current_min) current_min = state->pos[i][axis];
            if (state->pos[i][axis] > current_max) current_max = state->pos[i][axis];
        }
    }

    // Smooth the bounds with exponential decay
    state->pos_min += (current_min - state->pos_min) * 0.01f;
    state->pos_max += (current_max - state->pos_max) * 0.01f;

    // STABILITY: Reset if any body escapes to infinity
    for (int i = 0; i < NBODY_COUNT; i++) {
        float abs_x = (state->pos[i][0] < 0) ? -state->pos[i][0] : state->pos[i][0];
        float abs_y = (state->pos[i][1] < 0) ? -state->pos[i][1] : state->pos[i][1];
        float abs_z = (state->pos[i][2] < 0) ? -state->pos[i][2] : state->pos[i][2];

        if (abs_x > 100.0f || abs_y > 100.0f || abs_z > 100.0f) {
            // Reset to a stable configuration (instance 0)
            nbody_init(state, 0, state->dt);
            return;
        }
    }
}

// @endregion:ligase_pd.utils.random.nbody.update

// @region:ligase_pd.utils.random.nbody.output N-Body Output Mapping

// Get normalized output from N-body system
// mode: 0=Body0 X, 1=Body1 Y, 2=Body2 X, 3=Dist 0-1, 4=Vel0, 5=Vel1, 6=Vel2,
//       7=Dist 0-2, 8=Dist 1-2, 9=AngMom, 10=Energy
float nbody_get_normalized(nbody_state_t *state, int mode) {
    float value = 0.0f;
    float range = state->pos_max - state->pos_min;
    if (range < 0.001f) range = 1.0f;  // Avoid division by zero

    switch (mode) {
        case 0:
            // Body 0 X-position (slowest evolution - central body)
            value = (state->pos[0][0] - state->pos_min) / range;
            break;

        case 1:
            // Body 1 Y-position (moderate chaos - orbiting body)
            value = (state->pos[1][1] - state->pos_min) / range;
            break;

        case 2:
            // Body 2 X-position (maximum chaos - test particle)
            value = (state->pos[2][0] - state->pos_min) / range;
            break;

        case 3: {
            // Distance between Body 0 and Body 1 (relational data)
            float dx = state->pos[1][0] - state->pos[0][0];
            float dy = state->pos[1][1] - state->pos[0][1];
            float dz = state->pos[1][2] - state->pos[0][2];
            float distance = sqrtf(dx*dx + dy*dy + dz*dz);
            // Normalize distance (typical range: 1-10)
            value = (distance - 1.0f) / 9.0f;
            break;
        }

        case 4: {
            // Body 0 velocity magnitude
            float vx = state->vel[0][0];
            float vy = state->vel[0][1];
            float vz = state->vel[0][2];
            float vel_mag = sqrtf(vx*vx + vy*vy + vz*vz);
            // Normalize velocity (typical range: 0-30)
            value = vel_mag / 30.0f;
            break;
        }

        case 5: {
            // Body 1 velocity magnitude
            float vx = state->vel[1][0];
            float vy = state->vel[1][1];
            float vz = state->vel[1][2];
            float vel_mag = sqrtf(vx*vx + vy*vy + vz*vz);
            // Normalize velocity (typical range: 0-30)
            value = vel_mag / 30.0f;
            break;
        }

        case 6: {
            // Body 2 velocity magnitude
            float vx = state->vel[2][0];
            float vy = state->vel[2][1];
            float vz = state->vel[2][2];
            float vel_mag = sqrtf(vx*vx + vy*vy + vz*vz);
            // Normalize velocity (typical range: 0-30)
            value = vel_mag / 30.0f;
            break;
        }

        case 7: {
            // Distance between Body 0 and Body 2
            float dx = state->pos[2][0] - state->pos[0][0];
            float dy = state->pos[2][1] - state->pos[0][1];
            float dz = state->pos[2][2] - state->pos[0][2];
            float distance = sqrtf(dx*dx + dy*dy + dz*dz);
            // Normalize distance (typical range: 1-10)
            value = (distance - 1.0f) / 9.0f;
            break;
        }

        case 8: {
            // Distance between Body 1 and Body 2
            float dx = state->pos[2][0] - state->pos[1][0];
            float dy = state->pos[2][1] - state->pos[1][1];
            float dz = state->pos[2][2] - state->pos[1][2];
            float distance = sqrtf(dx*dx + dy*dy + dz*dz);
            // Normalize distance (typical range: 0-5)
            value = distance / 5.0f;
            break;
        }

        case 9: {
            // Total angular momentum magnitude
            // L = Σ(m_i * r_i × v_i)
            float Lx = 0.0f, Ly = 0.0f, Lz = 0.0f;
            for (int i = 0; i < NBODY_COUNT; i++) {
                float rx = state->pos[i][0];
                float ry = state->pos[i][1];
                float rz = state->pos[i][2];
                float vx = state->vel[i][0];
                float vy = state->vel[i][1];
                float vz = state->vel[i][2];
                float m = state->mass[i];

                // Cross product: r × v
                Lx += m * (ry * vz - rz * vy);
                Ly += m * (rz * vx - rx * vz);
                Lz += m * (rx * vy - ry * vx);
            }
            float L_mag = sqrtf(Lx*Lx + Ly*Ly + Lz*Lz);
            // Normalize angular momentum (typical range: 0-100)
            value = L_mag / 100.0f;
            break;
        }

        case 10: {
            // Total system energy (kinetic + potential)
            float kinetic = 0.0f;
            float potential = 0.0f;

            // Kinetic energy: KE = Σ(0.5 * m_i * v_i²)
            for (int i = 0; i < NBODY_COUNT; i++) {
                float vx = state->vel[i][0];
                float vy = state->vel[i][1];
                float vz = state->vel[i][2];
                float v_sq = vx*vx + vy*vy + vz*vz;
                kinetic += 0.5f * state->mass[i] * v_sq;
            }

            // Potential energy: PE = -Σ(G * m_i * m_j / r_ij)
            float epsilon_sq = state->epsilon * state->epsilon;
            for (int i = 0; i < NBODY_COUNT; i++) {
                for (int j = i + 1; j < NBODY_COUNT; j++) {
                    float dx = state->pos[j][0] - state->pos[i][0];
                    float dy = state->pos[j][1] - state->pos[i][1];
                    float dz = state->pos[j][2] - state->pos[i][2];
                    float dist_sq = dx*dx + dy*dy + dz*dz;
                    float r = sqrtf(dist_sq + epsilon_sq);
                    potential -= state->G * state->mass[i] * state->mass[j] / r;
                }
            }

            float total_energy = kinetic + potential;
            // Normalize energy (typical range: -1000 to 500)
            value = (total_energy + 1000.0f) / 1500.0f;
            break;
        }

        default:
            // Default to mode 0 if invalid
            value = (state->pos[0][0] - state->pos_min) / range;
            break;
    }

    // Clamp to [0.0, 1.0] for safety
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    return value;
}

// One chosen body's position, all three axes normalized to [-1,1] via pos_min/pos_max.
// Mirrors nbody_get_normalized modes 0/1/2 but centers on 0 (box center) and keeps all axes.
// Read-only (const-correct); binds fine to a non-const lvalue.
void nbody_get_normalized_vec3(const nbody_state_t *state, int body, float out[3]) {
    if (body < 0 || body >= NBODY_COUNT) body = 0;   // NBODY_COUNT == 3
    float range = state->pos_max - state->pos_min;   // default 20 (±10)
    if (range < 1e-6f) range = 1.0f;
    for (int a = 0; a < 3; a++)
        out[a] = 2.0f * (state->pos[body][a] - state->pos_min) / range - 1.0f;
}

// @endregion:ligase_pd.utils.random.nbody.output

// @endregion:ligase_pd.utils.random.nbody

// @endregion:ligase_pd.utils.random.perlin
