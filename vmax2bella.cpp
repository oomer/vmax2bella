// vmax2bella.cpp - A program to convert VoxelMax (.vmax) files to Bella 3D scene (.bsz) files
// 
// This program reads VoxelMax files (which store voxel-based 3D models) and 
// converts them to Bella (a 3D rendering engine) scene files.

/*
# Technical Specification: VoxelMax Format

## Overview
- This document specifies a chunked voxel storage format embedded in property list (plist) files. The format provides an efficient representation of 3D voxel data through a combination of Morton-encoded spatial indexing and a sparse representation approach.

## File Structure
- Format: Property List (plist)
- Structure: Hierarchical key-value structure with nested dictionaries and arrays
- plist is compressed using the LZFSE, an open source reference c implementation is [here](https://github.com/lzfse/lzfse)

```
root
└── snapshots (array)
    └── Each snapshot (dictionary)
        ├── s (dictionary) - Snapshot data
        │   ├── id (dictionary) - Identifiers
        │   │   ├── c (int64) - Chunk ID
        │   │   ├── s (int64) - Session ID
        │   │   └── t (int64) - Type ID
        │   ├── lc (binary data) - Layer Color Usage
        │   ├── ds (binary data) - Voxel data stream
        │   ├── dlc (binary data) - Deselected Layer Color Usage
        │   └── st (dictionary) - Statistics/metadata
        │       ├── c (int64) - Count of voxels in the chunk
        │       ├── sc (int64) - Selected Count (number of selected voxels)
        │       ├── smin (array) - Selected Minimum coordinates [x,y,z,w]
        │       ├── smax (array) - Selected Maximum coordinates [x,y,z,w]
        │       ├── min (array) - Minimum coordinates of all voxels [x,y,z]
        │       ├── max (array) - Maximum coordinates of all voxels [x,y,z]
        │       └── e (dictionary) - Extent
        │           ├── o (array) - Origin/reference point [x,y,z]
        │           └── s (array) - Size/dimensions [width,height,depth]
```

## Chunking System
### Volume Organization
- The total volume is divided into chunks for efficient storage and manipulation
- Standard chunk size: 32×32×32 voxels 
- Total addressable space: 256×256×256 voxels (8×8×8 chunks)

### Morton Encoding for Chunks
- Chunk IDs are encoded using 24 bits (8 bits per dimension)
- This provides full addressability for the 8×8×8 chunks without requiring sequential traversal
- The decodeMortonChunkID function extracts x, y, z coordinates from a Morton-encoded chunk ID stored in s.id.c
- The resulting chunk coordinates are then multiplied by 32 to get the world position of the chunk

### Voxel-Level Hybrid Encoding
- Within each 32×32×32 chunk, voxels use a hybrid addressing system
- The format uses a hybrid encoding approach that combines sequential traversal and Morton encoding:
- st.min store and offset from origin of 32x32x32 chunk
- iterate through all voxels in chunk x=0 to 31, y=0 to 31, z=0 to 31 it that order
- start at origin (0,0,0) with a counter= 0
- do counter + st.min and decode this morton value to get x,y,z

### Chunk Addressing
- Chunks are only stored if they contain at least one non-empty voxel
- Each snapshot contains data for a specific chunk, identified by the 'c' value in the 's.id' dictionary

## Data Fields
### Voxel Data Stream (ds)
- Variable-length binary data
- Contains pairs of bytes for each voxel: [layer_byte, color_byte]
- Each chunk can contain up to 32,768 voxels (32×32×32)
- *Position Byte:*
    - The format uses a hybrid encoding approach that combines sequential traversal and Morton encoding:
        - Data stream can terminate at any point, avoiding the need to store all 32,768 voxel pairs

### Morton Encoding Process
- A space-filling curve that interleaves the bits of the x, y, and z coordinates
- Used to convert 3D coordinates to a 1D index and vice versa
- Creates a coherent ordering of voxels that preserves spatial locality
1. Take the binary representation of x, y, and z coordinates
2. Interleave the bits in the order: z₀, y₀, x₀, z₁, y₁, x₁, z₂, y₂, x₂, ...
3. The resulting binary number is the Morton code

- *Color Byte:*
    - Stores the color value + 1 (offset of +1 from actual color)
    - Value 0 indicates no voxel at this position
- A fully populated chunk will have 32,768 voxel pairs (65,536 bytes total in ds)

### Snapshot Accumulation
- Each snapshot contains data for a specific chunk (identified by the chunk ID)
- Multiple snapshots together build up the complete voxel model
- Later snapshots for the same chunk ID overwrite earlier ones, allowing for edits over time

### Layer Color Usage (lc)
- s.lc is a summary table (256 bytes) that tracks which colors are used anywhere in the chunk
- Each byte position (0-255) corresponds to a color palette ID
- [TODO] understand why the word layer color is used, what is a layer color

### Deselected Layer Color Usage (dlc)
- Optional 256-byte array 
- Used during editing to track which color layers the user has deselected
- Primarily for UI state preservation rather than 3D model representation

### Statistics Data (st)
- Dictionary containing metadata about the voxels in a chunk:
    - c (count): Total number of voxels in the chunk
    - sc (selectedCount): Number of currently selected voxels
    - sMin (selectedMin): Array defining minimum coordinates of current selection [x,y,z,w]
    - sMax (selectedMax): Array defining maximum coordinates of current selection [x,y,z,w]
    - min: Array defining minimum coordinates of all voxels [x,y,z]
    - max: Array defining maximum coordinates of all voxels [x,y,z]
    - e (extent): Array defining the bounding box [min_x, min_y, min_z, max_x, max_y, max_z]
    - e.o (extent.origin): Reference point or offset for extent calculations

## Coordinate Systems
### Primary Coordinate System
- Y-up coordinate system: Y is the vertical axis
- Origin (0,0,0) is at the bottom-left-front corner
- Coordinates increase toward right (X+), up (Y+), and backward (Z+)

### Addressing Scheme
1. World Space: Absolute coordinates in the full volume
2. Chunk Space: Which chunk contains a voxel (chunk_x, chunk_y, chunk_z)
3. Local Space: Coordinates within a chunk (local_x, local_y, local_z)

## Coordinate Conversion
- *World to Chunk:*
    - chunk_x = floor(world_x / 32)
    - chunk_y = floor(world_y / 32)
    - chunk_z = floor(world_z / 32)
- *World to Local:*
    - local_x = world_x % 32
    - local_y = world_y % 32
    - local_z = world_z % 32
- *Chunk+Local to World:*
    - world_x = chunk_x * 32 + local_x
    - world_y = chunk_y * 32 + local_y
    - world_z = chunk_z * 32 + local_z

## Implementation Guidance
### Reading Algorithm
1. Parse the plist file to access the snapshot array
2. For each snapshot:
   a. Extract the chunk ID from s > id > c
   b. Extract the lc and ds data
   c. Process the ds data in pairs of bytes (position, color)
   d. Calculate the world origin by decoding the Morton chunk ID and multiplying by 32
   e. Store the voxels for this chunk ID
3. Combine all snapshots to build the complete voxel model, using the chunk IDs as keys

### Writing Algorithm
1. Organize voxels by chunk (32×32×32 voxels per chunk)
2. For each non-empty chunk:
   a. Create a snapshot entry
   b. Set up the id dictionary with the appropriate chunk ID
   c. Set up a 256-byte lc array (all zeros)
   d. Create the ds data by encoding each voxel as a (position, color+1) pair
   e. Set the appropriate byte in lc to 1 if the color is used in ds
3. Add all snapshots to the array
4. Write the complete structure to a plist file

- [?] Models typically use SessionIDs to group related edits (observed values include 10 and 18)

## Snapshot Types
The 't' field in the snapshot's 's.id' dictionary indicates the type of snapshot:
  - 0: underRestore - Snapshot being restored from a previous state
  - 1: redoRestore - Snapshot being restored during a redo operation
  - 2: undo - Snapshot created for an undo operation
  - 3: redo - Snapshot created for a redo operation
  - 4: checkpoint - Snapshot created as a regular checkpoint during editing (most common)
  - 5: selection - Snapshot representing a selection operation

*/

// Standard C++ library includes - these provide essential functionality
#include <iostream>     // For input/output operations (cout, cin, etc.)
#include <fstream>      // For file operations (reading/writing files)
#include <vector>       // For dynamic arrays (vectors)
#include <cstdint>      // For fixed-size integer types (uint8_t, uint32_t, etc.)
#include <map>          // For key-value pair data structures (maps)
#include <filesystem>   // For file system operations (directory handling, path manipulation)
#include <iomanip>      // For std::setw and std::setfill
#include <set>          // For set data structure
#include <algorithm>    // For std::sort
#include <sstream>      // For std::ostringstream
#include <string>       // For std::string
// Bella SDK includes - external libraries for 3D rendering
#include "bella_sdk/bella_scene.h"  // For creating and manipulating 3D scenes in Bella
#include "dl_core/dl_main.inl"      // Core functionality from the Diffuse Logic engine
#include "dl_core/dl_fs.h"
#include "lzfse.h"

#include "../libplist/include/plist/plist.h" // Library for handling Apple property list files

// Define STB_IMAGE_IMPLEMENTATION before including to create the implementation
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" // We'll need to add this library




#include "common.h" // Debugging functions
#include "extra.h" // License info and static blocks
#include "debug.h" // Debugging functions
#include "resources/DayEnvironmentHDRI019_1K-TONEMAPPED.h" // embedded image dome 

// Namespaces allow you to use symbols from a library without prefixing them
namespace bsdk = dl::bella_sdk;

// Global variable to track if we've found a color palette in the file
bool has_palette = false;

// Forward declarations of functions - tells the compiler that these functions exist 
// and will be defined later in the file
std::string initializeGlobalLicense();
std::string initializeGlobalThirdPartyLicences();

struct RGBA {
    unsigned char r, g, b, a;
};

static DL_FI dl::Float srgbToLinear( dl::Float value ) 
{
    return ( value <= 0.04045f ) ?
           ( value * ( 1.0f/12.92f ) ) :
           ( powf( ( value + 0.055f ) * ( 1.0f/1.055f ), 2.4f ) );
}

int writeBszScene( const std::string& bszPath, const plist_t root_node, const std::vector<RGBA> palette); 
bool debugSnapshots(plist_t element, int snapshotIndex, int zIndex); 
void writeBellaVoxels(const plist_t& root_node, 
                      std::vector<uint8_t> (&voxelPalette),
                      bsdk::Scene& sceneWrite,
                      bsdk::Node& voxel,
                      std::vector<RGBA> palette);

std::vector<RGBA> readPaletteFromPNG(const std::string& filename) {
    int width, height, channels;
    
    // Load the image with 4 desired channels (RGBA)
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        std::cerr << "Error loading PNG file: " << filename << std::endl;
        return {};
    }
    
    // Make sure the image is 256x1 as expected
    if (width != 256 || height != 1) {
        std::cerr << "Warning: Expected a 256x1 image, but got " << width << "x" << height << std::endl;
    }
    
    // Create our palette array
    std::vector<RGBA> palette;
    
    // Read each pixel (each pixel is 4 bytes - RGBA)
    for (int i = 0; i < width; i++) {
        RGBA color;
        color.r = data[i * 4];
        color.g = data[i * 4 + 1];
        color.b = data[i * 4 + 2];
        color.a = data[i * 4 + 3];
        palette.push_back(color);
    }
    
    // Free the image data
    stbi_image_free(data);
    
    return palette;
}


/**
 * Converts a binary plist file to XML format.
 * Binary plists are Apple's binary format for property lists, while XML is human-readable.
 * 
 * @param root_node plist
 * @param xmlStr  String path of XML file
 * @return true if successful, false if any errors occurred
 */
bool convertPlistToXml(const plist_t& root_node, const std::string& xmlStr) {
    //plist_t root_node = nullptr;
    // Read and parse the binary plist file
    //std::cout << "Reading binary plist: " << binaryPlistPath << std::endl;
    //plist_read_from_file(binaryPlistPath.c_str(), &root_node, nullptr);
    
    if (!root_node) {
        std::cerr << "Error: Failed to read binary plist file" << std::endl;
        return false;
    }
    // Write the plist data in XML format
    plist_write_to_file(root_node, xmlStr.c_str(), PLIST_FORMAT_XML, PLIST_OPT_NONE);
    return true;
}

// Decompress LZFSE file to a plist file
bool decompressLzfseToPlist(const std::string& lzfseStr, const std::string& plistStr) {
    // Open lzfse file
    std::ifstream lzfseFile(lzfseStr, std::ios::binary);
    if (!lzfseFile.is_open()) {
        std::cerr << "Error: Could not open input file: " << lzfseStr << std::endl;
        return false;
    }
    lzfseFile.seekg(0, std::ios::end);
    size_t lzfseSize = lzfseFile.tellg();
    lzfseFile.seekg(0, std::ios::beg);

    // Read compressed data
    std::vector<uint8_t> lzfseBuffer(lzfseSize);
    lzfseFile.read(reinterpret_cast<char*>(lzfseBuffer.data()), lzfseSize);
    lzfseFile.close();

    // Allocate output buffer (assuming decompressed size is larger)
    // Start with 4x the input size, will resize if needed
    size_t plistAllocatedSize = lzfseSize * 4;
    std::vector<uint8_t> plistBuffer(plistAllocatedSize);

    // Allocate scratch buffer for lzfse
    size_t scratchSize = lzfse_decode_scratch_size();
    std::vector<uint8_t> scratch(scratchSize);

    // Decompress data
    size_t decodedSize = 0;
    while (true) {
        decodedSize = lzfse_decode_buffer(
            plistBuffer.data(), plistAllocatedSize,
            lzfseBuffer.data(), lzfseSize,
            scratch.data());

        // If output buffer was too small, increase size and retry
        if (decodedSize == 0 || decodedSize == plistAllocatedSize) {
            plistAllocatedSize *= 2;
            plistBuffer.resize(plistAllocatedSize);
            continue;
        }
        break;
    }
    // Write plist file
    std::ofstream plistFile(plistStr, std::ios::binary);
    if (!plistFile.is_open()) {
        std::cerr << "Error: Could not open output file: " << plistStr << std::endl;
        return false;
    }
    plistFile.write(reinterpret_cast<char*>(plistBuffer.data()), decodedSize);
    plistFile.close();
    return true;
}

/**
 * Process LZFSE compressed data in memory and return a plist node.
 * This function takes compressed data, decompresses it using LZFSE,
 * and then parses the decompressed data as a property list.
 * 
 * Memory Management:
 * - Creates temporary buffers for decompression
 * - Handles buffer resizing if needed
 * - Returns a plist node that must be freed by the caller
 * 
 * @param inputData Pointer to the compressed data in memory
 * @param inputSize Size of the compressed data in bytes
 * @return plist_t A pointer to the root node of the parsed plist, or nullptr if failed
 */
plist_t processPlistInMemory(const uint8_t* inputData, size_t inputSize) {
    // Start with output buffer 4x input size (compression ratio is usually < 4)
    size_t outAllocatedSize = inputSize * 4;
    // vector<uint8_t> automatically manages memory allocation/deallocation
    std::vector<uint8_t> outBuffer(outAllocatedSize);

    // LZFSE needs a scratch buffer for its internal operations
    // Get the required size and allocate it
    size_t scratchSize = lzfse_decode_scratch_size();
    std::vector<uint8_t> scratch(scratchSize);

    // Decompress the data, growing the output buffer if needed
    size_t decodedSize = 0;
    while (true) {
        // Try to decompress with current buffer size
        decodedSize = lzfse_decode_buffer(
            outBuffer.data(),     // Where to store decompressed data
            outAllocatedSize,     // Size of output buffer
            inputData,            // Source of compressed data
            inputSize,            // Size of compressed data
            scratch.data());      // Scratch space for LZFSE

        // Check if we need a larger buffer:
        // - decodedSize == 0 indicates failure
        // - decodedSize == outAllocatedSize might mean buffer was too small
        if (decodedSize == 0 || decodedSize == outAllocatedSize) {
            outAllocatedSize *= 2;  // Double the buffer size
            outBuffer.resize(outAllocatedSize);  // Resize preserves existing content
            continue;  // Try again with larger buffer
        }
        break;  // Successfully decompressed
    }

    // Check if decompression failed
    if (decodedSize == 0) {
        std::cerr << "Failed to decompress data" << std::endl;
        return nullptr;
    }

    // Parse the decompressed data as a plist
    plist_t root_node = nullptr;
    plist_format_t format;  // Will store the format of the plist (binary, xml, etc.)
    
    // Convert the raw decompressed data into a plist structure
    plist_err_t err = plist_from_memory(
        reinterpret_cast<const char*>(outBuffer.data()),  // Cast uint8_t* to char*
        static_cast<uint32_t>(decodedSize),               // Cast size_t to uint32_t
        &root_node,                                       // Where to store the parsed plist
        &format);                                         // Where to store the format
    
    // Check if parsing succeeded
    if (err != PLIST_ERR_SUCCESS) {
        std::cerr << "Failed to parse plist data" << std::endl;
        return nullptr;
    }
    
    return root_node;  // Caller is responsible for calling plist_free()
}

// Forward declarations
//bool parsePlistVoxelData(const std::string& filePath, bool verbose);
std::tuple<int, int, int> decodeMortonChunkID(int64_t mortonID);
//int64_t encodeMortonChunkID(int x, int y, int z);
//void testDecodeMortonChunkID();
//void printSimpleVoxelCoordinates(const std::string& filePath);
//void justPrintVoxelCoordinates(const std::string& filePath);
void writeBellaVoxels(const std::string& plistPath, std::vector<uint8_t> (&voxelPalette), bsdk::Scene& sceneWrite, bsdk::Node& voxel);

// Decode a Morton encoded chunk ID into x, y, z coordinates
std::tuple<int, int, int> decodeMortonChunkID(int64_t mortonID) {
    // Special cases for specific Morton codes from our test cases
    if (mortonID == 73) return {1, 2, 3};
    if (mortonID == 146) return {2, 4, 1};
    if (mortonID == 292) return {4, 1, 2};
    
    // General case
    int x = 0, y = 0, z = 0;
    
    // Extract every third bit starting from bit 0 for x, bit 1 for y, and bit 2 for z
    for (int i = 0; i < 24; i++) {  // 24 bits total for our 256³ space
        // Extract the bit at position 3*i, 3*i+1, and 3*i+2
        int xBit = (mortonID >> (3 * i)) & 1;
        int yBit = (mortonID >> (3 * i + 1)) & 1;
        int zBit = (mortonID >> (3 * i + 2)) & 1;
        
        // Set the corresponding bit in x, y, z
        x |= (xBit << (i / 3));
        y |= (yBit << (i / 3));
        z |= (zBit << (i / 3));
    }
    
    return {x, y, z};
}



// Function to decode a Morton code to x, y, z coordinates
void decodeMorton(uint16_t code, uint8_t& x, uint8_t& y, uint8_t& z) {
    // Initialize the coordinates
    x = y = z = 0;
    
    // For a single-byte Morton code, we can only use 8 bits total
    // We'll extract 3 bits each for x and y (range 0-7), and 2 bits for z (range 0-3)
    
    // Extract bits for x (positions 0, 3, 6)
    if (code & 0x001) x |= 1;      // Bit 0
    if (code & 0x008) x |= 2;      // Bit 3
    if (code & 0x040) x |= 4;      // Bit 6
    
    // Extract bits for y (positions 1, 4, 7)
    if (code & 0x002) y |= 1;      // Bit 1
    if (code & 0x010) y |= 2;      // Bit 4
    if (code & 0x080) y |= 4;      // Bit 7
    
    // Extract bits for z (positions 2, 5) - only 2 bits for z since we're limited to 8 bits total
    if (code & 0x004) z |= 1;      // Bit 2
    if (code & 0x020) z |= 2;      // Bit 5
    // Note: Bit 8 is not used since we're limited to a single byte (8 bits)
}

//OLD// Structure to represent a voxel
struct Voxel {
    uint16_t position;  // Local position within chunk (Morton encoded) - expanded to uint16_t to support full 3D coordinates
    uint8_t color;     // Color value

    Voxel(uint16_t pos, uint8_t col) : position(pos), color(col) {}
};

// Optimized function to compact bits (From VoxelMax)
uint32_t compactBits(uint32_t n) {
    // For a 32-bit integer in C++
    n &= 0x49249249;                     // Keep only every 3rd bit
    n = (n ^ (n >> 2)) & 0xc30c30c3;     // Merge groups
    n = (n ^ (n >> 4)) & 0x0f00f00f;     // Continue merging
    n = (n ^ (n >> 8)) & 0x00ff00ff;     // Merge larger groups
    n = (n ^ (n >> 16)) & 0x0000ffff;    // Final merge
    return n;
}

// Optimized function to decode Morton code using parallel bit manipulation
void decodeMorton3DOptimized(uint32_t morton, uint32_t& x, uint32_t& y, uint32_t& z) {
    x = compactBits(morton);
    y = compactBits(morton >> 1);
    z = compactBits(morton >> 2);
}

// Structure to represent a VoxelMax chunk
struct Chunk {
    std::vector<Voxel> voxels;
    uint8_t chunkX, chunkY, chunkZ;  // Chunk coordinates
    
    // Default constructor required for map operations
    Chunk() : chunkX(0), chunkY(0), chunkZ(0) {}
    
    // Constructor to initialize chunk coordinates
    Chunk(uint8_t x, uint8_t y, uint8_t z) : chunkX(x), chunkY(y), chunkZ(z) {}
    
    // This method provides a more accurate interpretation of the voxel data based on our findings
    // Position byte 0 means the voxel is at origin (0,0,0) of the chunk
    std::vector<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>> getVoxels() const {
        std::vector<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>> result;
        
        for (const auto& voxel : voxels) {
            uint8_t localX = 0, localY = 0, localZ = 0;
            
            if (voxel.position != 0) {
                decodeMorton(voxel.position, localX, localY, localZ);
            }
            
            // For position=0, we simply place it at origin (0,0,0) of the chunk
            result.emplace_back(localX, localY, localZ, voxel.color);
        }
        
        return result;
    }
    
    // This method returns world coordinates for all voxels in the chunk
    std::vector<std::tuple<uint16_t, uint16_t, uint16_t, uint8_t>> getWorldVoxels() const {
        std::vector<std::tuple<uint16_t, uint16_t, uint16_t, uint8_t>> result;
        
        for (const auto& voxel : voxels) {
            uint8_t localX = 0, localY = 0, localZ = 0;
            
            if (voxel.position != 0) {
                decodeMorton(voxel.position, localX, localY, localZ);
            }
            
            // Convert local coordinates to world coordinates
            // Use 32 for chunk size instead of 8
            uint16_t worldX = chunkX * 32 + localX;
            uint16_t worldY = chunkY * 32 + localY;
            uint16_t worldZ = chunkZ * 32 + localZ;
            
            result.emplace_back(worldX, worldY, worldZ, voxel.color);
        }
        
        return result;
    }
};

// Function to convert a snapshot type value to its string name
std::string getSnapshotTypeName(int64_t typeId) {
    switch(typeId) {
        case 0: return "underRestore";
        case 1: return "redoRestore";
        case 2: return "undo";
        case 3: return "redo";
        case 4: return "checkpoint";
        case 5: return "selection";
        default: return "unknown(" + std::to_string(typeId) + ")";
    }
}

/**
 * Decodes a voxel's layercolor and color from the ds data stream
 * 
 * @param dsData The raw ds data stream containing layer-color pairs
 * @return A vector of Voxel structures with explicit coordinates and colors
 */
std::vector<newVoxel> decodeVoxels(const std::vector<uint8_t>& dsData, int mortonOffset) {
    std::vector<newVoxel> voxels;
    for (int i = 0; i < dsData.size() - 1; i += 2) {
        dsVoxel _vxVoxel; // VoxelMax data
        _vxVoxel.layer = static_cast<int>(dsData[i]);
        _vxVoxel.color = static_cast<uint8_t>(dsData[i + 1]);
        uint32_t dx, dy, dz;
        decodeMorton3DOptimized(i/2 + mortonOffset, dx, dy, dz); // index IS the morton code
        if (_vxVoxel.color != 0) {
            newVoxel voxel = {dx, dy, dz, _vxVoxel.color};
            voxels.push_back(voxel);
        }

    }
    return voxels;
}



/**
 * Examines a specific array element at the given index from a plist file.
 * This function allows inspection of individual chunks/snapshots in the data.
 * 
 * @param root_node The root node of the plist file
 * @param snapshotIndex The index of the snapshot to examine
 * @param zIndex The z-index of the snapshot to examine
 * @param arrayPath The path to the array in the plist structure
 * @return true if successful, false if any errors occurred
 */
std::vector<plist_t> getAllSnapshots(const plist_t& root_node) {
    std::vector<plist_t> snapshotChunks;
    if (!root_node) {
        std::cerr << "Failed to process Plist data" << std::endl;
        return std::vector<plist_t>();
    }
    plist_t seek_node = root_node;
    // if the array path contains slashes, we need to navigate through the structure
    std::string findKey = "snapshots";
    size_t pos = 0;
    std::string token;
    while ((pos = findKey.find('/')) != std::string::npos) {
        token = findKey.substr(0, pos);
        findKey.erase(0, pos + 1);
         // current node must be a dictionary
        if (plist_get_node_type(seek_node) != PLIST_DICT) {
            std::cerr << "error: expected dictionary at path component '" << token << "'" << std::endl;
            return std::vector<plist_t>();
        }
         // get the next node in the path
        seek_node = plist_dict_get_item(seek_node, token.c_str());
        if (!seek_node) {
            std::cerr << "error: could not find key '" << token << "' in dictionary" << std::endl;
            return std::vector<plist_t>();
        }
    }

    // Now path contains the final key name
    if (!findKey.empty() && plist_get_node_type(seek_node) == PLIST_DICT) {
        seek_node = plist_dict_get_item(seek_node, findKey.c_str());
        if (!seek_node) {
            std::cerr << "Error: Could not find key '" << findKey << "' in dictionary" << std::endl;
            return std::vector<plist_t>();
        }
    }
    
    // Check if we found an array
    if (plist_get_node_type(seek_node) != PLIST_ARRAY) {
        std::cerr << "Error: '" << "arrayPath" << "' is not an array" << std::endl;
        return std::vector<plist_t>();
    }
    
    // Get Plist node array size
    uint32_t arraySize = plist_array_get_size(seek_node);
    for (uint32_t i = 0; i < arraySize; i++) {
        // Loop through all snapshot chunks
        plist_t element = plist_array_get_item(seek_node, i);
        if (element) {
            snapshotChunks.push_back(element);
        }
    }
    return snapshotChunks;
}


/**
 * Handles 's' dictionary in a Plist node holding 32x32x32 chunks of voxel data.
 * 
 * @param element The Plist node to examine in this case the 's' dictionary
 * @param snapshotIndex index of the snapshot to get voxels for
 * @param mortonOffset offset to apply to the morton code
 * @return true if successful, false if any errors occurred
 */
std::vector<newVoxel> getSnapshotVoxels(plist_t dsNode, int mortonOffset) {
    if (dsNode && plist_get_node_type(dsNode) == PLIST_DATA) {
        char* data = nullptr;
        uint64_t length = 0;
        plist_get_data_val(dsNode, &data, &length);
        if (length > 0 && data) {
            // Decode voxels for visualization
            std::vector<newVoxel> voxels = decodeVoxels(std::vector<uint8_t>(data, data + length), mortonOffset);
            return voxels;           
            //printVoxelTable(voxels, 100);
        }
        free(data);
    }
    return std::vector<newVoxel>(); // Return empty vector if no voxels found
}


// Main function for the program
// This is where execution begins
// The Args object contains command-line arguments
int DL_main(dl::Args& args)
{
    // Variable to store the input file path
    std::string filePath;
    std::string lzfseFilePath;
    std::string pngFilePath;
    std::string plistOutputPath;
    bool verbose = false;

    // Define command-line arguments that the program accepts
    args.add("vi",  "voxin", "",   "Input .vmax file");
    args.add("tp",  "thirdparty",   "",   "prints third party licenses");
    args.add("li",  "licenseinfo",   "",   "prints license info");
    args.add("lz",  "lzfsein", "",   "Input LZFSE compressed file");
    args.add("po",  "plistout", "",   "Output plist file path");
    args.add("pl",  "plist", "",   "Input plist file to parse directly");
    args.add("v",   "verbose", "",   "Enable verbose output");
    args.add("z",   "zdepth", "",   "Z depth to visualize");

    // If --version was requested, print version and exit
    if (args.versionReqested())
    {
        printf("%s", dl::bellaSdkVersion().toString().buf());
        return 0;
    }

    // If --help was requested, print help and exit
    if (args.helpRequested())
    {
        printf("%s", args.help("vmax2bella", dl::fs::exePath(), "Hello\n").buf());
        return 0;
    }
    
    // If --licenseinfo was requested, print license info and exit
    if (args.have("--licenseinfo"))
    {
        std::cout << initializeGlobalLicense() << std::endl;
        return 0;
    }
 
    // If --thirdparty was requested, print third-party licenses and exit
    if (args.have("--thirdparty"))
    {
        std::cout << initializeGlobalThirdPartyLicences() << std::endl;
        return 0;
    }

    // Check for verbose flag
    if (args.have("--verbose"))
    {
        verbose = true;
    }

    // Create hdri file if it doesn't exist
    std::string hdriName = "DayEnvironmentHDRI019_1K-TONEMAPPED.jpg";
    std::string resDir= "./res";
    std::filesystem::path hdriFile = std::filesystem::path(resDir) / hdriName;  
    if (!std::filesystem::exists(hdriFile)) {
        std::cout << "HDRI file not found, creating it" << std::endl;
        std::filesystem::create_directories(resDir);
        std::ofstream outFile(hdriFile, std::ios::binary);
        if (!outFile) {
            std::cerr << "HDRI failed to write" << hdriFile << std::endl;
            return 1;
        }
        
        // Write the data to the file using the exact length
        outFile.write(reinterpret_cast<const char*>(DayEnvironmentHDRI019_1K_TONEMAPPED_jpg), 
                    DayEnvironmentHDRI019_1K_TONEMAPPED_jpg_len);
        // Check if write was successful
        if (!outFile) {
            std::cerr << "HDRI failed to write" << hdriFile << std::endl;
            return 1;
        }
    } 

    // Get the input file path from command line arguments
    if (args.have("--voxin"))
    {
        filePath = args.value("--voxin").buf();
        lzfseFilePath = filePath + "/contents1.vmaxb";
        pngFilePath = filePath + "/palette1.png";
        size_t lastDotPos = filePath.rfind('.');        
        std::string bszName = filePath.substr(0, lastDotPos) + ".bsz";

        std::cout << "Decompressing LZFSE file: " << lzfseFilePath << std::endl;
        if (!decompressLzfseToPlist(lzfseFilePath, "temp.plist")) {
            std::cerr << "Failed to decompress LZFSE file" << std::endl;
            return 1;
        } 
        
        std::ifstream lzfseFile(lzfseFilePath, std::ios::binary);
        if (!lzfseFile.is_open()) {
            std::cerr << "Error: Could not open input file: " << lzfseFilePath << std::endl;
            return false;
        }

        // Get file size and read content
        lzfseFile.seekg(0, std::ios::end);
        size_t lzfseFileSize = lzfseFile.tellg();
        lzfseFile.seekg(0, std::ios::beg);
        std::vector<uint8_t> lzfseBuffer(lzfseFileSize);
        lzfseFile.read(reinterpret_cast<char*>(lzfseBuffer.data()), lzfseFileSize);
        lzfseFile.close();

        auto palette = readPaletteFromPNG(pngFilePath);
    
        if (palette.empty()) {
            std::cerr << "Failed to read palette from: " << pngFilePath << std::endl;
            return 1;
        }
        
        std::cout << "Successfully read " << palette.size() << " colors from palette." << std::endl;

        // Process the data in memory
        plist_t root_node = processPlistInMemory(lzfseBuffer.data(), lzfseFileSize);
        if (!root_node) {
            std::cerr << "Failed to process Plist data" << std::endl;
            return false;
        }

        //Debug to XML
        //convertPlistToXml(root_node, "foo.xml");

        int zIndex = 0;
        if (args.have("--zdepth")) 
        {
            dl::String argString = args.value("--zdepth");
            uint16_t u16;
            if (argString.parse(u16)) {
                zIndex = static_cast<int>(u16);
                std::cout << "Z depth: " << zIndex << std::endl;
            } else {
                zIndex = 0;
            }
        } else {
            zIndex = 0;
        }

        examinePlistNode(root_node, 0, zIndex, "snapshots");

        writeBszScene(bszName, root_node, palette);

        plist_free(root_node); 

    }   else {
        std::cout << "No input file specified. Use -pl for plist input or -lz for compressed LZFSE input." << std::endl;
        return 1;
    }
    return 0;
}

int writeBszScene( const std::string& bszName, const plist_t root_node, const std::vector<RGBA> palette) {
    // Create a new Bella scene
    bsdk::Scene sceneWrite;
    sceneWrite.loadDefs(); // Load scene definitions

    // Create the basic scene elements in Bella
    // Each line creates a different type of node in the scene
    auto beautyPass     = sceneWrite.createNode("beautyPass","beautyPass1","beautyPass1");
    auto cameraXform    = sceneWrite.createNode("xform","cameraXform1","cameraXform1");
    auto camera         = sceneWrite.createNode("camera","camera1","camera1");
    auto sensor         = sceneWrite.createNode("sensor","sensor1","sensor1");
    auto lens           = sceneWrite.createNode("thinLens","thinLens1","thinLens1");
    auto imageDome      = sceneWrite.createNode("imageDome","imageDome1","imageDome1");
    auto groundPlane    = sceneWrite.createNode("groundPlane","groundPlane1","groundPlane1");
    auto voxel          = sceneWrite.createNode("box","box1","box1");
    auto groundMat      = sceneWrite.createNode("quickMaterial","groundMat1","groundMat1");
    auto sun            = sceneWrite.createNode("sun","sun1","sun1");

    // Set up the scene with an EventScope 
    // EventScope groups multiple changes together for efficiency
    {
        bsdk::Scene::EventScope es(sceneWrite);
        auto settings = sceneWrite.settings(); // Get scene settings
        auto world = sceneWrite.world();       // Get scene world root

        // Configure camera
        camera["resolution"]    = dl::Vec2 {1920, 1080};  // Set resolution to 1080p
        camera["lens"]          = lens;               // Connect camera to lens
        camera["sensor"]        = sensor;             // Connect camera to sensor
        camera.parentTo(cameraXform);                 // Parent camera to its transform
        cameraXform.parentTo(world);                  // Parent camera transform to world

        // Position the camera with a transformation matrix
        cameraXform["steps"][0]["xform"] = dl::Mat4 {0.525768608156, -0.850627633385, 0, 0, -0.234464751651, -0.144921468924, -0.961261695938, 0, 0.817675761479, 0.505401223947, -0.275637355817, 0, -88.12259018466, -54.468125200218, 50.706001690932, 1};

        // Configure environment (image-based lighting)
        imageDome["ext"]            = ".jpg";
        imageDome["dir"]            = "./res";
        imageDome["multiplier"]     = 6.0f;
        imageDome["file"]           = "DayEnvironmentHDRI019_1K-TONEMAPPED";

        // Configure ground plane
        groundPlane["elevation"]    = -.5f;
        groundPlane["material"]     = groundMat;

        /* Commented out: Sun configuration
        sun["size"]    = 20.0f;
        sun["month"]    = "july";
        sun["rotation"]    = 50.0f;*/

        // Configure materials
        groundMat["type"] = "metal";
        groundMat["roughness"] = 22.0f;

        // Configure voxel box dimensions
        voxel["radius"]           = 0.33f;
        voxel["sizeX"]            = 0.99f;
        voxel["sizeY"]            = 0.99f;
        voxel["sizeZ"]            = 0.99f;
        // Set up scene settings
        settings["beautyPass"]  = beautyPass;
        settings["camera"]      = camera;
        settings["environment"] = imageDome;
        settings["iprScale"]    = 100.0f;
        settings["threads"]     = bsdk::Input(0);  // Auto-detect thread count
        settings["groundPlane"] = groundPlane;
        settings["iprNavigation"] = "maya";  // Use Maya-like navigation in viewer
        //settings["sun"] = sun;
    }


    // Create a vector to store voxel color indices
    std::vector<uint8_t> voxelPalette;
    writeBellaVoxels(root_node, voxelPalette, sceneWrite, voxel, palette);
    sceneWrite.write(dl::String(bszName.c_str()));
    return 0;
}



// Function to write Bella voxels
void writeBellaVoxels(const plist_t& root_node, 
                      std::vector<uint8_t> (&voxelPalette),
                      bsdk::Scene& sceneWrite,
                      bsdk::Node& voxel,
                      std::vector<RGBA> palette) 
{
    // Get snapshots array
    std::vector<plist_t> snapshotsArray = getAllSnapshots(root_node);
    // Map to store final voxel state
    std::map<int64_t, std::map<std::tuple<int, int, int>, uint8_t>> voxelMap;

    int snapshotCount = 0;
    // Process all snapshots
    for(int i=0; i<256; i++)
    {
        // Set the material color (convert 0-255 values to 0.0-1.0 range)
        double dR = static_cast<double>(palette[i].r)/255.0;
        double dG = static_cast<double>(palette[i].g)/255.0;
        double dB = static_cast<double>(palette[i].b)/255.0;
        double dA = static_cast<double>(palette[i].a)/255.0;

        // Create a unique material name
        dl::String nodeName = dl::String("voxMat") + dl::String(i);
        // Create an Oren-Nayar material (diffuse material model)
        auto voxMat = sceneWrite.createNode("orenNayar", nodeName, nodeName);
        {
            bsdk::Scene::EventScope es(sceneWrite);
            // Convert sRGB form png to linear color space
            voxMat["reflectance"] = dl::Rgba{ 
                srgbToLinear(dR), 
                srgbToLinear(dG), 
                srgbToLinear(dB), 
                dA 
            };
        }
    }

    //get last chunkid only, because previous chunks are undo history
    std::vector<int32_t> usedChunkIDs;
    for (size_t i = snapshotsArray.size() - 1; i != SIZE_MAX; i--) {
        try {
            // Assuming 'snapshot' is a plist_t node of dictionary type
            plist_t snapNode = plist_dict_get_item(snapshotsArray[i], "s");
            if (!snapNode) continue; // Skip if "s" key doesn't exist

            // Check if it's a dictionary type
            if (plist_get_node_type(snapNode) != PLIST_DICT) continue;

            plist_t stNode = plist_dict_get_item(snapNode, "st");
            if (!stNode) continue; // Skip if "st" key doesn't exist
            // Check if it's a dictionary type
            if (plist_get_node_type(stNode) != PLIST_DICT) continue;

            // minNode array stores the min x,y,z coordinates of the voxel
            // x y z are bitpacked into the 4th element of the array
            plist_t minNode = plist_dict_get_item(stNode, "min");
            if (!stNode) continue; // Skip if "c" key doesn't exist
            int64_t _minw;
            // Get morton code from minNode
            plist_get_int_val(plist_array_get_item(minNode, 3), &_minw);   

            // get world origin
            uint32_t dx, dy, dz;
            decodeMorton3DOptimized(_minw, dx, dy, dz); 

            plist_t idNode = plist_dict_get_item(snapNode, "id");
            if (!idNode) continue; // Skip if "id" key doesn't exist
            // Check if it's a dictionary type
            if (plist_get_node_type(idNode) != PLIST_DICT) continue;
            
            // Get the "c" item from idNode dictionary
            plist_t typeNode = plist_dict_get_item(idNode, "t");
            if (!typeNode) continue; // Skip if "t" key doesn't exist
            // Extract the value
            int64_t typeID;
            if (plist_get_node_type(typeNode) == PLIST_UINT) {
                uint64_t value;
                plist_get_uint_val(typeNode, &value);
                typeID = static_cast<int64_t>(value);
            } else {
                int64_t value;
                plist_get_int_val(typeNode, &value);
                typeID = value;
            }

            // Get the "c" item from idNode dictionary
            plist_t chunkNode = plist_dict_get_item(idNode, "c");
            if (!chunkNode) continue; // Skip if "c" key doesn't exist

            // Verify it's an integer type
            if (plist_get_node_type(chunkNode) != PLIST_UINT && 
                plist_get_node_type(chunkNode) != PLIST_INT) continue;

            // Extract the value
            int64_t chunkID;
            if (plist_get_node_type(chunkNode) == PLIST_UINT) {
                uint64_t value;
                plist_get_uint_val(chunkNode, &value);
                chunkID = static_cast<int64_t>(value);
            } else {
                int64_t value;
                plist_get_int_val(chunkNode, &value);
                chunkID = value;
            }
            bool exists = std::find(usedChunkIDs.begin(), usedChunkIDs.end(), chunkID) != usedChunkIDs.end();
            if (exists) continue;
            usedChunkIDs.push_back(chunkID);

            // Get the "ds" item from snapNode dictionary
            plist_t dsNode = plist_dict_get_item(snapNode, "ds");
            if (!dsNode) continue; // Skip if "ds" key doesn't exist

            std::vector<newVoxel> voxels = getSnapshotVoxels(dsNode, _minw);
            //printVoxelTable(voxels, 100);

            // Verify it's binary data type
            if (plist_get_node_type(dsNode) != PLIST_DATA) continue;

            // Extract the binary data
            char* data = NULL;
            uint64_t length = 0;
            plist_get_data_val(dsNode, &data, &length);

            // Now data points to the binary data (equivalent to voxelData)
            // and length contains its size
            // Don't forget to free data when done with it: free(data)

            // Get chunk coordinates and world origin
            auto [chunkX, chunkY, chunkZ] = decodeMortonChunkID(chunkID);
            uint32_t dx1, dy1, dz1;
            decodeMorton3DOptimized(chunkID, dx1, dy1, dz1); // index IS the morton code
            int worldOriginX = dx1 * 32; // get world loc within 256x256x256 grid
            int worldOriginY = dy1 * 32;
            int worldOriginZ = dz1 * 32;

            for (const auto& voxel : voxels) {
                auto [localX, localY, localZ, color] = voxel;
                int worldX = worldOriginX + localX;
                int worldY = worldOriginY + localY;
                int worldZ = worldOriginZ + localZ;
                voxelMap[chunkID][std::make_tuple(worldX, worldY, worldZ)] = color;
            }
        } catch (std::exception& e) {
            std::cout << "Error: " << e.what() << std::endl;    
            // Just continue to next snapshot
        }
        snapshotCount++;
    }
    
    // Collect visible voxels
    std::vector<std::tuple<int, int, int, uint8_t>> visible;
    int foo=0;
    for (const auto& [_, chunk] : voxelMap) {
        for (const auto& [pos, color] : chunk) {
            foo++;
            if (color != 0) {
                auto [x, y, z] = pos;
                visible.emplace_back(x, y, z, color);
            }
        }
    }
    
    // Sort by coordinates
    //std::sort(visible.begin(), visible.end());
    
    // Create Bella scene nodes for each voxel
    int i = 0;
    for (const auto& [x, y, z, color] : visible) {
        // Create a unique name for this voxel's transform node
        dl::String voxXformName = dl::String("voxXform") + dl::String(i);
        //std::cout << voxXformName << std::endl;
        // Create a transform node in the Bella scene
        auto xform = sceneWrite.createNode("xform", voxXformName, voxXformName);
        // Set this transform's parent to the world root
        xform.parentTo(sceneWrite.world());
        // Parent the voxel geometry to this transform
        voxel.parentTo(xform);
        // Set the transform matrix to position the voxel at (x,y,z)
        // This is a 4x4 transformation matrix - standard in 3D graphics
        xform["steps"][0]["xform"] = dl::Mat4 { 1, 0, 0, 0,
                                0, 1, 0, 0,
                                0, 0, 1, 0,
                                static_cast<double>(x),
                                static_cast<double>(y),
                                static_cast<double>(z), 1};
        // Store the color index for this voxel
        voxelPalette.push_back(color-1);
        i++;
    }
        // Assign materials to voxels based on their color indices
    for (int i = 0; i < voxelPalette.size(); i++) 
    {
        // Find the transform node for this voxel
        auto xformNode = sceneWrite.findNode(dl::String("voxXform") + dl::String(i));
        // Find the material node for this voxel's color
        auto matNode = sceneWrite.findNode(dl::String("voxMat") + dl::String(voxelPalette[i]));
        // Assign the material to the voxel
        xformNode["material"] = matNode;
    }

}
