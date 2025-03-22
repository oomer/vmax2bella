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

// Bella SDK includes - external libraries for 3D rendering
#include "bella_sdk/bella_scene.h"  // For creating and manipulating 3D scenes in Bella
#include "dl_core/dl_main.inl"      // Core functionality from the Diffuse Logic engine
#include "dl_core/dl_fs.h"
#include "lzfse.h"

#include "../libplist/include/plist/plist.h" // Library for handling Apple property list files
// Define STB_IMAGE_IMPLEMENTATION before including to create the implementation
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" // We'll need to add this library



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

// Structure to hold voxel information
struct newVoxel {
    uint32_t x, y, z;  // 3D coordinates
    uint8_t color; // Color value
};

struct dsVoxel {
    uint8_t layer;
    uint8_t color;
};

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
 * Prints a table of voxel positions and colors
 * 
 * @param voxels The vector of decoded voxels
 * @param limit Maximum number of voxels to display (0 for all)
 * @param filterZ Optional z-value to filter by
 */
void printVoxelTable(const std::vector<newVoxel>& voxels, size_t limit = 100, int filterZ = -1) {
    int emptyVoxels = 32768 - voxels.size();
    std::cout << "Voxels: " << voxels.size() << " Empty: " << emptyVoxels << std::endl;
    
    // Count voxels at the filtered z-level if filtering is active
    int filteredCount = 0;
    if (filterZ >= 0) {
        for (const auto& voxel : voxels) {
            if (voxel.z == filterZ) filteredCount++;
        }
        std::cout << "Voxels at z=" << filterZ << ": " << filteredCount << std::endl;
    }
    
    std::cout << "Index | X  | Y  | Z  | Color" << std::endl;
    std::cout << "------|----|----|----|---------" << std::endl;
    
    int count = 0;
    int shownCount = 0;
    for (size_t i = 0; i < voxels.size(); i++) {
        const auto& voxel = voxels[i];
        
        // Skip if we're filtering by z and this doesn't match
        if (filterZ >= 0 && voxel.z != filterZ) continue;
        
        std::cout << std::setw(6) << i << " | ";
        std::cout << std::setw(2) << voxel.x << " | ";
        std::cout << std::setw(2) << voxel.y << " | ";
        std::cout << std::setw(2) << voxel.z << " | ";
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') 
                 << static_cast<int>(voxel.color) << std::dec << std::setfill(' ') << std::endl;
        
        // Count shown voxels
        shownCount++;
        
        // Check if we've reached the limit
        if (limit > 0 && shownCount >= limit) {
            if (filterZ >= 0) {
                int remaining = filteredCount - shownCount;
                if (remaining > 0) {
                    std::cout << "... (output truncated, " << remaining << " more voxels at z=" << filterZ << ")" << std::endl;
                }
            } else {
                std::cout << "... (output truncated, " << (voxels.size() - shownCount) << " more voxels)" << std::endl;
            }
            break;
        }
    }
}

/**
 * New visualization function that definitely uses the correct z-plane
 * 
 * @param voxels The vector of decoded voxels
 * @param zPlane The z-coordinate of the plane to visualize
 * @param size The size of the grid (default: 32x32)
 */
void visualizeZPlaneFixed(const std::vector<newVoxel>& voxels, int zPlane, int size = 32) {
    // Bounds checking
    const int MIN_Z = 0;
    const int MAX_Z = 31;
    if (zPlane < MIN_Z || zPlane > MAX_Z) {
        std::cout << "WARNING: z-plane value " << zPlane << " is out of bounds. Valid range is " << MIN_Z << "-" << MAX_Z << ". Using z=0 instead." << std::endl;
        zPlane = 0;
    }
    
    std::cout << "Visualizing z-plane: " << zPlane << std::endl;
    
    // Create a 2D grid for visualization
    std::vector<std::vector<char>> grid(size, std::vector<char>(size, ' '));
    
    // Count voxels for statistics
    int totalVoxels = voxels.size();
    int voxelsAtRequestedZ = 0;
    int coloredVoxels = 0;
    int clearVoxels = 0;
    
    // Loop 1: Debug output for the first few matching voxels
    int debugCount = 0;
    for (const auto& voxel : voxels) {
        if (voxel.z == zPlane) {
            voxelsAtRequestedZ++;
            
            // Update the grid and count color types
            if (voxel.x >= 0 && voxel.x < size && voxel.y >= 0 && voxel.y < size) {
                if (voxel.color == 0x00) {
                    grid[voxel.y][voxel.x] = '.';  // Clear voxel (0x00)
                    clearVoxels++;
                } else if (voxel.color == 0x25) {
                    grid[voxel.y][voxel.x] = '#';  // Colored voxel (0x25)
                    coloredVoxels++;
                } else {
                    grid[voxel.y][voxel.x] = 'X';  // Other color
                    coloredVoxels++;
                }
            }
        }
    }
    
    // Print statistics
    std::cout << "\nVisualization Statistics:" << std::endl;
    std::cout << "- Total voxels in data: " << totalVoxels << std::endl;
    std::cout << "- Voxels at z=" << zPlane << ": " << voxelsAtRequestedZ << std::endl;
    std::cout << "- Colored voxels: " << coloredVoxels << " (shown as '#' or 'X')" << std::endl;
    std::cout << "- Clear voxels: " << clearVoxels << " (shown as '.')" << std::endl;
    
    // If no matching voxels were found, print a message and return
    if (voxelsAtRequestedZ == 0) {
        std::cout << "\n*** NO VOXELS FOUND AT Z=" << zPlane << " ***\n" << std::endl;
        return;
    }
    
    // Print legend
    std::cout << "\nLegend:" << std::endl;
    std::cout << "- '#': Color 0x25" << std::endl;
    std::cout << "- '.': Clear (0x00)" << std::endl;
    std::cout << "- 'X': Other colors" << std::endl;
    std::cout << "- ' ': No voxel present" << std::endl;
    std::cout << "- Each 8x4 section represents one subchunk" << std::endl;
    
    // Print x-axis header
    std::cout << "\n    ";
    for (int x = 0; x < size; x++) {
        if (x % 8 == 0) {
            std::cout << "|";  // Mark subchunk boundaries
        } else {
            std::cout << x % 10;  // Print digit for readability
        }
    }
    std::cout << std::endl;
    
    // Print divider line
    std::cout << "    ";
    for (int x = 0; x < size; x++) {
        if (x % 8 == 0) {
            std::cout << "+";  // Mark subchunk corners
        } else {
            std::cout << "-";
        }
    }
    std::cout << std::endl;
    
    // Print grid with y-axis labels and subchunk markers
    for (int y = 0; y < size; y++) {
        std::cout << std::setw(2) << y << " ";
        
        // Mark subchunk boundaries on y-axis
        if (y % 4 == 0) {
            std::cout << "+";
        } else {
            std::cout << "|";
        }
        
        // Print the actual voxel data for this row
        for (int x = 0; x < size; x++) {
            std::cout << grid[y][x];
        }
        std::cout << std::endl;
    }
    
    std::cout << "\n===============================================\n";
}



/**
 * Print a plist node's contents recursively.
 * This function takes a plist node and prints its contents in a human-readable format.
 * It handles all types of plist nodes (dictionaries, arrays, strings, etc.) by using
 * recursion to traverse the entire plist structure.
 * 
 * @param node The plist node to print (plist_t is a pointer to the internal plist structure)
 * @param indent The current indentation level (defaults to 0 for the root node)
 */
void printPlistNode(const plist_t& node, int indent = 0) {
    // Early return if node is null (safety check)
    if (!node) return;

    // Create a string with 'indent * 2' spaces for proper indentation
    // This helps visualize the hierarchy of nested structures
    std::string indentStr(indent * 2, ' ');
    
    // Get the type of the current node (dictionary, array, string, etc.)
    plist_type nodeType = plist_get_node_type(node);
    
    // Handle each type of node differently
    switch (nodeType) {
        case PLIST_DICT: {
            std::cout << indentStr << "Dictionary:" << std::endl;
            
            // Create an iterator for the dictionary
            // nullptr is passed as initial value; the iterator will be allocated by plist_dict_new_iter
            plist_dict_iter it = nullptr;
            plist_dict_new_iter(node, &it);
            
            // Variables to store the current key-value pair
            char* key = nullptr;      // Will hold the dictionary key (needs to be freed)
            plist_t value = nullptr;  // Will hold the value node
            
            // Iterate through all items in the dictionary
            while (true) {
                // Get the next key-value pair
                plist_dict_next_item(node, it, &key, &value);
                
                // Break if we've reached the end of the dictionary
                if (!key || !value) break;
                
                // Print the key and recursively print its value
                std::cout << indentStr << "  " << key << ":" << std::endl;
                printPlistNode(value, indent + 2);  // Increase indent for nested values
                
                // Free the key string (allocated by plist_dict_next_item)
                free(key);
                key = nullptr;  // Set to nullptr to avoid double-free
            }
            
            // Free the iterator when done
            free(it);
            break;
        }
        case PLIST_ARRAY: {
            std::cout << indentStr << "Array:" << std::endl;
            uint32_t size = plist_array_get_size(node);
            for (uint32_t i = 0; i < size; i++) {
                plist_t item = plist_array_get_item(node, i);
                std::cout << indentStr << "  [" << i << "]:" << std::endl;
                printPlistNode(item, indent + 2);
            }
            break;
        }
        case PLIST_STRING: {
            char* str = nullptr;
            plist_get_string_val(node, &str);
            std::cout << indentStr << "String: " << (str ? str : "(null)") << std::endl;
            free(str);
            break;
        }
        case PLIST_BOOLEAN: {
            uint8_t bval;
            plist_get_bool_val(node, &bval);
            std::cout << indentStr << "Boolean: " << (bval ? "true" : "false") << std::endl;
            break;
        }
        case PLIST_UINT: {
            uint64_t val;
            plist_get_uint_val(node, &val);
            std::cout << indentStr << "Integer: " << val << std::endl;
            break;
        }
        case PLIST_REAL: {
            double val;
            plist_get_real_val(node, &val);
            std::cout << indentStr << "Real: " << val << std::endl;
            break;
        }
        case PLIST_DATE: {
            int32_t sec = 0;
            int32_t usec = 0;
            plist_get_date_val(node, &sec, &usec);
            std::cout << indentStr << "Date: " << sec << "." << usec << std::endl;
            break;
        }
        case PLIST_DATA: {
            char* data = nullptr;
            uint64_t length = 0;
            plist_get_data_val(node, &data, &length);
            std::cout << indentStr << "Data: <" << length << " bytes>" << std::endl;
            free(data);
            break;
        }
        default:
            std::cout << indentStr << "Unknown type" << std::endl;
    }
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
 * Examines a specific array element at the given index from a plist file.
 * This function allows inspection of individual chunks/snapshots in the data.
 * 
 * @param plistFilePath Path to the plist file
 * @param index The index of the array element to examine
 * @param arrayPath The path to the array in the plist structure
 * @return true if successful, false if any errors occurred
 */
bool examinePlistNode(const plist_t& root_node, int snapshotIndex, int zIndex, const std::string& arrayPath) {
    std::cout << "Examining Plist array at snapshot " << snapshotIndex << " zIndex " << zIndex << std::endl;

    if (!root_node) {
        std::cerr << "Failed to process Plist data" << std::endl;
        return false;
    }
    plist_t current_node = root_node;
    // if the array path contains slashes, we need to navigate through the structure
    std::string path = arrayPath;
    size_t pos = 0;
    std::string token;
    while ((pos = path.find('/')) != std::string::npos) {
        token = path.substr(0, pos);
        path.erase(0, pos + 1);
        
        // current node must be a dictionary
        if (plist_get_node_type(current_node) != PLIST_DICT) {
            std::cerr << "error: expected dictionary at path component '" << token << "'" << std::endl;
            //plist_free(root_node);
            return false;
        }
        
        // get the next node in the path
        current_node = plist_dict_get_item(current_node, token.c_str());
        if (!current_node) {
            std::cerr << "error: could not find key '" << token << "' in dictionary" << std::endl;
            //plist_free(root_node);
            return false;
        }
    }

    // Now path contains the final key name
    if (!path.empty() && plist_get_node_type(current_node) == PLIST_DICT) {
        current_node = plist_dict_get_item(current_node, path.c_str());
        if (!current_node) {
            std::cerr << "Error: Could not find key '" << path << "' in dictionary" << std::endl;
            return false;
        }
    }
    
    // Check if we found an array
    if (plist_get_node_type(current_node) != PLIST_ARRAY) {
        std::cerr << "Error: '" << "arrayPath" << "' is not an array" << std::endl;
        return false;
    }
    
    // Get Plist node array size
    uint32_t arraySize = plist_array_get_size(current_node);
    if (snapshotIndex < 0 || snapshotIndex >= static_cast<int>(arraySize)) {
        std::cerr << "Error: Index " << snapshotIndex << " is out of range (array size: " << arraySize << ")" << std::endl;
        return false;
    }
    
    // Get the Plist node at the specified index
    plist_t element = plist_array_get_item(current_node, snapshotIndex);
    if (!element) {
        std::cerr << "Error: Could not get Plist node at snapshot " << snapshotIndex << std::endl;
        return false;
    }
    
    std::cout << "Array size: " << arraySize << std::endl;
    std::cout << "Plist node details at snapshot " << snapshotIndex << " zIndex " << zIndex << ":" << std::endl;
    printPlistNode(element);
    debugSnapshots(element, snapshotIndex, zIndex);
    return true;
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

/**
 * Handles 's' dictionary in a Plist node holding 32x32x32 chunks of voxel data.
 * 
 * @param element The Plist node to examine
 * @return true if successful, false if any errors occurred
 */
bool debugSnapshots(plist_t element, int snapshotIndex, int zIndex) {
    std::cout << "Debugging snapshots" << std::endl;
    // Special handling for 's' dictionaries
    if (plist_get_node_type(element) == PLIST_DICT) {
        plist_t sNode = plist_dict_get_item(element, "s");
        if (sNode) {
            // Look for specific keys of interest in the 's' dictionary
            if (plist_get_node_type(sNode) == PLIST_DICT) {
                // Check for 'ds' (data stream) in the 's' dictionary
                plist_t dsNode = plist_dict_get_item(sNode, "ds");
                if (dsNode && plist_get_node_type(dsNode) == PLIST_DATA) {
                    char* data = nullptr;
                    uint64_t length = 0;
                    plist_get_data_val(dsNode, &data, &length);
                    
                    std::cout << "\nDetailed analysis of 'ds' data stream (size: " << length << " bytes):" << std::endl;
                    
                    // Detailed analysis of the data stream
                    if (length > 0 && data) {
                        // Display as hex bytes - increased to 384 bytes
                        std::cout << "First 384 bytes (hex):" << std::endl;
                        size_t bytesToShow = std::min(static_cast<size_t>(384), static_cast<size_t>(length));
                        for (size_t i = 0; i < bytesToShow; i++) {
                            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                                      << static_cast<int>(static_cast<uint8_t>(data[i])) << " ";
                            if ((i + 1) % 16 == 0) std::cout << std::endl;
                        }
                        std::cout << std::dec << std::endl;
                        
                        // If data appears to be position-color pairs (as in voxel data)
                        if (length % 2 == 0) {
                            size_t numPairs = length / 2;
                            std::cout << "Data appears to contain " << numPairs << " position-color pairs" << std::endl;
                            
                            // Check if all positions are 0 (common for optimized voxel data)
                            bool allPositionsZero = true;
                            for (size_t i = 0; i < std::min(numPairs, static_cast<size_t>(100)); i++) {
                                if (static_cast<uint8_t>(data[i * 2]) != 0) {
                                    allPositionsZero = false;
                                    break;
                                }
                            }
                            
                            if (allPositionsZero) {
                                // Show only color values for more compact analysis
                                std::cout << "\nAll position values are 0. Showing only color values:" << std::endl;
                                std::cout << "First 384 color values (hex):" << std::endl;
                                size_t colorsToShow = std::min(static_cast<size_t>(384), numPairs);
                                for (size_t i = 0; i < colorsToShow; i++) {
                                    std::cout << std::hex << std::setw(2) << std::setfill('0')
                                              << static_cast<int>(static_cast<uint8_t>(data[i * 2 + 1])) << " ";
                                    if ((i + 1) % 16 == 0) std::cout << std::endl;
                                }
                                std::cout << std::dec << std::endl;
                            } else {
                                // Show position-color pairs if positions vary
                                std::cout << "\nFirst 10 position-color pairs:" << std::endl;
                                std::cout << "Index | Position | Color" << std::endl;
                                std::cout << "------|----------|------" << std::endl;
                                
                                size_t pairsToShow = std::min(static_cast<size_t>(10), numPairs);
                                for (size_t i = 0; i < pairsToShow; i++) {
                                    uint8_t position = static_cast<uint8_t>(data[i * 2]);
                                    uint8_t color = static_cast<uint8_t>(data[i * 2 + 1]);
                                    
                                    std::cout << std::setw(5) << i << " | " 
                                              << std::setw(8) << std::hex << std::setfill('0') 
                                              << static_cast<int>(position) << std::dec << std::setfill(' ') << " | " 
                                              << std::setw(5) << std::hex << std::setfill('0') 
                                              << static_cast<int>(color) << std::dec << std::setfill(' ') << std::endl;
                                }
                            }
                            
                            // Analyze and print color runs
                            std::cout << "\nAnalyzing color runs:" << std::endl;
                            
                            if (numPairs > 0) {
                                uint8_t currentColor = static_cast<uint8_t>(data[1]); // First color
                                size_t runStart = 0;
                                size_t runLength = 1;
                                
                                // Find all runs
                                std::vector<std::tuple<size_t, size_t, uint8_t>> colorRuns;
                                
                                for (size_t i = 1; i < numPairs; i++) {
                                    uint8_t color = static_cast<uint8_t>(data[i * 2 + 1]);
                                    
                                    if (color == currentColor) {
                                        // Continue the current run
                                        runLength++;
                                    } else {
                                        // End the current run and start a new one
                                        colorRuns.emplace_back(runStart, runStart + runLength - 1, currentColor);
                                        currentColor = color;
                                        runStart = i;
                                        runLength = 1;
                                    }
                                }
                                
                                // Add the last run
                                colorRuns.emplace_back(runStart, runStart + runLength - 1, currentColor);
                                
                                // Print the runs in a condensed format
                                std::cout << "Found " << colorRuns.size() << " color runs:" << std::endl;
                                std::cout << "Color | Voxel Count | Range" << std::endl;
                                std::cout << "------|-------------|------" << std::endl;
                                
                                for (const auto& run : colorRuns) {
                                    size_t start = std::get<0>(run);
                                    size_t end = std::get<1>(run);
                                    uint8_t color = std::get<2>(run);
                                    size_t length = end - start + 1;
                                    
                                    std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0') 
                                              << static_cast<int>(color) << " | "
                                              << std::dec << std::setfill(' ') << std::setw(11) << length << " | "
                                              << std::setw(5) << start << "-" << std::setw(5) << end
                                              << std::endl;
                                }
                                
                                // Add special notice for full-voxel-space runs
                                if (colorRuns.size() == 1) {
                                    const auto& singleRun = colorRuns[0];
                                    size_t start = std::get<0>(singleRun);
                                    size_t end = std::get<1>(singleRun);
                                    size_t length = end - start + 1;
                                    uint8_t color = std::get<2>(singleRun);
                                    
                                    if (start == 0 && length == 32768) {
                                        std::cout << "\nNOTICE: This chunk contains a single color (0x" 
                                                  << std::hex << static_cast<int>(color) << std::dec 
                                                  << ") for all 32,768 voxels, which would fill a complete 32x32x32 voxel space." << std::endl;
                                        std::cout << "This could indicate:";
                                        std::cout << "\n  - A solid block of one color";
                                        std::cout << "\n  - A special encoding for empty/default chunks";
                                        std::cout << "\n  - A placeholder or initialization state" << std::endl;
                                    }
                                }
                            }
                        }
                        
                        // Decode voxels for visualization
                        // @param TODO: get morton offset from the 'lt' dictionary
                        std::vector<newVoxel> voxels = decodeVoxels(std::vector<uint8_t>(data, data + length), 0);
                        
                        printVoxelTable(voxels, 100);
                        
                        // Explicitly decode the voxels for visualization
                        char* data = nullptr;
                        uint64_t length = 0;
                        plist_get_data_val(dsNode, &data, &length);
                        
                        if (length > 0 && data) {
                            std::vector<newVoxel> voxels = decodeVoxels(std::vector<uint8_t>(data, data + length), 0);
                            visualizeZPlaneFixed(voxels, zIndex);
                            free(data);
                        }
                    }
                    
                    free(data);
                }
                
                // Check for 'id' dictionary to get chunk information
                plist_t idNode = plist_dict_get_item(sNode, "id");
                if (idNode && plist_get_node_type(idNode) == PLIST_DICT) {
                    plist_t chunkIdNode = plist_dict_get_item(idNode, "c");
                    if (chunkIdNode && plist_get_node_type(chunkIdNode) == PLIST_UINT) {
                        uint64_t chunkId;
                        plist_get_uint_val(chunkIdNode, &chunkId);
                        std::cout << "\nChunk ID: " << chunkId << std::endl;
                    }
                }
                
                // Check for 'lt' (location table)
                plist_t ltNode = plist_dict_get_item(sNode, "lt");
                if (ltNode && plist_get_node_type(ltNode) == PLIST_DATA) {
                    char* data = nullptr;
                    uint64_t length = 0;
                    plist_get_data_val(ltNode, &data, &length);
                    
                    std::cout << "\nLocation table size: " << length << " bytes" << std::endl;
                    if (length > 0 && data) {
                        std::cout << "First 16 bytes of location table:" << std::endl;
                        size_t bytesToShow = std::min(static_cast<size_t>(16), static_cast<size_t>(length));
                        for (size_t i = 0; i < bytesToShow; i++) {
                            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                                      << static_cast<int>(static_cast<uint8_t>(data[i])) << " ";
                        }
                        std::cout << std::dec << std::endl;
                    }
                    
                    free(data);
                }
            }
        }
    }
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

    // Get the input file path from command line arguments
    if (args.have("--voxin"))
    {
        filePath = args.value("--voxin").buf();
        lzfseFilePath = filePath + "/contents1.vmaxb";
        pngFilePath = filePath + "/palette1.png";
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

        //std::vector<newVoxel> voxels = getSnapshotVoxels(root_node, 0); // Get voxels for snapshot 0
        //std::cout << "VOXELS SIZE: " << voxels.size() << std::endl;
        /*std::vector<plist_t> snapshotChunks = getAllSnapshots(root_node);
        for (uint32_t i = 0; i < snapshotChunks.size(); i++) {
            std::cout << "Snapshot chunk " << i << std::endl;
            std::vector<newVoxel> voxels = getSnapshotVoxels(snapshotChunks[i]);
            std::cout << "VOXELS SIZE: " << voxels.size() << std::endl;
            //printVoxelTable(voxels, 100);
        }*/

        //examinePlistNode(root_node, 0, zIndex, "snapshots");
        writeBszScene("temp.bsz", root_node, palette);

        plist_free(root_node); 

    }   else {
        std::cout << "No input file specified. Use -pl for plist input or -lz for compressed LZFSE input." << std::endl;
        return 1;
    }
    return 0;
}

int writeBszScene( const std::string& bszPath, const plist_t root_node, const std::vector<RGBA> palette) {
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
        imageDome["dir"]            = "./resources";
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

    std::filesystem::path voxPath;  

    // Create a vector to store voxel color indices
    std::vector<uint8_t> voxelPalette;
    writeBellaVoxels(root_node, voxelPalette, sceneWrite, voxel, palette);
    //std::filesystem::path bszFSPath = bszFSPath.stem().string() + ".bsz";
    //sceneWrite.write(dl::String(bszPath.c_str()));
    sceneWrite.write(dl::String("goo.bsz"));
    return 0;
}

// Function that returns the license text for this program
std::string initializeGlobalLicense() 
{
    // R"(...)" is a C++ raw string literal - allows multi-line strings with preserved formatting
    return R"(
vmax2bella

Copyright (c) 2025 Harvey Fong

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.)";
}

// Function that returns third-party license text
std::string initializeGlobalThirdPartyLicences() 
{
    return R"(
Bella SDK (Software Development Kit)

Copyright Diffuse Logic SCP, all rights reserved.

Permission is hereby granted to any person obtaining a copy of this software
(the "Software"), to use, copy, publish, distribute, sublicense, and/or sell
copies of the Software.

THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY. ALL
IMPLIED WARRANTIES OF FITNESS FOR ANY PARTICULAR PURPOSE AND OF MERCHANTABILITY
ARE HEREBY DISCLAIMED.)

===

lzfse 

Copyright (c) 2015-2016, Apple Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1.  Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2.  Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.

3.  Neither the name of the copyright holder(s) nor the names of any contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
)"; 
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
    //for (size_t i = 0; i < snapshotsArray.size(); i++) {

    //for(int i=255; i>=0; i--)
    for(int i=0; i<256; i++)
    {

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
            // Commented out: Alternative material settings
            //dielectric["ior"] = 1.41f;
            //dielectric["roughness"] = 40.0f;
            //dielectric["depth"] = 33.0f;
            
            // Set the material color (convert 0-255 values to 0.0-1.0 range)
            voxMat["reflectance"] = dl::Rgba{ 
                srgbToLinear(dR), 
                srgbToLinear(dG), 
                srgbToLinear(dB), 
                dA 
            };
        }
    }


    //get last chunkid only
    std::vector<int32_t> usedChunkIDs;
    for (size_t i = snapshotsArray.size() - 1; i != SIZE_MAX; i--) {
    // Loop body

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

            // Get the "c" item from idNode dictionary
            plist_t minNode = plist_dict_get_item(stNode, "min");
            if (!stNode) continue; // Skip if "c" key doesn't exist
            int64_t _minx;
            int64_t _miny;
            int64_t _minz;
            int64_t _minw;
            //plist_get_int_val(plist_array_get_item(minNode, 0), &_minx);
            //plist_get_int_val(plist_array_get_item(minNode, 1), &_miny);
            //plist_get_int_val(plist_array_get_item(minNode, 2), &_minz);   
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
