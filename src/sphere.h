// @region:ligase_pd.utils.random.sphere 3D Sphere Simulation (STK-Based)

#ifndef SPHERE_H
#define SPHERE_H

/*
 * 3D Sphere Physics Simulation
 * Adapted from STK (Synthesis ToolKit in C++)
 * Original code by Perry R. Cook and Gary P. Scavone, 1995-2023
 *
 * This is a C adaptation of the STK Vector3D and Sphere classes
 * for use in Pure Data externals.
 *
 * STK License (MIT-style):
 * Copyright (c) 1995-2023 Perry R. Cook and Gary P. Scavone
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 */

// @region:ligase_pd.utils.random.sphere.vector3d Vector3D Data Structure

typedef struct {
    float x;
    float y;
    float z;
} vector3d_t;

// @endregion:ligase_pd.utils.random.sphere.vector3d

// @region:ligase_pd.utils.random.sphere.state Sphere Physical State

typedef struct {
    vector3d_t position;     // Current 3D position
    vector3d_t velocity;     // Current 3D velocity
    float radius;            // Sphere radius
    float mass;              // Sphere mass
    float damping_factor;    // Air resistance (0.0-1.0, where 1.0 = no damping)

    // Boundary collision settings
    float boundary_min_x;
    float boundary_max_x;
    float boundary_min_y;
    float boundary_max_y;
    float boundary_min_z;
    float boundary_max_z;
    float elasticity;        // Bounce coefficient (0.0-1.0)
    int enable_collision;    // Enable boundary collision detection
    float spin_rate;         // SOURCE SHAPE: velocity-vector rotation rate about the
                             // y-axis, radians/sim-second (-10..10, default 0 = off).
                             // Energy-neutral curl: |v| is preserved, so it cannot
                             // blow up and composes with damping/elasticity/kick.
} sphere_state_t;

// @endregion:ligase_pd.utils.random.sphere.state

// @region:ligase_pd.utils.random.sphere.vector3d Vector3D Operations

// Initialize a vector
void vector3d_set(vector3d_t *v, float x, float y, float z);

// Get vector length (magnitude)
float vector3d_length(const vector3d_t *v);

// Add two vectors
void vector3d_add(vector3d_t *result, const vector3d_t *a, const vector3d_t *b);

// Subtract two vectors (result = a - b)
void vector3d_subtract(vector3d_t *result, const vector3d_t *a, const vector3d_t *b);

// Scale vector by scalar
void vector3d_scale(vector3d_t *result, const vector3d_t *v, float scalar);

// @endregion:ligase_pd.utils.random.sphere.vector3d

// @region:ligase_pd.utils.random.sphere.parameters User-Controllable Parameters

// Initialize sphere with default values
void sphere_init(sphere_state_t *sphere);

// Set sphere position
void sphere_set_position(sphere_state_t *sphere, float x, float y, float z);

// Set sphere velocity
void sphere_set_velocity(sphere_state_t *sphere, float x, float y, float z);

// Set sphere mass
void sphere_set_mass(sphere_state_t *sphere, float mass);

// Set sphere radius
void sphere_set_radius(sphere_state_t *sphere, float radius);

// Set damping factor (0.0 = max damping, 1.0 = no damping)
void sphere_set_damping(sphere_state_t *sphere, float damping);

// Set boundary box for collisions
void sphere_set_boundaries(sphere_state_t *sphere,
                          float min_x, float max_x,
                          float min_y, float max_y,
                          float min_z, float max_z);

// Set elasticity (bounce coefficient)
void sphere_set_elasticity(sphere_state_t *sphere, float elasticity);

// Enable/disable collision detection
void sphere_set_collision_enabled(sphere_state_t *sphere, int enabled);

// Set spin rate (velocity-vector rotation about the y-axis, clamped to -10..10;
// 0 = off -> the update is bit-identical to the spinless sim)
void sphere_set_spin(sphere_state_t *sphere, float spin_rate);

// @endregion:ligase_pd.utils.random.sphere.parameters

// @region:ligase_pd.utils.random.sphere.kick Impulse Application (Kick)

// Add velocity impulse (the "kick")
void sphere_add_velocity(sphere_state_t *sphere, float vx, float vy, float vz);

// Apply a kick with specified direction and magnitude
void sphere_kick(sphere_state_t *sphere, float direction_x, float direction_y,
                float direction_z, float magnitude);

// @endregion:ligase_pd.utils.random.sphere.kick

// @region:ligase_pd.utils.random.sphere.physics Physics Update (Velocity, Position Integration)

// Update sphere physics for one time step
void sphere_tick(sphere_state_t *sphere, float time_increment);

// @endregion:ligase_pd.utils.random.sphere.physics

// @region:ligase_pd.utils.random.sphere.output Output Mapping (X, Y, Z, Velocity Magnitude)

// Get velocity magnitude (speed)
float sphere_get_velocity_magnitude(const sphere_state_t *sphere);

// Get position component
float sphere_get_x(const sphere_state_t *sphere);
float sphere_get_y(const sphere_state_t *sphere);
float sphere_get_z(const sphere_state_t *sphere);

// Get velocity component
float sphere_get_velocity_x(const sphere_state_t *sphere);
float sphere_get_velocity_y(const sphere_state_t *sphere);
float sphere_get_velocity_z(const sphere_state_t *sphere);

// Get normalized output value (0.0-1.0) based on mode
// Mode 0-6: Pos X, Pos Y, Pos Z, Vel X, Vel Y, Vel Z, Velocity Magnitude
float sphere_get_normalized(const sphere_state_t *sphere, int mode);

// Get all three position axes normalized to [-1,1] via the boundary box, in one call.
// Single point of truth for "sphere 3D -> normalized" used by spatial granulation (pan_mode 2).
// out[0]=x (L..R), out[1]=y (down..up), out[2]=z (back..front); centered so 0 = box center.
void sphere_get_normalized_vec3(const sphere_state_t *sphere, float out[3]);

// @endregion:ligase_pd.utils.random.sphere.output

#endif // SPHERE_H

// @endregion:ligase_pd.utils.random.sphere
