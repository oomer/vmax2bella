// oomer wrapper code for opengametools voxel conversion

#pragma once

#include "oomer_voxel_vmax.h"

#include <vector>
#include <string>
#include <stdio.h>
#include <stdint.h>
#include <cstdlib>
#include <cstring>

#include "../opengametools/src/ogt_vox.h"

// Only define implementation once to avoid redefinition errors
#ifndef OGT_VOXEL_MESHIFY_IMPLEMENTATION
#define OGT_VOXEL_MESHIFY_IMPLEMENTATION
#endif
#include "../opengametools/src/ogt_voxel_meshify.h"

// Convert a VmaxModel to an ogt_vox_model
// Note: The returned ogt_vox_model must be freed using ogt_vox_free when no longer needed
/*ogt_vox_model* convert_vmax_to_ogt_vox(const VmaxModel& vmaxModel) {
    // Use max dimensions from vmaxModel
    // Add 1 to get the actual size (since coordinates are 0-based)
    uint32_t size_x = vmaxModel.maxx+1;
    uint32_t size_y = vmaxModel.maxy+1;
    uint32_t size_z = vmaxModel.maxz+1;
    
    // Add some safety checks
    if (size_x > 256 || size_y > 256 || size_z > 256) {
        std::cout << "Warning: Model dimensions exceed 256 limit. Clamping to 256." << std::endl;
        size_x = std::min(size_x, 256u);
        size_y = std::min(size_y, 256u);
        size_z = std::min(size_z, 256u);
    }
    if (size_x == 0 || size_y == 0 || size_z == 0) {
        std::cout << "Error: Model has zero dimensions. Setting minimum size of 1x1x1." << std::endl;
        size_x = std::max(size_x, 1u);
        size_y = std::max(size_y, 1u);
        size_z = std::max(size_z, 1u);
    }
    // Create the voxel data array (initialized to 0, which means empty in ogt_vox)
    size_t voxel_count = size_x * size_y * size_z;
    uint8_t* voxel_data = (uint8_t*)ogt_vox_malloc(voxel_count);
    if (!voxel_data) {
        std::cout << "Error: Failed to allocate memory for voxel data" << std::endl;
        return nullptr;
    }
    memset(voxel_data, 0, voxel_count); // Initialize all to 0 (empty)
    
    // Fill the voxel data array with color indices
    int voxel_count_populated = 0;
    for (uint32_t x = 0; x < size_x; x++) {
        for (uint32_t y = 0; y < size_y; y++) {
            for (uint32_t z = 0; z < size_z; z++) {
                // Calculate the index in the 1D array
                size_t index = x + (y * size_x) + (z * size_x * size_y);
                
                if (index < voxel_count) {
                    try {
                        // Use the new hasVoxelsAt and getVoxelsAt methods instead of directly accessing voxelsSpatial
                        if (vmaxModel.hasVoxelsAt(x, y, z)) {
                            const std::vector<VmaxVoxel>& voxels = vmaxModel.getVoxelsAt(x, y, z);
                            if (!voxels.empty()) {
                                uint8_t palette_index = voxels[0].palette;
                                if (palette_index == 0) palette_index = 1; // If palette is 0, use 1 instead to make it visible
                                voxel_data[index] = palette_index;
                                voxel_count_populated++;
                            }
                        }
                        // If no voxels at this position, it remains 0 (empty)
                    }
                    catch (const std::exception& e) {
                        std::cout << "ERROR: Exception accessing voxels at [" << x << "][" << y << "][" << z 
                                  << "]: " << e.what() << std::endl;
                        ogt_vox_free(voxel_data);
                        return nullptr;
                    }
                }
            }
        }
    }
    
    // Create and initialize the ogt_vox_model
    ogt_vox_model* model = (ogt_vox_model*)ogt_vox_malloc(sizeof(ogt_vox_model));
    if (!model) {
        std::cout << "Error: Failed to allocate memory for ogt_vox_model" << std::endl;
        ogt_vox_free(voxel_data);
        return nullptr;
    }
    
    model->size_x = size_x;
    model->size_y = size_y;
    model->size_z = size_z;
    model->voxel_data = voxel_data;
    
    // Calculate a simple hash for the voxel data
    uint32_t hash = 0;
    for (size_t i = 0; i < voxel_count; i++) {
        hash = hash * 65599 + voxel_data[i];
    }
    model->voxel_hash = hash;
    
    return model;
}
*/


// Convert a vector of VmaxVoxel to an ogt_vox_model
// Note: The returned ogt_vox_model must be freed using ogt_vox_free when no longer needed
ogt_vox_model* convert_voxelsoftype_to_ogt_vox(const std::vector<VmaxVoxel>& voxelsOfType) {
    // Find the maximum dimensions from the voxels
    uint32_t size_x = 0;
    uint32_t size_y = 0;
    uint32_t size_z = 0;
   
    // WARNING must add 1 to each dimension
    // because voxel coordinates are 0-based
    for (const auto& voxel : voxelsOfType) {
        size_x = std::max(size_x, static_cast<uint32_t>(voxel.x)+1);  // this seems wasteful 
        size_y = std::max(size_y, static_cast<uint32_t>(voxel.y)+1);
        size_z = std::max(size_z, static_cast<uint32_t>(voxel.z)+1);
    }
    
    // Add some safety checks
    // This is a dense voxel model, so we need to make sure it's not too large
    // todo use a sparse storage like morton
    if (size_x > 256 || size_y > 256 || size_z > 256) {
        std::cout << "Warning: Model dimensions exceed 256 limit. Clamping to 256." << std::endl;
        size_x = std::min(size_x, 256u);
        size_y = std::min(size_y, 256u);
        size_z = std::min(size_z, 256u);
    }
    if (size_x == 0 || size_y == 0 || size_z == 0) {
        std::cout << "Error: Model has zero dimensions. Setting minimum size of 1x1x1." << std::endl;
        size_x = std::max(size_x, 1u);
        size_y = std::max(size_y, 1u);
        size_z = std::max(size_z, 1u);
    }
    
    // Create the voxel data array (initialized to 0, which means empty in ogt_vox)
    size_t voxel_count = size_x * size_y * size_z;
    uint8_t* voxel_data = (uint8_t*)ogt_vox_malloc(voxel_count);
    if (!voxel_data) {
        std::cout << "Error: Failed to allocate memory for voxel data" << std::endl;
        return nullptr;
    }
    memset(voxel_data, 0, voxel_count); // Initialize all to 0 (empty)
    
    // Fill the voxel data array with color indices
    int voxel_count_populated = 0;
    
    // Loop through the vector of voxels directly
    for (const auto& voxel : voxelsOfType) {
        // Get the coordinates and palette
        uint32_t x = voxel.x;
        uint32_t y = voxel.y;
        uint32_t z = voxel.z;
       
        // Skip voxels outside our valid range
        if (x >= size_x || y >= size_y || z >= size_z)
            continue;
            
        // Calculate the index in the 1D array
        size_t index = x + (y * size_x) + (z * size_x * size_y);
        
        if (index < voxel_count) {
            uint8_t palette_index = 0; // hardcoded for now
            if (palette_index == 0) palette_index = 1; // If palette is 0, use 1 instead to make it visible
            voxel_data[index] = palette_index;
            voxel_count_populated++;
        }
    }
    
    // Create the model
    ogt_vox_model* model = (ogt_vox_model*)ogt_vox_malloc(sizeof(ogt_vox_model));
    if (!model) {
        std::cout << "Error: Failed to allocate memory for model" << std::endl;
        ogt_vox_free(voxel_data);
        return nullptr;
    }
    
    model->size_x = size_x;
    model->size_y = size_y;
    model->size_z = size_z;
    model->voxel_data = voxel_data;
    
    // Calculate a simple hash for the voxel data
    uint32_t hash = 0;
    for (size_t i = 0; i < voxel_count; i++) {
        hash = hash * 65599 + voxel_data[i];
    }
    model->voxel_hash = hash;
    
    return model;
}

// Free resources allocated for an ogt_vox_model created by convert_vmax_to_ogt_vox
void free_ogt_vox_model(ogt_vox_model* model) {
    if (model) {
        if (model->voxel_data) {
            ogt_vox_free((void*)model->voxel_data);
        }
        ogt_vox_free(model);
    }
}

// Free resources allocated for an ogt_vox_scene created by create_ogt_vox_scene_from_vmax
/*void free_ogt_vox_scene(ogt_vox_scene* scene) {
    if (scene) {
        // Free each model
        for (uint32_t i = 0; i < scene->num_models; i++) {
            free_ogt_vox_model((ogt_vox_model*)scene->models[i]);
        }
        
        // Free pointers
        if (scene->models) ogt_vox_free((void*)scene->models);
        if (scene->instances) ogt_vox_free((void*)scene->instances);
        if (scene->layers) ogt_vox_free((void*)scene->layers);
        
        // Free the scene itself
        ogt_vox_free(scene);
    }
}
*/

// Custom allocator functions for ogt_voxel_meshify
static void* voxel_meshify_malloc(size_t size, void* user_data) {
    return malloc(size);
}

static void voxel_meshify_free(void* ptr, void* user_data) {
    free(ptr);
}
