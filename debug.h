#pragma once

#include <iostream>
#include <vector>
#include <cstdint>
#include <iomanip>
#include "../libplist/include/plist/plist.h" // Library for handling Apple property list files
#include "common.h" // Debugging functions
// Include any necessary structures

// Optimized function to compact bits (From VoxelMax)
uint32_t compactBits(uint32_t n); 
// Optimized function to decode Morton code using parallel bit manipulation
void decodeMorton3DOptimized(uint32_t morton, uint32_t& x, uint32_t& y, uint32_t& z);



// Function declarations
std::vector<newVoxel> decodeVoxels(const std::vector<uint8_t>& dsData, int mortonOffset);
void printPlistNode(const plist_t& node, int indent = 0);
bool examinePlistNode(const plist_t& root_node, int snapshotIndex, int zIndex, const std::string& arrayPath);
bool debugSnapshots(plist_t element, int snapshotIndex, int zIndex);
void printVoxelTable(const std::vector<newVoxel>& voxels, size_t limit = 100, int filterZ = -1);
void visualizeZPlaneFixed(const std::vector<newVoxel>& voxels, int zPlane, int size = 32);