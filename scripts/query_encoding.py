"""Shared deterministic query encoding; no workload-specific validation."""

MASK64 = (1 << 64) - 1


def mix(value: int) -> int:
    value = (value + 0x9E3779B97F4A7C15) & MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return value ^ (value >> 31)


def permuted_rank(position: int, n: int, seed: int) -> int:
    if n == 1:
        return 0
    bits = (n - 1).bit_length()
    bits += bits & 1
    half = bits // 2
    mask = (1 << half) - 1
    value = position
    while True:
        left, right = value >> half, value & mask
        for round_id in range(8):
            f = mix(right ^ mix(seed + round_id * 0x9E3779B97F4A7C15)) & mask
            left, right = right, left ^ f
        value = (left << half) | right
        if value < n:
            return value


def vertex_from_rank(rank: int, x: int, y: int) -> int:
    value = rank
    first, second = sorted((x, y))
    if value >= first:
        value += 1
    if value >= second:
        value += 1
    return value
