#pragma once

#include "Common.hlsli"

// Analytic calculation of simplified Rayleigh and Mie scattering.
// Took the implementation from here (with small tweaks):
// https://rhept.org/posts/scattering/
// IMO this looks very decent for the amount of computations it takes and simplicity.
// For more realistic approach this seem to be the way:
// A Scalable and Production Ready Sky and Atmosphere Rendering Technique
// https://sebh.github.io/publications/egsr2020.pdf
// It's a lot more compicated, of course.

float3 CalcSky(float3 viewDirectionWorld, float3 sunDirectionWorld)
{
    const float I0 = 11.0;
    const float3 sigmaRayleigh = float3(0.33, 0.78, 1.89);
    const float sRayleigh = 0.17;
    const float sMie = 0.05;
    const float sSun = 0.07;
    const float wMie = 0.25;
    const float wSun = 0.0002;

    const float dotViewSun = dot(viewDirectionWorld, sunDirectionWorld);
    // Tweaking a little to prevent divisions by zero.
    const float sunY = sunDirectionWorld.y + 1e-5;
    const float viewY = viewDirectionWorld.y + 2e-5;

    const float phaseRayleigh = 3.0 / (16.0 * M_PIf) * (1.0 + dotViewSun * dotViewSun);

    float phaseMie = wMie / (1.0 + wMie + dotViewSun);
    phaseMie = 0.3 * phaseMie * phaseMie;

    float phaseSun = wSun / (1.0 + wSun + dotViewSun);
    phaseSun = 4.0 * phaseSun * phaseSun;

    const float3 sigmaSum = sRayleigh * sigmaRayleigh + sMie;
    const float3 phaseSum =
        sRayleigh * sigmaRayleigh * phaseRayleigh +
        sMie * phaseMie +
        sSun * phaseSun;

    const float surfaceDarkening = viewDirectionWorld.y < 0.0 ? 0.3 : 1.0;

    return I0 * surfaceDarkening *
        sunY / (sunY + viewY) * (phaseSum / sigmaSum) *
        (exp(sigmaSum / sunY) - exp(-sigmaSum / abs(viewY)));
}
