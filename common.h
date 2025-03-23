#pragma once

// Structure to hold voxel information
struct newVoxel {
    uint32_t x, y, z;  // 3D coordinates
    uint8_t color; // Color value
};


struct dsVoxel {
    uint8_t layer;
    uint8_t color;
};