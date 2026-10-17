#pragma once

#include "common.hlsli"

// Analytic calculation of simplified Rayleigh and Mie scattering.
// Took the implementation from here (with small tweaks):
// https://rhept.org/posts/scattering/
// IMO this looks very decent for the amount of computations it takes and simplicity.
// For more realistic approach this seem to be the way:
// A Scalable and Production Ready Sky and Atmosphere Rendering Technique
// https://sebh.github.io/publications/egsr2020.pdf
// It's a lot more compicated, of course.

float3 calc_sky(float3 view_direction_world, float3 sun_direction_world) {
    const float I0 = 11.0f;
    const float3 sigma_rayleigh = float3(0.33f, 0.78f, 1.89f);
    const float s_rayleigh = 0.17f;
    const float s_mie = 0.05f;
    const float s_sun = 0.07f;
    const float w_mie = 0.25f;
    const float w_sun = 0.0002f;

    const float dot_view_sun = dot(view_direction_world, sun_direction_world);
    // Tweaking a little to prevent divisions by zero.
    const float sun_y = sun_direction_world.y + 1e-5f;
    const float view_y = view_direction_world.y + 2e-5f;

    const float phase_rayleigh = 3.0f / (16.0f * M_PIf) * (1.0f + dot_view_sun * dot_view_sun);

    float phase_mie = w_mie / (1.0f + w_mie + dot_view_sun);
    phase_mie = 0.3f * phase_mie * phase_mie;

    float phase_sun = w_sun / (1.0f + w_sun + dot_view_sun);
    phase_sun = 4.0f * phase_sun * phase_sun;

    const float3 sigma_sum = s_rayleigh * sigma_rayleigh + s_mie;
    const float3 phase_sum =
        s_rayleigh * sigma_rayleigh * phase_rayleigh +
        s_mie * phase_mie +
        s_sun * phase_sun;

    const float surface_darkening = view_direction_world.y < 0.0f ? 0.3f : 1.0f;

    return I0 * surface_darkening *
        sun_y / (sun_y + view_y) * (phase_sum / sigma_sum) *
        (exp(sigma_sum / sun_y) - exp(-sigma_sum / abs(view_y)));
}
