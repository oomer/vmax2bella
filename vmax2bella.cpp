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
- This allows addressing up to 256 chunks in each dimension , although only 8x8x8 are used in practice
- The decodeMortonChunkID function extracts x, y, z coordinates from a Morton-encoded chunk ID
- The resulting chunk coordinates are then multiplied by 32 to get the world position of the chunk

### Chunk Addressing
- Each chunk has 3D coordinates (chunk_x, chunk_y, chunk_z)
- For a 128×128×128 volume with 32×32×32 chunks, there would be 4×4×4 chunks total (64 chunks)
- Chunks are only stored if they contain at least one non-empty voxel
- Each snapshot contains data for a specific chunk, identified by the 'c' value in the 's.id' dictionary

## Data Fields
### Voxel Data Stream (ds)
- Variable-length binary data
- Contains pairs of bytes for each voxel: [position_byte, color_byte]
- Each chunk can contain up to 32,768 voxels (32×32×32)
- *Position Byte:*
    - The format uses a encoding approach that combines sequential and Morton encoding for optimal storage efficiency:
        - Uses a mix of position=0 bytes (for sequential implicit positions) and Morton-encoded position bytes
        - The decoder maintains an internal position counter that advances through the chunk in a predefined order (x, then y, then z)
        - Color byte 0 indicates "no voxel at this position" (empty space)
        - If a chunk uses the entire 256x256x256 addressable space, then it uses exactly 65,536 bytes (32,768 voxel pairs)
        - This is the dense case and it not memory efficient
        - When we introduce morton encoding we can jump to a specific position in the chunk
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

### Layer Color Usagw (lc)
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


### Detailed Morton Encoding Example
To clarify the bit interleaving process, here's a step-by-step example:

For position (3,1,2):

1. Convert to binary (3 bits each):
   - x = 3 = 011 (bits labeled as x₂x₁x₀)
   - y = 1 = 001 (bits labeled as y₂y₁y₀)
   - z = 2 = 010 (bits labeled as z₂z₁z₀)

2. Interleave the bits in the order z₂y₂x₂, z₁y₁x₁, z₀y₀x₀:
   - z₂y₂x₂ = 001 (z₂=0, y₂=0, x₂=1)
   - z₁y₁x₁ = 010 (z₁=1, y₁=0, x₁=0)
   - z₀y₀x₀ = 100 (z₀=0, y₀=0, x₀=0)

3. Combine: 001010100 = binary 10000110 = decimal 134

Therefore, position (3,1,2) has Morton index 134.

```
Morton    Binary       Coords      Visual (Z-layers)
Index   (zyx bits)     (x,y,z)   

  0     000 (000)      (0,0,0)    Z=0 layer:    Z=1 layer:
  1     001 (001)      (1,0,0)    +---+---+    +---+---+
  2     010 (010)      (0,1,0)    | 0 | 1 |    | 4 | 5 |
  3     011 (011)      (1,1,0)    +---+---+    +---+---+
  4     100 (100)      (0,0,1)    | 2 | 3 |    | 6 | 7 |
  5     101 (101)      (1,0,1)    +---+---+    +---+---+
  6     110 (110)      (0,1,1)
  7     111 (111)      (1,1,1)
```

### Deinterleave the bits:
Extract bits 0, 3, 6 for the x coordinate
Extract bits 1, 4, 7 for the y coordinate
Extract bits 2, 5 for the z coordinate (note: z only needs 2 bits for 0-3 range)
For example, to convert chunk ID 73 to 3D chunk coordinates:
73 in binary: 01001001

### Deinterleaving:
```
x = bits 0,3,6 = 101 = 5
y = bits 1,4,7 = 001 = 1
z = bits 2,5 = 00 = 0
```
So chunk ID 73 corresponds to chunk position (5,1,0).
The Morton encoding ensures that spatially close chunks are also close in memory.

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

// PlistCPP includes
#include "Plist.hpp"
#include "PlistDate.hpp"

// Namespaces allow you to use symbols from a library without prefixing them
// For example, with these 'using' statements, you can write 'Scene' instead of 'bella_sdk::Scene'
namespace bsdk = dl::bella_sdk;

// Global variable to track if we've found a color palette in the file
bool has_palette = false;

// Forward declarations of functions - tells the compiler that these functions exist 
// and will be defined later in the file
std::string initializeGlobalLicense();
std::string initializeGlobalThirdPartyLicences();

// Observer class for monitoring scene events
// This is a custom implementation of the SceneObserver interface from Bella SDK
// The SceneObserver is called when various events occur in the scene
struct Observer : public bsdk::SceneObserver
{   
    bool inEventGroup = false; // Flag to track if we're in an event group
        
    // Override methods from SceneObserver to provide custom behavior
    
    // Called when a node is added to the scene
    void onNodeAdded( bsdk::Node node ) override
    {   
        dl::logInfo("%sNode added: %s", inEventGroup ? "  " : "", node.name().buf());
    }
    
    // Called when a node is removed from the scene
    void onNodeRemoved( bsdk::Node node ) override
    {
        dl::logInfo("%sNode removed: %s", inEventGroup ? "  " : "", node.name().buf());
    }
    
    // Called when an input value changes
    void onInputChanged( bsdk::Input input ) override
    {
        dl::logInfo("%sInput changed: %s", inEventGroup ? "  " : "", input.path().buf());
    }
    
    // Called when an input is connected to something
    void onInputConnected( bsdk::Input input ) override
    {
        dl::logInfo("%sInput connected: %s", inEventGroup ? "  " : "", input.path().buf());
    }
    
    // Called at the start of a group of related events
    void onBeginEventGroup() override
    {
        inEventGroup = true;
        dl::logInfo("Event group begin.");
    }
    
    // Called at the end of a group of related events
    void onEndEventGroup() override
    {
        inEventGroup = false;
        dl::logInfo("Event group end.");
    }
};

// Function to decompress LZFSE file to a plist file
bool decompressLzfseToPlist(const std::string& inputFile, const std::string& outputFile) {
    // Open input file
    std::ifstream inFile(inputFile, std::ios::binary);
    if (!inFile.is_open()) {
        std::cerr << "Error: Could not open input file: " << inputFile << std::endl;
        return false;
    }

    // Get file size
    inFile.seekg(0, std::ios::end);
    size_t inSize = inFile.tellg();
    inFile.seekg(0, std::ios::beg);

    // Read compressed data
    std::vector<uint8_t> inBuffer(inSize);
    inFile.read(reinterpret_cast<char*>(inBuffer.data()), inSize);
    inFile.close();

    // Allocate output buffer (assuming decompressed size is larger)
    // Start with 4x the input size, will resize if needed
    size_t outAllocatedSize = inSize * 4;
    std::vector<uint8_t> outBuffer(outAllocatedSize);

    // Allocate scratch buffer for lzfse
    size_t scratchSize = lzfse_decode_scratch_size();
    std::vector<uint8_t> scratch(scratchSize);

    // Decompress data
    size_t decodedSize = 0;
    while (true) {
        decodedSize = lzfse_decode_buffer(
            outBuffer.data(), outAllocatedSize,
            inBuffer.data(), inSize,
            scratch.data());

        // If output buffer was too small, increase size and retry
        if (decodedSize == 0 || decodedSize == outAllocatedSize) {
            outAllocatedSize *= 2;
            outBuffer.resize(outAllocatedSize);
            continue;
        }
        break;
    }

    // Write output file
    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "Error: Could not open output file: " << outputFile << std::endl;
        return false;
    }

    outFile.write(reinterpret_cast<char*>(outBuffer.data()), decodedSize);
    outFile.close();

    std::cout << "Successfully decompressed " << inputFile << " to " << outputFile << std::endl;
    std::cout << "Input size: " << inSize << " bytes, Output size: " << decodedSize << " bytes" << std::endl;
    
    return true;
}

// Forward declarations
bool outputBellaFile(const std::string& outputPath, size_t voxelCount);
bool parsePlistVoxelData(const std::string& filePath, bool verbose);
std::tuple<int, int, int> decodeMortonChunkID(int64_t mortonID);
void printSimpleVoxelCoordinates(const std::string& filePath);
void justPrintVoxelCoordinates(const std::string& filePath);

// Function to decode a Morton code to x, y, z coordinates
void decodeMorton(uint8_t code, uint8_t& x, uint8_t& y, uint8_t& z) {
    // Initialize the coordinates
    x = y = z = 0;
    
    // For a 3D Morton code in 8 bits, we have:
    // Bits: 7  6  5  4  3  2  1  0
    //       z2 y2 x2 z1 y1 x1 z0 y0 x0
    
    // Extract bits for x (positions 0, 3, 6)
    if (code & 0x01) x |= 1;      // Bit 0
    if (code & 0x08) x |= 2;      // Bit 3
    if (code & 0x40) x |= 4;      // Bit 6
    
    // Extract bits for y (positions 1, 4, 7)
    if (code & 0x02) y |= 1;      // Bit 1
    if (code & 0x10) y |= 2;      // Bit 4
    if (code & 0x80) y |= 4;      // Bit 7
    
    // Extract bits for z (positions 2, 5)
    if (code & 0x04) z |= 1;      // Bit 2
    if (code & 0x20) z |= 2;      // Bit 5
    
    // Note: For a fully 3-bit coordinate system (0-7), we would expect a 9-bit Morton code
    // But since we're using uint8_t (8 bits), we can only represent coordinates up to:
    // x: 0-7 (3 bits)
    // y: 0-7 (3 bits)
    // z: 0-3 (2 bits)
    // This gives a total of 8 bits (3+3+2)
}

// Function to decode a Morton code to x, y, z coordinates for the chunk index
void decodeChunkMorton(uint8_t code, uint8_t& chunkX, uint8_t& chunkY, uint8_t& chunkZ) {
    // Same decoding as for voxel coordinates
    decodeMorton(code, chunkX, chunkY, chunkZ);
}

// Function to analyze and print detailed information for a specific chunk
void analyzeChunk(const Plist::data_type& locationTable, const Plist::data_type& voxelData, uint8_t chunkIndex) {
    // First, check if the chunk is occupied
    if (chunkIndex >= locationTable.size() || locationTable[chunkIndex] == 0) {
        std::cout << "Chunk " << (int)chunkIndex << " is not occupied." << std::endl;
        return;
    }
    
    // Decode chunk coordinates
    uint8_t chunkX, chunkY, chunkZ;
    decodeChunkMorton(chunkIndex, chunkX, chunkY, chunkZ);
    
    std::cout << "Analyzing chunk " << (int)chunkIndex << " at position (" 
              << (int)chunkX << "," << (int)chunkY << "," << (int)chunkZ << ")" << std::endl;
    
    // Count voxels in this chunk and their position/color data
    std::map<uint8_t, int> colorCounts; // Map to count occurrences of each color
    std::map<uint8_t, int> positionCounts; // Map to count occurrences of each position
    
    // Assuming each voxel is 2 bytes: position, color
    for (size_t i = 0; i < voxelData.size(); i += 2) {
        if (i + 1 < voxelData.size()) {
            uint8_t position = static_cast<uint8_t>(voxelData[i]);
            uint8_t color = static_cast<uint8_t>(voxelData[i + 1]);
            
            // Count position and color occurrences
            positionCounts[position]++;
            colorCounts[color]++;
            
            // Extract x, y, z from position if it's not 0
            if (position != 0) {
                uint8_t localX, localY, localZ;
                decodeMorton(position, localX, localY, localZ);
                
                // Calculate world coordinates
                uint16_t worldX = chunkX * 32 + localX;
                uint16_t worldY = chunkY * 32 + localY;
                uint16_t worldZ = chunkZ * 32 + localZ;
                
                std::cout << "  Voxel at local (" << (int)localX << "," << (int)localY << "," << (int)localZ 
                          << "), world (" << worldX << "," << worldY << "," << worldZ 
                          << ") with color " << (int)color << std::endl;
            } else {
                // If position is 0, it's at the origin of the chunk
                uint16_t worldX = chunkX * 32;
                uint16_t worldY = chunkY * 32;
                uint16_t worldZ = chunkZ * 32;
                
                // Only print a few of these to avoid flooding output
                if (i < 10) {
                    std::cout << "  Voxel at local (0,0,0), world (" << worldX << "," << worldY << "," << worldZ 
                              << ") with color " << (int)color << std::endl;
                }
            }
        }
    }
    
    // Print color statistics
    std::cout << "Color distribution in chunk " << (int)chunkIndex << ":" << std::endl;
    for (const auto& pair : colorCounts) {
        std::cout << "  Color " << (int)pair.first << ": " << pair.second << " voxels" << std::endl;
    }
    
    // Print position statistics
    std::cout << "Position code distribution in chunk " << (int)chunkIndex << ":" << std::endl;
    for (const auto& pair : positionCounts) {
        if (pair.first == 0) {
            std::cout << "  Position 0 (chunk origin): " << pair.second << " voxels" << std::endl;
        } else {
            uint8_t localX, localY, localZ;
            decodeMorton(pair.first, localX, localY, localZ);
            std::cout << "  Position " << (int)pair.first << " (" << (int)localX << "," 
                      << (int)localY << "," << (int)localZ << "): " << pair.second << " voxels" << std::endl;
        }
    }
}

// Structure to represent a voxel
struct Voxel {
    uint8_t position;  // Local position within chunk (Morton encoded)
    uint8_t color;     // Color value

    Voxel(uint8_t pos, uint8_t col) : position(pos), color(col) {}
};

// Structure to represent a chunk in the final model
struct Chunk {
    std::vector<Voxel> voxels;
    uint8_t chunkX, chunkY, chunkZ;  // Chunk coordinates
    
    // Default constructor required for map operations
    Chunk() : chunkX(0), chunkY(0), chunkZ(0) {}
    
    // Constructor to initialize chunk coordinates
    Chunk(uint8_t x, uint8_t y, uint8_t z) : chunkX(x), chunkY(y), chunkZ(z) {}
    
    // This method provides a more accurate interpretation of the voxel data based on our findings
    // Position byte 0 means the voxel is at origin (0,0,0) of the chunk
    // The pattern is encoded through the sequence of color transitions, not through position expansion
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

// Function to analyze the Morton codes in a snapshot's data stream
void analyzeDataStreamMortonCodes(const Plist::data_type& voxelData, size_t snapshotIndex) {
    std::cout << "Morton codes in Snapshot " << snapshotIndex << " data stream:" << std::endl;
    
    // Count frequencies of each position value
    std::map<uint8_t, int> positionCounts;
    std::map<uint8_t, int> colorCounts;
    
    // Extract all position and color bytes
    std::cout << "  First 30 [position, color] pairs (in hex): " << std::endl;
    for (size_t i = 0; i < std::min(voxelData.size(), size_t(60)); i += 2) {
        if (i + 1 < voxelData.size()) {
            uint8_t position = static_cast<uint8_t>(voxelData[i]);
            uint8_t color = static_cast<uint8_t>(voxelData[i + 1]);
            positionCounts[position]++;
            colorCounts[color]++;
            
            std::cout << "    [" << std::hex << std::setw(2) << std::setfill('0') << (int)position 
                     << ", " << std::setw(2) << std::setfill('0') << (int)color << "]";
            
            if ((i/2) % 4 == 3) std::cout << std::endl;
            else std::cout << " ";
        }
    }
    std::cout << std::dec << std::endl;
    
    // Check if all position counts are the same value
    bool allSamePosition = true;
    uint8_t firstPosition = 0;
    
    if (!positionCounts.empty()) {
        firstPosition = positionCounts.begin()->first;
        for (const auto& [position, count] : positionCounts) {
            if (position != firstPosition) {
                allSamePosition = false;
                break;
            }
        }
    }
    
    // Print frequencies of each position value
    if (allSamePosition) {
        std::cout << "  ALL position bytes are " << std::hex << std::setw(2) << std::setfill('0') 
                 << (int)firstPosition << std::dec << " (" << voxelData.size() / 2 << " occurrences)" << std::endl;
    } else {
        std::cout << "  Position value frequencies:" << std::endl;
        for (const auto& [position, count] : positionCounts) {
            std::cout << "    Position " << std::hex << std::setw(2) << std::setfill('0') 
                     << (int)position << std::dec << ": " << count << " occurrences" << std::endl;
        }
    }
    
    // Print frequencies of each color value
    std::cout << "  Color value frequencies:" << std::endl;
    for (const auto& [color, count] : colorCounts) {
        std::cout << "    Color " << std::hex << std::setw(2) << std::setfill('0') 
                 << (int)color << std::dec << ": " << count << " occurrences" << std::endl;
    }
    
    std::cout << "  Total voxels in snapshot: " << voxelData.size() / 2 << std::endl;
    
    // Check if this might be a fill pattern by analyzing color transitions
    if (voxelData.size() >= 4) {
        std::cout << "  Analyzing possible fill pattern:" << std::endl;
        
        // Count how many consecutive voxels have the same color
        int runLength = 1;
        uint8_t prevColor = static_cast<uint8_t>(voxelData[1]);
        std::vector<int> runLengths;
        
        for (size_t i = 3; i < voxelData.size(); i += 2) {
            uint8_t color = static_cast<uint8_t>(voxelData[i]);
            if (color == prevColor) {
                runLength++;
            } else {
                runLengths.push_back(runLength);
                std::cout << "    Run of " << runLength << " voxels with color " 
                         << std::hex << std::setw(2) << std::setfill('0') << (int)prevColor << std::dec << std::endl;
                prevColor = color;
                runLength = 1;
            }
        }
        
        // Add the last run
        runLengths.push_back(runLength);
        std::cout << "    Run of " << runLength << " voxels with color " 
                 << std::hex << std::setw(2) << std::setfill('0') << (int)prevColor << std::dec << std::endl;
        
        // Calculate average run length
        if (!runLengths.empty()) {
            double avgRunLength = 0;
            for (int len : runLengths) {
                avgRunLength += len;
            }
            avgRunLength /= runLengths.size();
            
            std::cout << "    Average run length: " << avgRunLength << " voxels" << std::endl;
            std::cout << "    Number of color transitions: " << runLengths.size() - 1 << std::endl;
        }
    }
}

// Debug function to print the plist structure recursively
void printPlistStructure(const boost::any& node, int depth = 0, const std::string& key = "") {
    std::string indent(depth * 2, ' ');
    
    if (!key.empty()) {
        std::cout << indent << "Key: '" << key << "' -> ";
    }
    
    try {
        // Try as dictionary
        Plist::dictionary_type dict = boost::any_cast<Plist::dictionary_type>(node);
        std::cout << "Dictionary with " << dict.size() << " keys" << std::endl;
        
        for (const auto& [dictKey, value] : dict) {
            std::cout << indent << "  '" << dictKey << "': ";
            printPlistStructure(value, depth + 1);
        }
    } catch (const boost::bad_any_cast&) {
        try {
            // Try as array
            Plist::array_type array = boost::any_cast<Plist::array_type>(node);
            std::cout << "Array with " << array.size() << " elements" << std::endl;
            
            for (size_t i = 0; i < std::min(array.size(), size_t(5)); i++) {
                std::cout << indent << "  [" << i << "]: ";
                printPlistStructure(array[i], depth + 1);
            }
            
            if (array.size() > 5) {
                std::cout << indent << "  ... and " << (array.size() - 5) << " more elements" << std::endl;
            }
        } catch (const boost::bad_any_cast&) {
            try {
                // Try as string
                std::string str = boost::any_cast<std::string>(node);
                std::cout << "String: '" << str << "'" << std::endl;
            } catch (const boost::bad_any_cast&) {
                try {
                    // Try as int
                    int num = boost::any_cast<int>(node);
                    std::cout << "Int: " << num << std::endl;
                } catch (const boost::bad_any_cast&) {
                    try {
                        // Try as bool
                        bool b = boost::any_cast<bool>(node);
                        std::cout << "Bool: " << (b ? "true" : "false") << std::endl;
                    } catch (const boost::bad_any_cast&) {
                        try {
                            // Try as data
                            Plist::data_type data = boost::any_cast<Plist::data_type>(node);
                            std::cout << "Data with " << data.size() << " bytes";
                            
                            // Print first few bytes in hex
                            if (!data.empty()) {
                                std::cout << " [";
                                size_t bytesToShow = std::min(data.size(), size_t(8));
                                for (size_t i = 0; i < bytesToShow; i++) {
                                    if (i > 0) std::cout << " ";
                                    std::cout << std::hex << std::setw(2) << std::setfill('0') 
                                              << static_cast<int>(data[i]) << std::dec;
                                }
                                if (data.size() > 8) {
                                    std::cout << " ...";
                                }
                                std::cout << "]";
                            }
                std::cout << std::endl;
                        } catch (const boost::bad_any_cast&) {
                            try {
                                // Try as double
                                double d = boost::any_cast<double>(node);
                                std::cout << "Double: " << d << std::endl;
                            } catch (const boost::bad_any_cast&) {
                                try {
                                    // Try as date
                                    Plist::date_type date = boost::any_cast<Plist::date_type>(node);
                                    std::cout << "Date object" << std::endl;
                                } catch (const boost::bad_any_cast&) {
                                    try {
                                        // Try as long int
                                        long num = boost::any_cast<long>(node);
                                        std::cout << "Long Int: " << num << std::endl;
                                    } catch (const boost::bad_any_cast&) {
                                        try {
                                            // Try as uint8_t
                                            uint8_t num = boost::any_cast<uint8_t>(node);
                                            std::cout << "UInt8: " << (int)num << std::endl;
                                        } catch (const boost::bad_any_cast&) {
                                            try {
                                                // Try as int8_t
                                                int8_t num = boost::any_cast<int8_t>(node);
                                                std::cout << "Int8: " << (int)num << std::endl;
                                            } catch (const boost::bad_any_cast&) {
                                                // Print the type info name for debugging
                                                std::cout << "Unknown type: " << node.type().name() << std::endl;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

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

// Function to analyze statistics data from a snapshot
void analyzeSnapshotStatistics(const Plist::dictionary_type& snapshotData, size_t snapshotIndex, size_t actualVoxelCount) {
    std::cout << "SNAPSHOT " << snapshotIndex << " STATISTICS:" << std::endl;
    
    if (snapshotData.find("st") == snapshotData.end()) {
        std::cout << "  No statistics found" << std::endl;
        return;
    }
    
    try {
        Plist::dictionary_type stats = boost::any_cast<Plist::dictionary_type>(snapshotData.at("st"));
        
        // Check for voxel count in statistics
        int64_t statsVoxelCount = -1;
        bool foundCount = false;
        
        // Check both 'c' and 'count' keys
        if (stats.find("c") != stats.end()) {
            try {
                statsVoxelCount = boost::any_cast<int64_t>(stats.at("c"));
                foundCount = true;
            } catch (const boost::bad_any_cast& e) {
                std::cout << "  Error extracting 'c' (count) value: " << e.what() << std::endl;
            }
        } else if (stats.find("count") != stats.end()) {
            try {
                statsVoxelCount = boost::any_cast<int64_t>(stats.at("count"));
                foundCount = true;
            } catch (const boost::bad_any_cast& e) {
                std::cout << "  Error extracting 'count' value: " << e.what() << std::endl;
            }
        }
        
        if (foundCount) {
            std::cout << "  Voxel count (from stats): " << statsVoxelCount << std::endl;
            std::cout << "  Voxel count (from data stream): " << actualVoxelCount << std::endl;
            
            if (statsVoxelCount > 0) {
                double ratio = static_cast<double>(actualVoxelCount) / statsVoxelCount;
                std::cout << "  Ratio (actual/stats): " << ratio << std::endl;
            }
        }
        
        // Helper function to print arrays
        auto printArray = [](const std::string& name, const Plist::array_type& array) {
            std::cout << "  " << name << " array: [";
            for (size_t i = 0; i < array.size(); i++) {
                if (i > 0) std::cout << ", ";
                try {
                    int64_t val = boost::any_cast<int64_t>(array[i]);
                    std::cout << val;
                } catch (const boost::bad_any_cast&) {
                    try {
                        int val = boost::any_cast<int>(array[i]);
                        std::cout << val;
                    } catch (const boost::bad_any_cast&) {
                        try {
                            double val = boost::any_cast<double>(array[i]);
                            std::cout << val;
                        } catch (const boost::bad_any_cast&) {
                            std::cout << "?";
                        }
                    }
                }
            }
            std::cout << "]" << std::endl;
        };
        
        // Check for min array
        if (stats.find("min") != stats.end()) {
            try {
                Plist::array_type minArray = boost::any_cast<Plist::array_type>(stats.at("min"));
                printArray("min", minArray);
            } catch (const boost::bad_any_cast& e) {
                std::cout << "  Error extracting min array: " << e.what() << std::endl;
            }
        }
        
        // Check for max array
        if (stats.find("max") != stats.end()) {
            try {
                Plist::array_type maxArray = boost::any_cast<Plist::array_type>(stats.at("max"));
                printArray("max", maxArray);
            } catch (const boost::bad_any_cast& e) {
                std::cout << "  Error extracting max array: " << e.what() << std::endl;
            }
        }
        
        // Check for position byte frequencies in the data stream
        if (snapshotData.find("ds") != snapshotData.end()) {
            try {
                Plist::data_type voxelData = boost::any_cast<Plist::data_type>(snapshotData.at("ds"));
                std::map<uint8_t, size_t> positionByteFrequency;
                
                // Count the frequency of each position byte (first byte of each pair)
                for (size_t i = 0; i < voxelData.size(); i += 2) {
                    if (i + 1 < voxelData.size()) {
                        uint8_t positionByte = voxelData[i];
                        positionByteFrequency[positionByte]++;
                    }
                }
                
                // Display frequencies
                for (const auto& [byte, count] : positionByteFrequency) {
                    std::cout << "  Position byte " << std::setw(2) << std::setfill('0') << std::hex << (int)byte 
                              << " occurs " << std::dec << count << " times" << std::endl;
                }
            } catch (const boost::bad_any_cast& e) {
                std::cout << "  Error analyzing position bytes: " << e.what() << std::endl;
            }
        }
        
    } catch (const boost::bad_any_cast& e) {
        std::cout << "  Error extracting statistics: " << e.what() << std::endl;
    }
}

// Function to create a Bella scene file from the extracted voxel data
bool outputBellaFile(const std::string& outputPath, size_t voxelCount) {
    // Comment out all Bella scene creation code to avoid errors
    std::cout << "Skipping Bella scene creation. Just printing voxel data." << std::endl;
    return true;
}

// Function to analyze a snapshot's location table in detail
void analyzeLocationTable(const Plist::data_type& locationTable, size_t snapshotIndex, int64_t chunkID) {
    std::cout << "LOCATION TABLE ANALYSIS for Snapshot " << snapshotIndex << " (ChunkID " << chunkID << "):" << std::endl;
    std::cout << "  Location table size: " << locationTable.size() << " bytes" << std::endl;
    
    // Count non-zero bytes
    int nonZeroCount = 0;
    for (size_t i = 0; i < locationTable.size(); i++) {
        if (locationTable[i] != 0) {
            nonZeroCount++;
            std::cout << "  Byte " << i << " = " << static_cast<int>(locationTable[i]) << std::endl;
        }
    }
    
    std::cout << "  Total non-zero bytes: " << nonZeroCount << " out of " << locationTable.size() << std::endl;
    
    // Print first 16 bytes as hex
    std::cout << "  First 16 bytes (hex): ";
    for (size_t i = 0; i < std::min(locationTable.size(), size_t(16)); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                 << static_cast<int>(locationTable[i]) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // Print last 16 bytes as hex
    if (locationTable.size() > 16) {
        std::cout << "  Last 16 bytes (hex): ";
        for (size_t i = locationTable.size() - 16; i < locationTable.size(); i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                     << static_cast<int>(locationTable[i]) << " ";
        }
        std::cout << std::dec << std::endl;
    }
}

// Function to analyze a snapshot's layer color usage data
void analyzeLayerColorUsage(const Plist::data_type& layerColorTable, size_t snapshotIndex, int64_t chunkID) {
    std::cout << "LAYER COLOR USAGE ANALYSIS for Snapshot " << snapshotIndex << " (ChunkID " << chunkID << "):" << std::endl;
    std::cout << "  Layer color table size: " << layerColorTable.size() << " bytes" << std::endl;
    
    // Count non-zero bytes (active color layers)
    int activeLayerCount = 0;
    for (size_t i = 0; i < layerColorTable.size(); i++) {
        if (layerColorTable[i] != 0) {
            activeLayerCount++;
            std::cout << "  Color Layer " << i << " is active (value=" << static_cast<int>(layerColorTable[i]) << ")" << std::endl;
        }
    }
    
    std::cout << "  Total active color layers: " << activeLayerCount << " out of " << layerColorTable.size() << " possible" << std::endl;
    
    // Print first 16 bytes as hex
    std::cout << "  First 16 color layers (hex): ";
    for (size_t i = 0; i < std::min(layerColorTable.size(), size_t(16)); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                 << static_cast<int>(layerColorTable[i]) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // Print last 16 bytes as hex
    if (layerColorTable.size() > 16) {
        std::cout << "  Last 16 color layers (hex): ";
        for (size_t i = layerColorTable.size() - 16; i < layerColorTable.size(); i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                     << static_cast<int>(layerColorTable[i]) << " ";
        }
        std::cout << std::dec << std::endl;
    }
}

// Function to parse the plist file and extract voxel data
bool parsePlistVoxelData(const std::string& filePath, bool verbose) {
    // Load and parse the plist file
    boost::any plistData;
    try {
        Plist::readPlist(filePath.c_str(), plistData);
    } catch (std::exception& e) {
        std::cerr << "Error reading plist file: " << e.what() << std::endl;
        return false;
    }

    // The root should be a dictionary
    Plist::dictionary_type rootDict;
    try {
        rootDict = boost::any_cast<Plist::dictionary_type>(plistData);
    } catch (std::exception& e) {
        std::cerr << "Error casting plist data to dictionary: " << e.what() << std::endl;
        return false;
    }

    // Print all keys in the root dictionary if verbose
    if (verbose) {
        std::cout << "Root keys: ";
        for (const auto& key : rootDict) {
            std::cout << key.first << " ";
        }
        std::cout << std::endl;
    }

    // Check if the snapshots key exists in the plist
    if (rootDict.find("snapshots") == rootDict.end()) {
        std::cerr << "Error: No 'snapshots' key found in plist" << std::endl;
        return false;
    }

    // Get the snapshots array
    Plist::array_type snapshotsArray;
    try {
        snapshotsArray = boost::any_cast<Plist::array_type>(rootDict.at("snapshots"));
    } catch (std::exception& e) {
        std::cerr << "Error casting snapshots to array: " << e.what() << std::endl;
        return false;
    }
    
    // Print the structure of the first few snapshots
    if (verbose) {
        std::cout << "\nDetailed structure of the first snapshot:" << std::endl;
        if (!snapshotsArray.empty()) {
            printPlistStructure(snapshotsArray[0], 0, "snapshots[0]");
        }
    }
    
    // Statistics tracking
    int totalSnapshots = snapshotsArray.size();
    size_t totalLocationTableBytes = 0;
    size_t totalVoxelDataBytes = 0;
    std::set<std::string> uniqueChunkIds; // Track unique chunk IDs
    
    // Additional statistics for snapshot types
    std::map<int64_t, int> snapshotTypeCount; // Count occurrences of each snapshot type
    
    std::cout << "Total snapshots in plist: " << totalSnapshots << std::endl;
    
    // World model: map of chunk index to chunk data
    std::map<std::string, Chunk> worldModel;
    
    // Process each snapshot in the plist sequentially
    for (size_t i = 0; i < snapshotsArray.size(); i++) {
        try {
            Plist::dictionary_type snapshot = boost::any_cast<Plist::dictionary_type>(snapshotsArray[i]);
            
            // DEBUG: Print all keys in this snapshot
            if (verbose && i < 5) { // Only print for first 5 snapshots to avoid excessive output
                std::cout << "\nSnapshot " << i << " Keys: ";
                for (const auto& key : snapshot) {
                    std::cout << "'" << key.first << "' ";
                }
                std::cout << std::endl;
                
                // If snapshot has a 's' key, print all keys in that dictionary too
                if (snapshot.find("s") != snapshot.end()) {
                    try {
                        Plist::dictionary_type snapshotData = boost::any_cast<Plist::dictionary_type>(snapshot.at("s"));
                        std::cout << "  Snapshot " << i << " 's' Dictionary Keys: ";
                        for (const auto& key : snapshotData) {
                            std::cout << "'" << key.first << "' ";
                        }
                        std::cout << std::endl;
                    } catch (std::exception& e) {
                        std::cout << "  Error getting 's' dictionary keys: " << e.what() << std::endl;
                    }
                }
            }
            
            // Variables to store snapshot metadata
            std::string chunkId = "unknown";
            bool foundChunkID = false;
            int64_t sessionId = -1;
            int64_t typeId = -1;
            std::string typeName = "unknown";
            size_t voxelsInSnapshot = 0;  // Track voxel count
            
            // Extract chunk ID (cid) if present
            if (snapshot.find("c") != snapshot.end()) {
                try {
                    chunkId = boost::any_cast<std::string>(snapshot.at("c"));
                    uniqueChunkIds.insert(chunkId);
                    
                    if (verbose) {
                        std::cout << "\nProcessing Snapshot " << i << " with Chunk ID: " << chunkId << std::endl;
                    }
                } catch (std::exception& e) {
                    // If casting fails, try as an integer
                    try {
                        int cid = boost::any_cast<int>(snapshot.at("c"));
                        chunkId = std::to_string(cid);
                        uniqueChunkIds.insert(chunkId);
                        
                        if (verbose) {
                            std::cout << "\nProcessing Snapshot " << i << " with Chunk ID: " << chunkId << std::endl;
                        }
                    } catch (std::exception& e2) {
                        if (verbose) {
                            std::cout << "\nProcessing Snapshot " << i << " (couldn't extract chunk ID)" << std::endl;
                        }
                    }
                }
            } else if (verbose) {
                std::cout << "\nProcessing Snapshot " << i << " (no chunk ID found)" << std::endl;
            }
            
            // Each snapshot has an 's' key containing voxel data
            if (snapshot.find("s") != snapshot.end()) {
                Plist::dictionary_type snapshotData = boost::any_cast<Plist::dictionary_type>(snapshot.at("s"));
                
                // Try to get chunk ID from 's' dictionary if it has an 'id' key
                if (snapshotData.find("id") != snapshotData.end()) {
                    try {
                        // The 'id' field is a dictionary containing the chunk ID
                        Plist::dictionary_type idDict = boost::any_cast<Plist::dictionary_type>(snapshotData.at("id"));
                        
                        // DEBUG: Print all keys in the 'id' dictionary for first few snapshots
                        if (verbose && i < 3) {
                            std::cout << "  's.id' Dictionary Keys: ";
                            for (const auto& key : idDict) {
                                std::cout << "'" << key.first << "' ";
                            }
                            std::cout << std::endl;
                            
                            // Try to extract and print the values in the dictionary with more detailed type info
                            for (const auto& [key, value] : idDict) {
                                std::cout << "    '" << key << "': ";
                                std::cout << "Type: '" << value.type().name() << "' ";
                                
                                // Try a variety of data types
                                try {
                                    std::string strValue = boost::any_cast<std::string>(value);
                                    std::cout << "String '" << strValue << "'";
                                } catch (boost::bad_any_cast&) {
                                    try {
                                        int intValue = boost::any_cast<int>(value);
                                        std::cout << "Int " << intValue;
                                    } catch (boost::bad_any_cast&) {
                                        try {
                                            double doubleValue = boost::any_cast<double>(value);
                                            std::cout << "Double " << doubleValue;
                                        } catch (boost::bad_any_cast&) {
                                            try {
                                                float floatValue = boost::any_cast<float>(value);
                                                std::cout << "Float " << floatValue;
                                            } catch (boost::bad_any_cast&) {
                                                try {
                                                    int64_t int64Value = boost::any_cast<int64_t>(value);
                                                    std::cout << "Int64 " << int64Value;
                                                } catch (boost::bad_any_cast&) {
                                                    try {
                                                        bool boolValue = boost::any_cast<bool>(value);
                                                        std::cout << "Bool " << (boolValue ? "true" : "false");
                                                    } catch (boost::bad_any_cast&) {
                                                        try {
                                                            Plist::dictionary_type dictValue = boost::any_cast<Plist::dictionary_type>(value);
                                                            std::cout << "Dictionary with " << dictValue.size() << " keys";
                                                        } catch (boost::bad_any_cast&) {
                                                            try {
                                                                Plist::array_type arrayValue = boost::any_cast<Plist::array_type>(value);
                                                                std::cout << "Array with " << arrayValue.size() << " elements";
                                                            } catch (boost::bad_any_cast&) {
                                                                std::cout << "Unknown type - couldn't cast";
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                std::cout << std::endl;
                            }
                        }
                        
                        // Now look for 'c' in the id dictionary
                        if (idDict.find("c") != idDict.end()) {
                            try {
                                // These are type Int64 in plist
                                int64_t chunkId_int = boost::any_cast<int64_t>(idDict.at("c"));
                                chunkId = std::to_string(chunkId_int);
                                
                                if (verbose) {
                                    std::cout << "  Found chunk ID: " << chunkId << std::endl;
                                }
                                uniqueChunkIds.insert(chunkId);
                                foundChunkID = true;
                            } catch (boost::bad_any_cast&) {
                                if (verbose) {
                                    std::cout << "  Couldn't cast chunk ID as Int64" << std::endl;
                                }
                            }
                        } else if (verbose) {
                            std::cout << "  No 'c' key found in 's.id' dictionary" << std::endl;
                        }
                        
                        // Also check for session ID ('s') 
                        if (idDict.find("s") != idDict.end()) {
                            try {
                                sessionId = boost::any_cast<int64_t>(idDict.at("s"));
                                if (verbose) {
                                    std::cout << "  Found session ID: " << sessionId << std::endl;
                                }
                            } catch (boost::bad_any_cast&) {
                                if (verbose) {
                                    std::cout << "  Couldn't cast session ID as Int64" << std::endl;
                                }
                            }
                        }
                        
                        // Check for type ID ('t')
                        if (idDict.find("t") != idDict.end()) {
                            try {
                                typeId = boost::any_cast<int64_t>(idDict.at("t"));
                                typeName = getSnapshotTypeName(typeId);
                                snapshotTypeCount[typeId]++;
                                
                                if (verbose) {
                                    std::cout << "  Found type ID: " << typeId << " (" << typeName << ")" << std::endl;
                                }
                            } catch (boost::bad_any_cast&) {
                                if (verbose) {
                                    std::cout << "  Couldn't cast type ID as Int64" << std::endl;
                                }
                            }
                        }
                    } catch (boost::bad_any_cast&) {
                        if (verbose) {
                            std::cout << "  's.id' is not a dictionary" << std::endl;
                        }
                    }
                } else if (verbose) {
                    std::cout << "  No 'id' key found in 's' dictionary" << std::endl;
                }
                
                // Get voxel data stream for counting
                if (snapshotData.find("ds") != snapshotData.end()) {
                    Plist::data_type voxelData = boost::any_cast<Plist::data_type>(snapshotData.at("ds"));
                    voxelsInSnapshot = voxelData.size() / 2; // Each voxel is 2 bytes
                }
                
                // Print snapshot summary even in non-verbose mode
                if (!chunkId.empty()) {
                    int64_t chunkID_int = std::stoll(chunkId);
                    
                    // Get world origin coordinates using Morton decoding
                    auto [worldOriginX, worldOriginY, worldOriginZ] = decodeMortonChunkID(chunkID_int);
                    
                    // Scale by chunk size (32 instead of 8 for larger chunks)
                    worldOriginX *= 32;
                    worldOriginY *= 32;
                    worldOriginZ *= 32;
                    
                    std::cout << "Snapshot " << i << ": ChunkID=" << chunkId 
                              << ", World Origin=(" << worldOriginX << "," << worldOriginY << "," << worldOriginZ << ")"
                              << ", Type=" << typeName
                              << ", SessionID=" << sessionId 
                              << ", Data Stream Voxels=" << voxelsInSnapshot << std::endl;
                    
                    // Check if this is a checkpoint type (type ID 4) with voxel data
                    if (typeId == 4 && snapshotData.find("ds") != snapshotData.end()) {
                        Plist::data_type voxelData = boost::any_cast<Plist::data_type>(snapshotData.at("ds"));
                        if (!voxelData.empty()) {
                            std::cout << "  CHECKPOINT CHUNK WORLD ORIGIN: (" 
                                      << worldOriginX << "," << worldOriginY << "," << worldOriginZ 
                                      << ")" << std::endl;
                        }
                    }
                    
                    // Analyze statistics data for this snapshot
                    analyzeSnapshotStatistics(snapshotData, i, voxelsInSnapshot);
                } else {
                    std::cout << "Snapshot " << i << ": ChunkID=unknown"
                              << ", World Origin=(unknown,unknown,unknown)"
                              << ", Type=" << typeName
                              << ", SessionID=" << sessionId 
                              << ", Data Stream Voxels=" << voxelsInSnapshot << std::endl;
                }
                
                // Check for location table (lc) and voxel data stream (ds)
                if (snapshotData.find("lc") != snapshotData.end() && snapshotData.find("ds") != snapshotData.end()) {
                    // Get layer color usage table
                    Plist::data_type locationTable = boost::any_cast<Plist::data_type>(snapshotData.at("lc"));
                    totalLocationTableBytes += locationTable.size();
                    
                    // Add this to analyze the location table in detail
                    if (i < 5 || i % 10 == 0) { // Only analyze a subset of snapshots to avoid excessive output
                        analyzeLayerColorUsage(locationTable, i, foundChunkID ? std::stoll(chunkId) : -1);
                    }
                    
                    // Get voxel data stream
                    Plist::data_type voxelData = boost::any_cast<Plist::data_type>(snapshotData.at("ds"));
                    totalVoxelDataBytes += voxelData.size();
                    
                    if (verbose) {
                        std::cout << "  Location table size: " << locationTable.size() << " bytes" << std::endl;
                        std::cout << "  Voxel data size: " << voxelData.size() << " bytes" << std::endl;
                    }
                    
                    // Analyze Morton codes in the data stream
                    analyzeDataStreamMortonCodes(voxelData, i);
                    
                    // Find chunks modified in this snapshot
                    std::vector<uint8_t> modifiedChunks;
                    for (size_t j = 0; j < locationTable.size(); j++) {
                        if (locationTable[j] != 0) {
                            modifiedChunks.push_back(j);
                            
                            // Decode chunk location from Morton code
                            uint8_t chunkX, chunkY, chunkZ;
                            decodeChunkMorton(j, chunkX, chunkY, chunkZ);
                            
                            if (verbose) {
                                std::cout << "  Modified chunk " << (int)j << " at (" 
                                        << (int)chunkX << "," << (int)chunkY << "," << (int)chunkZ 
                                        << ")" << std::endl;
                            }
                        }
                    }
                    
                    // Count voxels in the data stream (each voxel is 2 bytes: position and color)
                    size_t voxelsInSnapshot = voxelData.size() / 2;
                    
                    // Simple validation check
                    if (voxelsInSnapshot == 0 || modifiedChunks.empty()) {
                        if (verbose) {
                            std::cout << "  Warning: Snapshot contains no voxels or no modified chunks" << std::endl;
                        }
                        continue; // Skip this snapshot
                    }

                    if (verbose) {
                        std::cout << "  Voxels in snapshot: " << voxelsInSnapshot << std::endl;
                        std::cout << "  Modified chunks: " << modifiedChunks.size() << std::endl;
                    }
                    
                    // Extract the chunk ID we found earlier to use as the key in our world model
                    std::string modelChunkKey = foundChunkID ? std::to_string(i) + "_" + chunkId : std::to_string(i);
                    
                    // Interpret the voxel data
                    for (uint8_t chunkIndex : modifiedChunks) {
                        // Decode chunk coordinates
                        uint8_t chunkX, chunkY, chunkZ;
                        decodeChunkMorton(chunkIndex, chunkX, chunkY, chunkZ);
                        
                        // Create a unique key using the chunk ID if available, or just the chunk index
                        std::string chunkKey = modelChunkKey + "_" + std::to_string(chunkIndex);
                        
                        // Get existing chunk or create a new one in our modified world model map
                        std::map<std::string, Chunk>& worldModelByChunkId = worldModel;
                        Chunk& chunk = worldModelByChunkId[chunkKey];
                        
                        if (chunk.voxels.empty()) {
                            // If this is a new chunk, initialize its coordinates
                            chunk.chunkX = chunkX;
                            chunk.chunkY = chunkY;
                            chunk.chunkZ = chunkZ;
                        }
                        
                        // Create a vector for this chunk's voxels
                        std::vector<Voxel> newVoxels;
                        
                        // Pre-allocate a reasonable amount of memory to avoid frequent reallocations
                        newVoxels.reserve(voxelsInSnapshot / modifiedChunks.size());
                        
                        // Loop through voxel data and populate the chunk
                        for (size_t j = 0; j < voxelData.size(); j += 2) {
                            if (j + 1 < voxelData.size()) {
                                uint8_t position = static_cast<uint8_t>(voxelData[j]);
                                uint8_t color = static_cast<uint8_t>(voxelData[j + 1]);
                                newVoxels.emplace_back(position, color);
                            }
                        }
                        
                        // For normal snapshots, replace previous voxels in the chunk
                        // This assumes that each snapshot contains a complete, updated state for the chunk
                        chunk.voxels = newVoxels;
                        
                        if (verbose) {
                            std::cout << "  Updated chunk " << chunkKey << " with " 
                                    << newVoxels.size() << " voxels" << std::endl;
                        }
                    }
                }
            }
        } catch (std::exception& e) {
            std::cerr << "Error processing snapshot " << i << ": " << e.what() << std::endl;
        }
    }
    
    // Print snapshot type statistics
    std::cout << "\nSNAPSHOT TYPE SUMMARY:" << std::endl;
    for (const auto& [type, count] : snapshotTypeCount) {
        std::cout << "  " << getSnapshotTypeName(type) << ": " << count << " snapshots (" 
                 << (count * 100.0 / totalSnapshots) << "%)" << std::endl;
    }
    
    // Print information about unique chunk IDs
    std::cout << "\nFound " << uniqueChunkIds.size() << " unique chunk IDs across " 
              << totalSnapshots << " snapshots" << std::endl;
    
    // Print information about the final world model
    std::cout << "\nFINAL WORLD MODEL:" << std::endl;
    std::cout << "Total chunks occupied: " << worldModel.size() << std::endl;
    
    // Count total voxels in the final model
    size_t totalVoxels = 0;
    for (const auto& chunkEntry : worldModel) {
        totalVoxels += chunkEntry.second.voxels.size();
    }
    std::cout << "Total voxels in model: " << totalVoxels << std::endl;
    
    // Detailed output for each chunk if verbose
    if (verbose) {
        std::cout << "\nDetailed chunk information:" << std::endl;
        
        for (const auto& chunkEntry : worldModel) {
            std::string chunkKey = chunkEntry.first;
            const Chunk& chunk = chunkEntry.second;
            
            std::cout << "Chunk " << chunkKey << " at position (" 
                    << (int)chunk.chunkX << "," << (int)chunk.chunkY << "," << (int)chunk.chunkZ 
                    << ") contains " << chunk.voxels.size() << " voxels" << std::endl;
            
            // Print color distribution
            std::map<uint8_t, int> colorCount;
            for (const auto& voxel : chunk.voxels) {
                colorCount[voxel.color]++;
            }
            
            std::cout << "  Color distribution:" << std::endl;
            for (const auto& [color, count] : colorCount) {
                std::cout << "    Color " << (int)color << ": " << count << " voxels" << std::endl;
            }
            
            // Count voxels with non-zero positions
            int nonZeroPos = 0;
            for (const auto& voxel : chunk.voxels) {
                if (voxel.position != 0) nonZeroPos++;
            }
            
            std::cout << "  Non-zero positions: " << nonZeroPos << " out of " 
                    << chunk.voxels.size() << std::endl;
            
            // Get voxels using correct interpretation
            auto voxels = chunk.getVoxels();
            std::cout << "  Total voxels: " << voxels.size() << std::endl;
            
            // Show sample voxels
            if (!voxels.empty()) {
                std::cout << "  Sample voxels:" << std::endl;
                for (int i = 0; i < std::min(5, (int)voxels.size()); i++) {
                    auto [x, y, z, color] = voxels[i];
                    std::cout << "    Voxel " << i << ": local=(" << (int)x << "," << (int)y << "," << (int)z 
                            << ") color=" << (int)color << std::endl;
                }
            }
            
            // Print world coordinates for voxels in verbose mode
            if (verbose) {
                auto worldVoxels = chunk.getWorldVoxels();
                std::cout << "  World coordinates (showing first 10):" << std::endl;
                
                int count = 0;
                std::map<std::tuple<uint16_t, uint16_t, uint16_t>, int> uniquePositions;
                
                for (const auto& [worldX, worldY, worldZ, color] : worldVoxels) {
                    // Track unique positions
                    uniquePositions[std::make_tuple(worldX, worldY, worldZ)]++;
                    
                    // Only show first 10 to avoid overwhelming output
                    if (count < 10) {
                        std::cout << "    Voxel " << count << ": world=(" << worldX << "," << worldY << "," << worldZ 
                                << ") color=" << (int)color << std::endl;
                    }
                    count++;
                }
                
                std::cout << "    Total voxels: " << worldVoxels.size() << std::endl;
                std::cout << "    Unique positions: " << uniquePositions.size() << std::endl;
                
                // Show runs with world coordinates if there are interesting patterns
                if (worldVoxels.size() > 1 && uniquePositions.size() < worldVoxels.size() / 2) {
                    std::cout << "    World runs (first 5):" << std::endl;
                    int runCount = 0;
                    
                    for (const auto& [pos, count] : uniquePositions) {
                        if (runCount < 5) {
                            auto [x, y, z] = pos;
                            std::cout << "      Position (" << x << "," << y << "," << z 
                                    << "): " << count << " voxels" << std::endl;
                        }
                        runCount++;
                    }
                }
            }
            
            // Only print visualization for a few sample chunks (to keep output manageable)
            static int sampleCounter = 0;
            if (worldModel.size() <= 5 || (sampleCounter++ % (worldModel.size() / 5 + 1) == 0)) {
                // Print raw voxel data representation instead of visualization
                std::cout << "  Raw voxel data summary:" << std::endl;
                
                // Count position=0 voxels
                int zeroPositionCount = 0;
                for (const auto& voxel : chunk.voxels) {
                    if (voxel.position == 0) zeroPositionCount++;
                }
                
                std::cout << "    Position=0 voxels: " << zeroPositionCount 
                         << " (" << (zeroPositionCount * 100.0 / chunk.voxels.size()) << "%)" << std::endl;
                
                // Analyze color transitions
                if (chunk.voxels.size() >= 2) {
                    int transitions = 0;
                    for (size_t i = 1; i < chunk.voxels.size(); i++) {
                        if (chunk.voxels[i].color != chunk.voxels[i-1].color) {
                            transitions++;
                        }
                    }
                    std::cout << "    Color transitions: " << transitions << std::endl;
                    
                    // Count runs of same color
                    int currentRunLength = 1;
                    int maxRunLength = 1;
                    std::vector<int> runLengths;
                    
                    for (size_t i = 1; i < chunk.voxels.size(); i++) {
                        if (chunk.voxels[i].color == chunk.voxels[i-1].color) {
                            currentRunLength++;
                        } else {
                            runLengths.push_back(currentRunLength);
                            if (currentRunLength > maxRunLength) maxRunLength = currentRunLength;
                            currentRunLength = 1;
                        }
                    }
                    runLengths.push_back(currentRunLength);
                    if (currentRunLength > maxRunLength) maxRunLength = currentRunLength;
                    
                    // Calculate average run length
                    double avgRunLength = 0;
                    for (int len : runLengths) {
                        avgRunLength += len;
                    }
                    avgRunLength /= runLengths.size();
                    
                    std::cout << "    Average color run length: " << avgRunLength << " voxels" << std::endl;
                    std::cout << "    Max color run length: " << maxRunLength << " voxels" << std::endl;
                    std::cout << "    Number of color runs: " << runLengths.size() << std::endl;
                    
                    // Show first few and last few color runs
                    int runsToShow = std::min(3, (int)runLengths.size());
                    std::cout << "    First " << runsToShow << " color runs: ";
                    for (int i = 0; i < runsToShow; i++) {
                        if (i > 0) std::cout << ", ";
                        std::cout << runLengths[i];
                    }
                    std::cout << std::endl;
                    
                    if (runLengths.size() > 6) {
                        std::cout << "    Last " << runsToShow << " color runs: ";
                        for (int i = runLengths.size() - runsToShow; i < runLengths.size(); i++) {
                            if (i > runLengths.size() - runsToShow) std::cout << ", ";
                            std::cout << runLengths[i];
                        }
                        std::cout << std::endl;
                    }
                }
            }
        }
    }
    
    // Check how full the voxel space is compared to a fully filled 256×256×256 voxel space
    const size_t maxVoxels = 256 * 256 * 256;  // Maximum possible voxels in the space
    double percentFull = (double)totalVoxels / maxVoxels * 100.0;
    std::cout << "Voxel space occupancy: " << percentFull << "% of maximum (" 
            << totalVoxels << " out of " << maxVoxels << " voxels)" << std::endl;
    
    return true;
}

// Function to print all visible voxels after processing all snapshots
void printAllVisibleVoxels(const std::string& filePath) {
    boost::any plistData;
    try {
        Plist::readPlist(filePath.c_str(), plistData);
    } catch (std::exception& e) {
        std::cerr << "Error reading plist file: " << e.what() << std::endl;
        return;
    }

    // Get root dictionary
    Plist::dictionary_type rootDict = boost::any_cast<Plist::dictionary_type>(plistData);
    if (rootDict.find("snapshots") == rootDict.end()) {
        std::cerr << "Error: No 'snapshots' key found in plist" << std::endl;
        return;
    }

    // Get the snapshots array
    Plist::array_type snapshotsArray = boost::any_cast<Plist::array_type>(rootDict.at("snapshots"));
    std::cout << "Processing " << snapshotsArray.size() << " snapshots..." << std::endl;

    // Map to store final voxel state: key is chunkID, value is a map of voxel positions to colors
    // A special value of color=0 means "no voxel here" (empty)
    std::map<int64_t, std::map<std::tuple<int, int, int>, uint8_t>> finalVoxelState;
    
    // Map to store snapshot indices by chunk ID for debugging
    std::map<int64_t, std::vector<size_t>> snapshotIndicesByChunk;

    // First pass: Process all snapshots in order to build the final voxel state
    for (size_t snapshotIndex = 0; snapshotIndex < snapshotsArray.size(); snapshotIndex++) {
        try {
            Plist::dictionary_type snapshot = boost::any_cast<Plist::dictionary_type>(snapshotsArray[snapshotIndex]);
            
            // Skip if no 's' key
            if (snapshot.find("s") == snapshot.end()) continue;
            
            Plist::dictionary_type snapshotData = boost::any_cast<Plist::dictionary_type>(snapshot.at("s"));
            
            // Skip if no 'id' dictionary
            if (snapshotData.find("id") == snapshotData.end()) continue;
            
            Plist::dictionary_type idDict = boost::any_cast<Plist::dictionary_type>(snapshotData.at("id"));
            
            // Skip if no chunk ID
            if (idDict.find("c") == idDict.end()) continue;
            
            // Get chunk ID
            int64_t chunkID = boost::any_cast<int64_t>(idDict.at("c"));
            
            // Track this snapshot index for this chunk
            snapshotIndicesByChunk[chunkID].push_back(snapshotIndex);
            
            // Skip if no voxel data
            if (snapshotData.find("ds") == snapshotData.end()) continue;
            
            // Get world origin coordinates for this chunk
            auto [chunkX, chunkY, chunkZ] = decodeMortonChunkID(chunkID);
            
            // Scale by chunk size (32)
            int worldOriginX = chunkX * 32;
            int worldOriginY = chunkY * 32;
            int worldOriginZ = chunkZ * 32;
            
            // Get voxel data
            Plist::data_type voxelData = boost::any_cast<Plist::data_type>(snapshotData.at("ds"));
            
            // Process voxel data using hybrid encoding
            int localX = 0, localY = 0, localZ = 0; // Current position counter within chunk
            
            // Process each voxel pair (position, color)
            for (size_t i = 0; i < voxelData.size(); i += 2) {
                if (i + 1 < voxelData.size()) {
                    uint8_t position = static_cast<uint8_t>(voxelData[i]);
                    uint8_t color = static_cast<uint8_t>(voxelData[i + 1]);
                    
                    if (position != 0) {
                        // Non-zero position: Jump to Morton-encoded position
                        uint8_t mortonX = 0, mortonY = 0, mortonZ = 0;
                        decodeMorton(position, mortonX, mortonY, mortonZ);
                        localX = mortonX;
                        localY = mortonY;
                        localZ = mortonZ;
                    }
                    
                    // Calculate world coordinates
                    int worldX = worldOriginX + localX;
                    int worldY = worldOriginY + localY;
                    int worldZ = worldOriginZ + localZ;
                    
                    // Update voxel state for this position
                    // Note: color=0 means "no voxel here" (empty space)
                    finalVoxelState[chunkID][std::make_tuple(worldX, worldY, worldZ)] = color;
                    
                    // Increment position counter for next voxel (only if current position is 0)
                    if (position == 0) {
                        // Increment in x-first, then y, then z order (matches typical voxel traversal)
                        localX++;
                        if (localX >= 32) {
                            localX = 0;
                            localY++;
                            if (localY >= 32) {
                                localY = 0;
                                localZ++;
                                // If we've gone beyond the chunk bounds, wrap around
                                if (localZ >= 32) {
                                    localZ = 0;
                                }
                            }
                        }
                    }
                }
            }
        } catch (std::exception& e) {
            std::cerr << "Error processing snapshot " << snapshotIndex << ": " << e.what() << std::endl;
        }
    }

    // Count the total number of visible voxels (color != 0)
    size_t totalVisibleVoxels = 0;
    for (const auto& [chunkID, voxelMap] : finalVoxelState) {
        for (const auto& [pos, color] : voxelMap) {
            if (color != 0) {
                totalVisibleVoxels++;
            }
        }
    }
    
    std::cout << "\n===== FINAL VISIBLE VOXEL COUNT: " << totalVisibleVoxels << " =====" << std::endl;
    
    // Print snapshot history for each chunk
    std::cout << "\nSNAPSHOT HISTORY BY CHUNK:" << std::endl;
    for (const auto& [chunkID, indices] : snapshotIndicesByChunk) {
        auto [chunkX, chunkY, chunkZ] = decodeMortonChunkID(chunkID);
        std::cout << "  ChunkID " << chunkID << " (at chunk position " 
                  << chunkX << "," << chunkY << "," << chunkZ 
                  << ") appears in " << indices.size() << " snapshots: ";
        
        for (size_t i = 0; i < std::min(indices.size(), size_t(5)); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << indices[i];
        }
        
        if (indices.size() > 5) {
            std::cout << ", ... (and " << (indices.size() - 5) << " more)";
        }
        std::cout << std::endl;
    }
    
    // Print visible voxels (limiting to first 100 to avoid excessive output)
    std::cout << "\n===== VISIBLE VOXELS (first 100): =====" << std::endl;
    size_t voxelsPrinted = 0;
    
    std::vector<std::tuple<int, int, int, uint8_t, int, int, int>> allVisibleVoxels;
    
    // First collect all visible voxels
    for (const auto& [chunkID, voxelMap] : finalVoxelState) {
        auto [chunkX, chunkY, chunkZ] = decodeMortonChunkID(chunkID);
        int worldOriginX = chunkX * 32;
        int worldOriginY = chunkY * 32;
        int worldOriginZ = chunkZ * 32;
        
        for (const auto& [pos, color] : voxelMap) {
            if (color != 0) {  // Only include visible voxels
                auto [worldX, worldY, worldZ] = pos;
                int localX = worldX - worldOriginX;
                int localY = worldY - worldOriginY;
                int localZ = worldZ - worldOriginZ;
                
                allVisibleVoxels.emplace_back(worldX, worldY, worldZ, color, chunkX, chunkY, chunkZ);
            }
        }
    }
    
    // Sort voxels by world coordinates to make the output more organized
    std::sort(allVisibleVoxels.begin(), allVisibleVoxels.end(), 
        [](const auto& a, const auto& b) {
            const auto& [aX, aY, aZ, aColor, aChunkX, aChunkY, aChunkZ] = a;
            const auto& [bX, bY, bZ, bColor, bChunkX, bChunkY, bChunkZ] = b;
            
            if (aX != bX) return aX < bX;
            if (aY != bY) return aY < bY;
            return aZ < bZ;
        });
    
    // Print sorted voxels
    for (const auto& voxel : allVisibleVoxels) {
        auto [worldX, worldY, worldZ, color, chunkX, chunkY, chunkZ] = voxel;
        int worldOriginX = chunkX * 32;
        int worldOriginY = chunkY * 32;
        int worldOriginZ = chunkZ * 32;
        int localX = worldX - worldOriginX;
        int localY = worldY - worldOriginY;
        int localZ = worldZ - worldOriginZ;
        
        std::cout << "  World(" << worldX << "," << worldY << "," << worldZ 
                  << "), Chunk(" << chunkX << "," << chunkY << "," << chunkZ
                  << "), Local(" << localX << "," << localY << "," << localZ
                  << "), Color=" << (int)color << std::endl;
        
        voxelsPrinted++;
        if (voxelsPrinted >= 100) {
            break;
        }
    }
    
    if (totalVisibleVoxels > 100) {
        std::cout << "  ... and " << (totalVisibleVoxels - 100) << " more voxels" << std::endl;
    }
}

// Add the following function before the main function
void justPrintVoxelCoordinates(const std::string& filePath) {
    // Disable cout temporarily to prevent it from printing tool output
    std::streambuf* oldCoutStreamBuf = std::cout.rdbuf();
    std::ostringstream strCout;
    std::cout.rdbuf(strCout.rdbuf());

    // Process the file quietly
    boost::any plistData;
    try {
        Plist::readPlist(filePath.c_str(), plistData);
    } catch (std::exception& e) {
        // Restore cout before reporting error
        std::cout.rdbuf(oldCoutStreamBuf);
        std::cerr << "Error: " << e.what() << std::endl;
        return;
    }

    // Get snapshots array
    Plist::dictionary_type rootDict = boost::any_cast<Plist::dictionary_type>(plistData);
    Plist::array_type snapshotsArray = boost::any_cast<Plist::array_type>(rootDict.at("snapshots"));
    
    // Map to store final voxel state
    std::map<int64_t, std::map<std::tuple<int, int, int>, uint8_t>> voxelMap;
    
    // Process all snapshots
    for (size_t i = 0; i < snapshotsArray.size(); i++) {
        try {
            Plist::dictionary_type snapshot = boost::any_cast<Plist::dictionary_type>(snapshotsArray[i]);
            
            if (snapshot.find("s") == snapshot.end()) continue;
            Plist::dictionary_type snapData = boost::any_cast<Plist::dictionary_type>(snapshot.at("s"));
            
            if (snapData.find("id") == snapData.end()) continue;
            Plist::dictionary_type idDict = boost::any_cast<Plist::dictionary_type>(snapData.at("id"));
            
            if (idDict.find("c") == idDict.end()) continue;
            int64_t chunkID = boost::any_cast<int64_t>(idDict.at("c"));
            
            if (snapData.find("ds") == snapData.end()) continue;
            Plist::data_type voxelData = boost::any_cast<Plist::data_type>(snapData.at("ds"));
            
            // Get chunk coordinates and world origin
            auto [chunkX, chunkY, chunkZ] = decodeMortonChunkID(chunkID);
            int worldOriginX = chunkX * 32;
            int worldOriginY = chunkY * 32;
            int worldOriginZ = chunkZ * 32;
            
            // Process voxels
            int localX = 0, localY = 0, localZ = 0;
            for (size_t j = 0; j < voxelData.size(); j += 2) {
                if (j + 1 >= voxelData.size()) continue;
                
                uint8_t position = static_cast<uint8_t>(voxelData[j]);
                uint8_t color = static_cast<uint8_t>(voxelData[j + 1]);
                
                if (position != 0) {
                    uint8_t mortonX = 0, mortonY = 0, mortonZ = 0;
                    decodeMorton(position, mortonX, mortonY, mortonZ);
                    localX = mortonX;
                    localY = mortonY;
                    localZ = mortonZ;
                }
                
                int worldX = worldOriginX + localX;
                int worldY = worldOriginY + localY;
                int worldZ = worldOriginZ + localZ;
                
                voxelMap[chunkID][std::make_tuple(worldX, worldY, worldZ)] = color;
                
                if (position == 0) {
                    localX++;
                    if (localX >= 32) {
                        localX = 0;
                        localY++;
                        if (localY >= 32) {
                            localY = 0;
                            localZ++;
                            if (localZ >= 32) {
                                localZ = 0;
                            }
                        }
                    }
                }
            }
        } catch (std::exception& e) {
            // Just continue to next snapshot
        }
    }
    
    // Collect visible voxels
    std::vector<std::tuple<int, int, int, uint8_t>> visible;
    for (const auto& [_, chunk] : voxelMap) {
        for (const auto& [pos, color] : chunk) {
            if (color != 0) {
                auto [x, y, z] = pos;
                visible.emplace_back(x, y, z, color);
            }
        }
    }
    
    // Sort by coordinates
    std::sort(visible.begin(), visible.end());
    
    // Restore cout before printing coordinates
    std::cout.rdbuf(oldCoutStreamBuf);
    
    // Print only coordinates
    std::cout << "START VOXEL COORDINATES" << std::endl;
    for (const auto& [x, y, z, color] : visible) {
        std::cout << x << "," << y << "," << z << "," << (int)color << std::endl;
    }
    std::cout << "END VOXEL COORDINATES" << std::endl;
    std::cout << "TOTAL COORDINATES: " << visible.size() << std::endl;
}


// Main function for the program
// This is where execution begins
// The Args object contains command-line arguments
int DL_main(dl::Args& args)
{
    // Variable to store the input file path
    std::string filePath;
    std::string lzfseInputPath;
    std::string plistOutputPath;
    bool verbose = false;

    // Define command-line arguments that the program accepts
    args.add("vi",  "voxin", "",   "Input .vox file");
    args.add("tp",  "thirdparty",   "",   "prints third party licenses");
    args.add("li",  "licenseinfo",   "",   "prints license info");
    args.add("lz",  "lzfsein", "",   "Input LZFSE compressed file");
    args.add("po",  "plistout", "",   "Output plist file path");
    args.add("pl",  "plist", "",   "Input plist file to parse directly");
    args.add("v",   "verbose", "",   "Enable verbose output");

    // Handle special command-line requests
    
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

    // Process LZFSE compressed files
    if (args.have("--lzfsein")) {
        lzfseInputPath = args.value("--lzfsein").buf();
        
        if (args.have("--plistout")) {
            plistOutputPath = args.value("--plistout").buf();
        } else {
            // Default output path: change .vmax to .plist
            plistOutputPath = lzfseInputPath + ".plist";
        }
        
        std::cout << "Decompressing LZFSE file: " << lzfseInputPath << " to " << plistOutputPath << std::endl;
        if (!decompressLzfseToPlist(lzfseInputPath, plistOutputPath)) {
            std::cerr << "Failed to decompress LZFSE file" << std::endl;
        return 1; 
    }

        // Parse the plist file
        std::cout << "Parsing plist file..." << std::endl;
        if (!parsePlistVoxelData(plistOutputPath, verbose)) {
            std::cerr << "Failed to parse plist file" << std::endl;
        return 1;
    }

        return 0;
    }
    
    // Process direct plist files
    if (args.have("--plist")) {
        std::string plistPath = args.value("--plist").buf();
        
        // Add a check for the --just-coords flag first
        if (args.have("--just-coords")) {
            justPrintVoxelCoordinates(plistPath);
            return 0;
        }
        
        // Only print this message for other options
        printf("Reading directly from plist file: %s\n", plistPath.c_str());
        
        // Add a new simple print option for voxel coordinates
        if (args.have("--print-coords")) {
            printSimpleVoxelCoordinates(plistPath);
            return 0;
        }
        
        // Add a check for the new --print-voxels flag
        if (args.have("--print-voxels")) {
            printAllVisibleVoxels(plistPath);
            return 0;
        }
        
        if (!parsePlistVoxelData(plistPath, verbose)) {
            return 1;
        } 
        return 0;
    }

    // If no input file was specified, print error and exit
    std::cout << "No input file specified. Use -pl for plist input or -lz for compressed LZFSE input." << std::endl;
    return 1;
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

// Decode a Morton encoded chunk ID into x, y, z coordinates
std::tuple<int, int, int> decodeMortonChunkID(int64_t mortonID) {
    // Extract the xyz coordinates from the Morton encoded chunk ID
    // Implementation based on VoxelMax's Swift decoding
    int x = 0, y = 0, z = 0;
    
    // Extract each dimension by filtering bits at positions 0, 3, 6, etc. for x
    // positions 1, 4, 7, etc. for y, and positions 2, 5, 8, etc. for z
    for (int i = 0; i < 24; i++) {  // 24 bits (8 per dimension for chunks)
        int bit = (mortonID >> i) & 1;
        
        if (i % 3 == 0) {
            x |= (bit << (i / 3));
        } else if (i % 3 == 1) {
            y |= (bit << (i / 3));
        } else { // i % 3 == 2
            z |= (bit << (i / 3));
        }
    }
    
    return {x, y, z};
}

// Function to print just the coordinates of visible voxels
void printSimpleVoxelCoordinates(const std::string& filePath) {
    boost::any plistData;
    try {
        Plist::readPlist(filePath.c_str(), plistData);
    } catch (std::exception& e) {
        std::cerr << "Error reading plist file: " << e.what() << std::endl;
        return;
    }

    // Get root dictionary
    Plist::dictionary_type rootDict = boost::any_cast<Plist::dictionary_type>(plistData);
    if (rootDict.find("snapshots") == rootDict.end()) {
        std::cerr << "Error: No 'snapshots' key found in plist" << std::endl;
        return;
    }

    // Get the snapshots array
    Plist::array_type snapshotsArray = boost::any_cast<Plist::array_type>(rootDict.at("snapshots"));
    
    // Map to store final voxel state: key is chunkID, value is a map of voxel positions to colors
    std::map<int64_t, std::map<std::tuple<int, int, int>, uint8_t>> finalVoxelState;
    
    std::cout << "Processing " << snapshotsArray.size() << " snapshots..." << std::endl;
    
    // Process all snapshots in order to build the final voxel state
    for (size_t snapshotIndex = 0; snapshotIndex < snapshotsArray.size(); snapshotIndex++) {
        try {
            Plist::dictionary_type snapshot = boost::any_cast<Plist::dictionary_type>(snapshotsArray[snapshotIndex]);
            
            // Skip if no 's' key
            if (snapshot.find("s") == snapshot.end()) continue;
            
            Plist::dictionary_type snapshotData = boost::any_cast<Plist::dictionary_type>(snapshot.at("s"));
            
            // Skip if no 'id' dictionary
            if (snapshotData.find("id") == snapshotData.end()) continue;
            
            Plist::dictionary_type idDict = boost::any_cast<Plist::dictionary_type>(snapshotData.at("id"));
            
            // Skip if no chunk ID
            if (idDict.find("c") == idDict.end()) continue;
            
            // Get chunk ID
            int64_t chunkID = boost::any_cast<int64_t>(idDict.at("c"));
            
            // Skip if no voxel data
            if (snapshotData.find("ds") == snapshotData.end()) continue;
            
            // Get world origin coordinates for this chunk
            auto [chunkX, chunkY, chunkZ] = decodeMortonChunkID(chunkID);
            
            // Scale by chunk size (32)
            int worldOriginX = chunkX * 32;
            int worldOriginY = chunkY * 32;
            int worldOriginZ = chunkZ * 32;
            
            // Get voxel data
            Plist::data_type voxelData = boost::any_cast<Plist::data_type>(snapshotData.at("ds"));
            
            // Process voxel data using hybrid encoding
            int localX = 0, localY = 0, localZ = 0; // Current position counter within chunk
            
            // Process each voxel pair (position, color)
            for (size_t i = 0; i < voxelData.size(); i += 2) {
                if (i + 1 < voxelData.size()) {
                    uint8_t position = static_cast<uint8_t>(voxelData[i]);
                    uint8_t color = static_cast<uint8_t>(voxelData[i + 1]);
                    
                    if (position != 0) {
                        // Non-zero position: Jump to Morton-encoded position
                        uint8_t mortonX = 0, mortonY = 0, mortonZ = 0;
                        decodeMorton(position, mortonX, mortonY, mortonZ);
                        localX = mortonX;
                        localY = mortonY;
                        localZ = mortonZ;
                    }
                    
                    // Calculate world coordinates
                    int worldX = worldOriginX + localX;
                    int worldY = worldOriginY + localY;
                    int worldZ = worldOriginZ + localZ;
                    
                    // Update voxel state for this position
                    finalVoxelState[chunkID][std::make_tuple(worldX, worldY, worldZ)] = color;
                    
                    // Increment position counter for next voxel (only if current position is 0)
                    if (position == 0) {
                        // Increment in x-first, then y, then z order (matches typical voxel traversal)
                        localX++;
                        if (localX >= 32) {
                            localX = 0;
                            localY++;
                            if (localY >= 32) {
                                localY = 0;
                                localZ++;
                                // If we've gone beyond the chunk bounds, wrap around
                                if (localZ >= 32) {
                                    localZ = 0;
                                }
                            }
                        }
                    }
                }
            }
        } catch (std::exception& e) {
            std::cerr << "Error processing snapshot " << snapshotIndex << ": " << e.what() << std::endl;
        }
    }
    
    // Count and print visible voxels
    std::vector<std::tuple<int, int, int, uint8_t>> visibleVoxels;
    
    for (const auto& [chunkID, voxelMap] : finalVoxelState) {
        for (const auto& [pos, color] : voxelMap) {
            if (color != 0) {  // Only include visible voxels
                auto [worldX, worldY, worldZ] = pos;
                visibleVoxels.emplace_back(worldX, worldY, worldZ, color);
            }
        }
    }
    
    // Sort voxels by coordinates
    std::sort(visibleVoxels.begin(), visibleVoxels.end(), 
        [](const auto& a, const auto& b) {
            const auto& [aX, aY, aZ, aColor] = a;
            const auto& [bX, bY, bZ, bColor] = b;
            
            if (aX != bX) return aX < bX;
            if (aY != bY) return aY < bY;
            return aZ < bZ;
        });
    
    // Print the results
    std::cout << "Total visible voxels: " << visibleVoxels.size() << std::endl;
    std::cout << "VOXEL COORDINATES (x,y,z,color):" << std::endl;
    
    for (const auto& [x, y, z, color] : visibleVoxels) {
        std::cout << x << "," << y << "," << z << "," << (int)color << std::endl;
    }

    // Confirm that printing is complete
    std::cout << "End of voxel coordinate listing" << std::endl;
}
