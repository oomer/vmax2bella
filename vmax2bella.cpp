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
        │   ├── lc (binary data) - Location table 
        │   ├── ds (binary data) - Voxel data stream
        │   ├── dlc (binary data) - Default layer colors 
        │   └── st (dictionary) - Statistics/metadata
        └── Other metadata fields:
            ├── cid (chunk id)
            ├── sid (edit session id)
            └── t (type - VXVolumeSnapshotType)
```

## Chunking System
### Volume Organization
- The total volume is divided into chunks for efficient storage and manipulation
- Standard chunk size: 8×8×8 voxels
- Total addressable space: 256×256×256 voxels (32×32×32 chunks)
### Chunk Addressing
- Each chunk has 3D coordinates (chunk_x, chunk_y, chunk_z)
- ie a 16×16×16 volume with 8×8×8 chunks, there would be 2×2×2 chunks total
- Chunks are only stored if they contain at least one non-empty voxel

## Data Fields
### Location Table (lc)
- Fixed-size binary array (256 bytes)
- Each byte represents a position in a 3D volume
- Non-zero values (typically 1) indicate occupied positions
- The index of each byte is interpreted as a Morton code for the position
- Size of 256 bytes limits addressable chunks to 256 
- ie a 16×16×16 volume would get 16÷8 = 2 chunks, Total chunks = 2×2×2 = 8 chunks total
### Voxel Data Stream (ds)
- Variable-length binary data
- Contains pairs of bytes for each voxel: [position_byte, color_byte]
- *Position Byte:*
    - Usually 0 (meaning position at origin of chunk)
    - Can encode a local position within a chunk using Morton encoding
- *Color Byte:*
    - Stores the color value + 1 (offset of +1 from actual color)
    - Value 0 would represent -1 (typically not used)

### Default Layer Colors (dlc)
- Optional 256-byte array
- May contain default color information for specific layers

### Statistics Data (st)
- Dictionary containing metadata like:
    - e (extent): Array defining the bounding box [min_x, min_y, min_z, max_x, max_y, max_z]
    - Other statistics fields may include count, max, min, etc.

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
    - chunk_x = floor(world_x / 8)
    - chunk_y = floor(world_y / 8)
    - chunk_z = floor(world_z / 8)
- *World to Local:*
    - local_x = world_x % 8
    - local_y = world_y % 8
    - local_z = world_z % 8
- *Chunk+Local to World:*
    - world_x = chunk_x * 8 + local_x
    - world_y = chunk_y * 8 + local_y
    - world_z = chunk_z * 8 + local_z
 
## Morton Encoding
### Morton Code (Z-order curve)
- A space-filling curve that interleaves the bits of the x, y, and z coordinates
- Used to convert 3D coordinates to a 1D index and vice versa
- Creates a coherent ordering of voxels that preserves spatial locality
### Encoding Process
1. Take the binary representation of x, y, and z coordinates
2. Interleave the bits in the order: z₀, y₀, x₀, z₁, y₁, x₁, z₂, y₂, x₂, ...
3. The resulting binary number is the Morton code
### For Chunk Indexing
- Morton code is used to map the 3D chunk coordinates to a 1D index in the lc array
Formula: index = interleave_bits(chunk_x, chunk_y, chunk_z)

### Detailed Example
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

Yes, an 8×8×8 chunk contains 512 voxels total, with Morton indices ranging from 0 to 511:
Morton index 0 corresponds to position (0,0,0)
Morton index 511 corresponds to position (7,7,7)
The Morton indices are organized by z-layer:
Z=0 layer: indices 0-63
Z=1 layer: indices 64-127
Z=2 layer: indices 128-191
Z=3 layer: indices 192-255
Z=4 layer: indices 256-319
Z=5 layer: indices 320-383
Z=6 layer: indices 384-447
Z=7 layer: indices 448-511
Each z-layer contains 64 positions (8×8), and with 8 layers, we get the full 512 positions for the chunk.

### For Local Positions
The position byte in the voxel data can contain a Morton-encoded local position
- Typically 0 (representing the origin of the chunk)
- When non-zero, it encodes a specific position within the chunk

## Color Representation
### Color Values
- Stored as a single byte (8-bit)
- Range: 0-255
- Important: Stored with an offset of +1 from actual color

### Color Interpretation
Format doesn't specify a specific color model (RGB, palette, etc.)
- Interpretation depends on the implementation
## Snapshots and Edit History
###Snapshots
- Each snapshot represents a single edit operation
- Multiple snapshots build up the complete voxel model over time
- Later snapshots override earlier ones for the same positions

### Snapshot Metadata
- cid (Chunk ID): Which chunk was modified
- sid (Session ID): Identifies the editing session
- t (Type): Type of snapshot (VXVolumeSnapshotType)

## Special Considerations
### Sparse Representation
- Only non-empty chunks and voxels are stored
- This creates an efficient representation for mostly empty volumes
### Position Encoding
- When all voxels in a chunk are at the origin (0,0,0), the position byte is always 0
- For more complex structures, the position byte encodes local positions within the chunk
### Color Offset
- The +1 offset for colors must be accounted for when reading and writing
- This might be to reserve 0 as a special value (e.g., for "no color")
### Field Sizes
- The location table (lc) is fixed at 256 bytes
- The voxel data stream (ds) varies based on the number of voxels
- For a simple 16×16×16 volume with 8×8×8 chunks, only 64 chunks can be addressed (2×2×2 chunks = 8 chunks total)
## Implementation Guidance
### Reading Algorithm
1. Parse the plist file to access the snapshot array
2. For each snapshot, extract the lc and ds data
3. Scan the lc data for non-zero entries to identify occupied positions
4. For each non-zero entry, decode the Morton index to get the chunk coordinates
5. Process the ds data in pairs of bytes (position, color)
6. For each voxel, calculate its absolute position and store with the corrected color (subtract 1)
7. Combine all snapshots to build the complete voxel model

### Writing Algorithm
1. Organize voxels by chunk
2. For each non-empty chunk:
3. Create a snapshot entry
4. Set up a 256-byte lc array (all zeros)
5. Set the appropriate byte in lc to 1 based on the chunk's Morton code
6. Create the ds data by encoding each voxel as a (position, color+1) pair
7. Add metadata as needed
8. Add all snapshots to the array
9. Write the complete structure to a plist file
10. Compress with LZFSE
12. [hand wabving] Package up in a MacOS style package directory with some other files
## Practical Limits
- 256-byte location table can address up to 256 distinct positions
- 8-bit color values allow 256 different colors (including the offset)
- For a 256×256×256 volume with 8×8×8 chunks, there would be 32×32×32 = 32,768 chunks total
- The format could efficiently represent volumes with millions of voxels
*/

// Standard C++ library includes - these provide essential functionality
#include <iostream>     // For input/output operations (cout, cin, etc.)
#include <fstream>      // For file operations (reading/writing files)
#include <vector>       // For dynamic arrays (vectors)
#include <cstdint>      // For fixed-size integer types (uint8_t, uint32_t, etc.)
#include <map>          // For key-value pair data structures (maps)
#include <filesystem>   // For file system operations (directory handling, path manipulation)

// Bella SDK includes - external libraries for 3D rendering
#include "bella_sdk/bella_scene.h"  // For creating and manipulating 3D scenes in Bella
#include "dl_core/dl_main.inl"      // Core functionality from the Diffuse Logic engine
#include "dl_core/dl_fs.h"
#include "lzfse.h"

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

// Main function for the program
// This is where execution begins
// The Args object contains command-line arguments
int DL_main(dl::Args& args)
{
    // Variable to store the input file path
    std::string filePath;
    std::string lzfseInputPath;
    std::string plistOutputPath;

    // Define command-line arguments that the program accepts
    args.add("vi",  "voxin", "",   "Input .vox file");
    args.add("tp",  "thirdparty",   "",   "prints third party licenses");
    args.add("li",  "licenseinfo",   "",   "prints license info");
    args.add("lz",  "lzfsein", "",   "Input LZFSE compressed file");
    args.add("po",  "plistout", "",   "Output plist file path");

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
        printf("%s", args.help("vox2bella", dl::fs::exePath(), "Hello\n").buf());
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
    // Get the input file path from command line arguments
    if (args.have("--voxin"))
    {
        filePath = args.value("--voxin").buf();
    } 
    /*else 
    {
        // If no input file was specified, print error and exit
        std::cout << "Mandatory -vi .vox input missing" << std::endl;
        return 1; 
    }*/

    // Check for LZFSE decompression request
    if ((args.have("--lzfsein")  && (args.have("--plistout") )))
    {
        lzfseInputPath = args.value("--lzfsein").buf();
        std::cout << "lzfseInputPath: " << lzfseInputPath << std::endl;
        plistOutputPath = args.value("--plistout").buf();
        std::cout << "plistOutputPath: " << plistOutputPath << std::endl;
        return decompressLzfseToPlist(lzfseInputPath, plistOutputPath) ? 0 : 1;
    }
    else 
    {
        std::cout << "Mandatory sing" << std::endl;
        return 1; 
    }

    // Create a new Bella scene
    bsdk::Scene sceneWrite;
    sceneWrite.loadDefs(); // Load scene definitions
    
    // Create a vector to store voxel color indices
    std::vector<uint8_t> voxelPalette;

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

    /* Commented out: Loop to create material nodes
    for( uint8_t i = 0; ;i++) 
    {
        String nodeName = String("voxMat") + String(i); // Create the node name
        auto dielectric = sceneWrite.createNode("dielectric",nodeName,nodeName);
        {
            Scene::EventScope es(sceneWrite);
            dielectric["ior"] = 1.51f;
            dielectric["roughness"] = 22.0f;
            dielectric["depth"] = 44.0f;
        }
        if(i==255)
        {
            break;
        }
    }*/

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

    // Process all chunks in the VOX file
    // Loop until we reach the end of the file
    /*while (file.peek() != EOF) {
        readChunk(file, palette, voxelPalette, sceneWrite, voxel);
    }*/ 

    // If the file didn't have a palette, create materials using the default palette
    /*if (!has_palette)
    {
        for(int i=0; i<256; i++)
        {
            // Extract RGBA components from the palette color
            // Bit shifting and masking extracts individual byte components
            uint8_t r = (palette[i] >> 0) & 0xFF;   // Red (lowest byte)
            uint8_t g = (palette[i] >> 8) & 0xFF;   // Green (second byte)
            uint8_t b = (palette[i] >> 16) & 0xFF;  // Blue (third byte)
            uint8_t a = (palette[i] >> 24) & 0xFF;  // Alpha (highest byte)
            
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
                voxMat["reflectance"] = dl::Rgba{ static_cast<double>(r)/255.0,
                                              static_cast<double>(g)/255.0,
                                              static_cast<double>(b)/255.0,
                                              static_cast<double>(a)/255.0};
            }
            
        }
    }*/

    // Assign materials to voxels based on their color indices
    /*for (int i = 0; i < voxelPalette.size(); i++) 
    {
        // Find the transform node for this voxel
        auto xformNode = sceneWrite.findNode(dl::String("voxXform") + dl::String(i));
        // Find the material node for this voxel's color
        auto matNode = sceneWrite.findNode(dl::String("voxMat") + dl::String(voxelPalette[i]));
        // Assign the material to the voxel
        xformNode["material"] = matNode;
    }

    // Close the input file
    file.close();*/

    // Create the output file path by replacing .vox with .bsz
    //std::filesystem::path bszPath = voxPath.stem().string() + ".bsz";
    // Write the Bella cene to the output file
    sceneWrite.write(dl::String("foo.bsz"));

    // Return success
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
