import math

# For a separable filter (horizontal/vertical).

SIGMA = 3.0
KERNEL_RADIUS = 3

assert SIGMA >= 1.0, "The error is probably too big for small sigmas, don't recommend using it."
# TODO: this should be better for SIGMA < 1.0:
# https://web.archive.org/web/20260304022728/https://bartwronski.com/2021/10/31/practical-gaussian-filter-binomial-filter-and-small-sigma-gaussians/

coeffs = []
coeff_sum = 0.0
for i in range(-KERNEL_RADIUS, KERNEL_RADIUS + 1):
    c = math.exp(-i * i / (2.0 * SIGMA * SIGMA))
    coeffs.append(c)
    coeff_sum += c

coeffs = [c / coeff_sum for c in coeffs]

[print(f"{c:.6f}") for c in coeffs]
