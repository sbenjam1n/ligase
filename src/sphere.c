// @region:ligase_pd.utils.random.sphere 3D Sphere Simulation (STK-Based)

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

#include "sphere.h"
#include <math.h>

// @region:ligase_pd.utils.random.sphere.vector3d Vector3D Data Structure

void vector3d_set(vector3d_t *v, float x, float y, float z) {
    v->x = x;
    v->y = y;
    v->z = z;
}

float vector3d_length(const vector3d_t *v) {
    float temp = v->x * v->x;
    temp += v->y * v->y;
    temp += v->z * v->z;
    return sqrtf(temp);
}

void vector3d_add(vector3d_t *result, const vector3d_t *a, const vector3d_t *b) {
    result->x = a->x + b->x;
    result->y = a->y + b->y;
    result->z = a->z + b->z;
}

void vector3d_subtract(vector3d_t *result, const vector3d_t *a, const vector3d_t *b) {
    result->x = a->x - b->x;
    result->y = a->y - b->y;
    result->z = a->z - b->z;
}

void vector3d_scale(vector3d_t *result, const vector3d_t *v, float scalar) {
    result->x = v->x * scalar;
    result->y = v->y * scalar;
    result->z = v->z * scalar;
}

// @endregion:ligase_pd.utils.random.sphere.vector3d

// @region:ligase_pd.utils.random.sphere.parameters User-Controllable Parameters

void sphere_init(sphere_state_t *sphere) {
    // Initialize position and velocity to zero
    vector3d_set(&sphere->position, 0.0f, 0.0f, 0.0f);
    vector3d_set(&sphere->velocity, 0.0f, 0.0f, 0.0f);

    // Default physical properties
    sphere->radius = 1.0f;
    sphere->mass = 1.0f;
    sphere->damping_factor = 0.99f;  // Light damping by default

    // Default boundary box (centered at origin, 10 units on each side)
    sphere->boundary_min_x = -10.0f;
    sphere->boundary_max_x = 10.0f;
    sphere->boundary_min_y = -10.0f;
    sphere->boundary_max_y = 10.0f;
    sphere->boundary_min_z = -10.0f;
    sphere->boundary_max_z = 10.0f;

    // Default collision settings
    sphere->elasticity = 0.8f;  // 80% energy retained on bounce
    sphere->enable_collision = 1;  // Enabled by default
}

void sphere_set_position(sphere_state_t *sphere, float x, float y, float z) {
    vector3d_set(&sphere->position, x, y, z);
}

void sphere_set_velocity(sphere_state_t *sphere, float x, float y, float z) {
    vector3d_set(&sphere->velocity, x, y, z);
}

void sphere_set_mass(sphere_state_t *sphere, float mass) {
    sphere->mass = mass > 0.0f ? mass : 1.0f;
}

void sphere_set_radius(sphere_state_t *sphere, float radius) {
    sphere->radius = radius > 0.0f ? radius : 1.0f;
}

void sphere_set_damping(sphere_state_t *sphere, float damping) {
    // Clamp damping factor to valid range
    if (damping < 0.0f) damping = 0.0f;
    if (damping > 1.0f) damping = 1.0f;
    sphere->damping_factor = damping;
}

void sphere_set_boundaries(sphere_state_t *sphere,
                          float min_x, float max_x,
                          float min_y, float max_y,
                          float min_z, float max_z) {
    sphere->boundary_min_x = min_x;
    sphere->boundary_max_x = max_x;
    sphere->boundary_min_y = min_y;
    sphere->boundary_max_y = max_y;
    sphere->boundary_min_z = min_z;
    sphere->boundary_max_z = max_z;
}

void sphere_set_elasticity(sphere_state_t *sphere, float elasticity) {
    // Clamp elasticity to valid range
    if (elasticity < 0.0f) elasticity = 0.0f;
    if (elasticity > 1.0f) elasticity = 1.0f;
    sphere->elasticity = elasticity;
}

void sphere_set_collision_enabled(sphere_state_t *sphere, int enabled) {
    sphere->enable_collision = enabled;
}

// @endregion:ligase_pd.utils.random.sphere.parameters

// @region:ligase_pd.utils.random.sphere.kick Impulse Application (Kick)

void sphere_add_velocity(sphere_state_t *sphere, float vx, float vy, float vz) {
    // Adapted from STK Sphere::addVelocity()
    sphere->velocity.x += vx;
    sphere->velocity.y += vy;
    sphere->velocity.z += vz;
}

void sphere_kick(sphere_state_t *sphere, float direction_x, float direction_y,
                float direction_z, float magnitude) {
    // Normalize direction vector
    vector3d_t direction;
    vector3d_set(&direction, direction_x, direction_y, direction_z);

    float length = vector3d_length(&direction);
    if (length > 0.0001f) {
        // Scale to unit vector, then multiply by magnitude
        float scale = magnitude / length;
        sphere_add_velocity(sphere,
                          direction.x * scale,
                          direction.y * scale,
                          direction.z * scale);
    }
}

// @endregion:ligase_pd.utils.random.sphere.kick

// @region:ligase_pd.utils.random.sphere.damping Damping (Air Resistance)

static void apply_damping(sphere_state_t *sphere) {
    // Apply damping to simulate air resistance
    sphere->velocity.x *= sphere->damping_factor;
    sphere->velocity.y *= sphere->damping_factor;
    sphere->velocity.z *= sphere->damping_factor;
}

// @endregion:ligase_pd.utils.random.sphere.damping

// @region:ligase_pd.utils.random.sphere.collision Collision Detection and Response

static void check_collisions(sphere_state_t *sphere) {
    if (!sphere->enable_collision) {
        return;
    }

    // Check X boundaries
    if (sphere->position.x - sphere->radius < sphere->boundary_min_x) {
        sphere->position.x = sphere->boundary_min_x + sphere->radius;
        sphere->velocity.x = -sphere->velocity.x * sphere->elasticity;
    } else if (sphere->position.x + sphere->radius > sphere->boundary_max_x) {
        sphere->position.x = sphere->boundary_max_x - sphere->radius;
        sphere->velocity.x = -sphere->velocity.x * sphere->elasticity;
    }

    // Check Y boundaries
    if (sphere->position.y - sphere->radius < sphere->boundary_min_y) {
        sphere->position.y = sphere->boundary_min_y + sphere->radius;
        sphere->velocity.y = -sphere->velocity.y * sphere->elasticity;
    } else if (sphere->position.y + sphere->radius > sphere->boundary_max_y) {
        sphere->position.y = sphere->boundary_max_y - sphere->radius;
        sphere->velocity.y = -sphere->velocity.y * sphere->elasticity;
    }

    // Check Z boundaries
    if (sphere->position.z - sphere->radius < sphere->boundary_min_z) {
        sphere->position.z = sphere->boundary_min_z + sphere->radius;
        sphere->velocity.z = -sphere->velocity.z * sphere->elasticity;
    } else if (sphere->position.z + sphere->radius > sphere->boundary_max_z) {
        sphere->position.z = sphere->boundary_max_z - sphere->radius;
        sphere->velocity.z = -sphere->velocity.z * sphere->elasticity;
    }
}

// @endregion:ligase_pd.utils.random.sphere.collision

// @region:ligase_pd.utils.random.sphere.physics Physics Update (Velocity, Position Integration)

void sphere_tick(sphere_state_t *sphere, float time_increment) {
    // Adapted from STK Sphere::tick()
    // Update position based on velocity (Euler integration)
    sphere->position.x += time_increment * sphere->velocity.x;
    sphere->position.y += time_increment * sphere->velocity.y;
    sphere->position.z += time_increment * sphere->velocity.z;

    // Apply damping to velocity
    apply_damping(sphere);

    // Check for boundary collisions and respond
    check_collisions(sphere);
}

// @endregion:ligase_pd.utils.random.sphere.physics

// @region:ligase_pd.utils.random.sphere.output Output Mapping (X, Y, Z, Velocity Magnitude)

float sphere_get_velocity_magnitude(const sphere_state_t *sphere) {
    return vector3d_length(&sphere->velocity);
}

float sphere_get_x(const sphere_state_t *sphere) {
    return sphere->position.x;
}

float sphere_get_y(const sphere_state_t *sphere) {
    return sphere->position.y;
}

float sphere_get_z(const sphere_state_t *sphere) {
    return sphere->position.z;
}

float sphere_get_velocity_x(const sphere_state_t *sphere) {
    return sphere->velocity.x;
}

float sphere_get_velocity_y(const sphere_state_t *sphere) {
    return sphere->velocity.y;
}

float sphere_get_velocity_z(const sphere_state_t *sphere) {
    return sphere->velocity.z;
}

float sphere_get_normalized(const sphere_state_t *sphere, int mode) {
    float value = 0.0f;
    float pos_range_x = sphere->boundary_max_x - sphere->boundary_min_x;
    float pos_range_y = sphere->boundary_max_y - sphere->boundary_min_y;
    float pos_range_z = sphere->boundary_max_z - sphere->boundary_min_z;

    // Estimate velocity range based on boundary size and typical physics
    // (velocity can theoretically be unbounded, but we use boundary size as scaling)
    float vel_range = (pos_range_x + pos_range_y + pos_range_z) / 3.0f;
    if (vel_range < 0.001f) vel_range = 10.0f;  // Default range if boundaries are tiny

    switch (mode) {
        case 0:  // Position X (normalized to boundary range)
            value = (sphere->position.x - sphere->boundary_min_x) / pos_range_x;
            break;
        case 1:  // Position Y (normalized to boundary range)
            value = (sphere->position.y - sphere->boundary_min_y) / pos_range_y;
            break;
        case 2:  // Position Z (normalized to boundary range)
            value = (sphere->position.z - sphere->boundary_min_z) / pos_range_z;
            break;
        case 3:  // Velocity X (normalized to ±vel_range)
            value = (sphere->velocity.x + vel_range) / (2.0f * vel_range);
            break;
        case 4:  // Velocity Y (normalized to ±vel_range)
            value = (sphere->velocity.y + vel_range) / (2.0f * vel_range);
            break;
        case 5:  // Velocity Z (normalized to ±vel_range)
            value = (sphere->velocity.z + vel_range) / (2.0f * vel_range);
            break;
        case 6:  // Velocity magnitude (normalized to vel_range)
            value = sphere_get_velocity_magnitude(sphere) / vel_range;
            break;
        default:
            value = 0.5f;  // Fallback
            break;
    }

    // Clamp to [0.0, 1.0]
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    return value;
}

// All three position axes normalized to [-1,1] via the boundary box, in one call.
// Mirrors sphere_get_normalized modes 0/1/2 but centers on 0 (box center) and keeps all axes.
void sphere_get_normalized_vec3(const sphere_state_t *sphere, float out[3]) {
    float rx = sphere->boundary_max_x - sphere->boundary_min_x;   // default 20 (±10)
    float ry = sphere->boundary_max_y - sphere->boundary_min_y;
    float rz = sphere->boundary_max_z - sphere->boundary_min_z;
    out[0] = (rx > 1e-6f) ? 2.0f * (sphere->position.x - sphere->boundary_min_x) / rx - 1.0f : 0.0f;
    out[1] = (ry > 1e-6f) ? 2.0f * (sphere->position.y - sphere->boundary_min_y) / ry - 1.0f : 0.0f;
    out[2] = (rz > 1e-6f) ? 2.0f * (sphere->position.z - sphere->boundary_min_z) / rz - 1.0f : 0.0f;
}

// @endregion:ligase_pd.utils.random.sphere.output

// @endregion:ligase_pd.utils.random.sphere
