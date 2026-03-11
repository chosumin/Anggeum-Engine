// Shared SDF bounds buffer layout and decode utilities.
// The bounds are stored as sortable-encoded uints for GPU atomic min/max.
// Consumer shaders decode on read ? no separate finalize pass needed.

float sdfDecodeBound(uint encoded)
{
    uint mask = ((encoded >> 31) - 1u) | 0x80000000u;
    return uintBitsToFloat(encoded ^ mask);
}

// Call after reading from the raw buffer to get world-space AABB with padding
void sdfDecodeBounds(in uint rawBounds[8], float paddingFactor,
                     out vec3 boundsMin, out vec3 boundsMax)
{
    vec3 rawMin = vec3(
        sdfDecodeBound(rawBounds[0]),
        sdfDecodeBound(rawBounds[1]),
        sdfDecodeBound(rawBounds[2]));
    vec3 rawMax = vec3(
        sdfDecodeBound(rawBounds[4]),
        sdfDecodeBound(rawBounds[5]),
        sdfDecodeBound(rawBounds[6]));

    vec3 padding = (rawMax - rawMin) * paddingFactor;
    boundsMin = rawMin - padding;
    boundsMax = rawMax + padding;
}