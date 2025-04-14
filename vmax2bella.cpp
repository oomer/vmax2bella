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

#include <iostream> // For input/output operations (cout, cin, etc.)

// Bella SDK includes - external libraries for 3D rendering
#include "../bella_scene_sdk/src/bella_sdk/bella_scene.h" // For creating and manipulating 3D scenes in Bella
#include "../bella_scene_sdk/src/dl_core/dl_main.inl" // Core functionality from the Diffuse Logic engine

#ifdef _WIN32
#include <windows.h> // For ShellExecuteW
#include <shellapi.h> // For ShellExecuteW
#include <codecvt> // For wstring_convert
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h> // For fork, exec
#include <sys/wait.h> // For waitpid
#endif



#include "../oom/oom_bella_long.h"   
#include "../oom/oom_bella_scene.h"   
#include "../oom/oom_misc.h"         // common misc code
#include "../oom/oom_license.h"         // common misc code
#include "../oom/oom_voxel_vmax.h"   // common vmax voxel code and structures
#include "../oom/oom_voxel_ogt.h"    // common opengametools voxel conversion wrappers

#define OGT_VOX_IMPLEMENTATION
#include "../opengametools/src/ogt_vox.h"

// oomer helper functions from ../oom
dl::bella_sdk::Node oom::bella::essentialsToScene(dl::bella_sdk::Scene& belScene);
dl::bella_sdk::Node add_ogt_mesh_to_scene(dl::String bellaName, ogt_mesh* ogtMesh, dl::bella_sdk::Scene& belScene, dl::bella_sdk::Node& belWorld );

// Forward declaration
dl::bella_sdk::Node addModelToScene(dl::Args& args, dl::bella_sdk::Scene& belScene, dl::bella_sdk::Node& belWorld, const oom::vmax::VmaxModel& vmaxModel, const std::vector<oom::vmax::VmaxRGBA>& vmaxPalette, const std::array<oom::vmax::VmaxMaterial, 8>& vmaxMaterial); 

int DL_main(dl::Args& args) {
    args.add("i", "input", "", "vmax directory or vmax.zip file");
    args.add("mo", "mode", "", "mode for output, mesh, voxel, or both");
    args.add("mt", "meshtype", "", "meshtype classic, greedy, other");
    args.add("be", "bevel", "", "add bevel to material");
    args.add("tp",  "thirdparty",   "",   "prints third party licenses");
    args.add("li",  "licenseinfo",   "",   "prints license info");

    // If --help was requested, print help and exit
    if (args.helpRequested()) {
        std::cout << args.help("vmax2bella © 2025 Harvey Fong","vmax2bella", "1.0") << std::endl;
        return 0;
    }
    
    // If --licenseinfo was requested, print license info and exit
    if (args.have("--licenseinfo"))
    {
        std::cout << oom::license::printLicense() << std::endl;
        return 0;
    }
 
    // If --thirdparty was requested, print third-party licenses and exit
    if (args.have("--thirdparty"))
    {
        std::cout << oom::license::printBellaSDK() << "\n====\n" << std::endl;
        std::cout << oom::license::printLZFSE() << "\n====\n" << std::endl;
        std::cout << oom::license::printLibPlist() << "\n====\n" << std::endl;
        std::cout << oom::license::printOpenGameTools() << "\n====\n" << std::endl;
        return 0;
    }

    if (args.have("--input"))
    {
        dl::String bszName;
        dl::String objName;
        dl::String vmaxDirName;
        vmaxDirName = args.value("--input");
        bszName = vmaxDirName.replace("vmax", "bsz");
        objName = vmaxDirName.replace("vmax", "obj");

        // Create a new scene
        dl::bella_sdk::Scene belScene;
        belScene.loadDefs();
        auto belWorld = belScene.world(true);

        // scene.json is the toplevel file that hierarchically defines the scene
        // it contains nestable groups (containers) and objects (instances) that point to resources that define the object
        // objects properties
        //  - transformation matrix
        // objects resources
        /// - reference a contentsN.vmaxb (lzfse compressed plist file) that contains a 256x256x256 voxel "model"
        //  - reference to a paletteN.png that defines the 256 24bit colors used in the 256x256x256 model
        //  - reference to a paletteN.settings.vmaxpsb (plist file) that defines the 8 materials used in the "model"
        // In scenegraph parlance a group is a xform, a object is a transform with a child geometry 
        // multiple objects can point to the same model creating what is known as an instance
        oom::vmax::JsonVmaxSceneParser vmaxSceneParser;
        vmaxSceneParser.parseScene((vmaxDirName+"/scene.json").buf());

        #ifdef _DEBUG
            vmaxSceneParser.printSummary();
        #endif
        std::map<std::string, oom::vmax::JsonGroupInfo> jsonGroups = vmaxSceneParser.getGroups();
        std::map<dl::String, dl::bella_sdk::Node> belGroupNodes; // Map of UUID to bella node
        std::map<dl::String, dl::bella_sdk::Node> belCanonicalNodes; // Map of UUID to bella node

        // First pass to create all the Bella nodes for the groups
        for (const auto& [groupName, groupInfo] : jsonGroups) { 
            dl::String belGroupUUID = dl::String(groupName.c_str());
            belGroupUUID = belGroupUUID.replace("-", "_"); // Make sure the group name is valid for a Bella node name
            belGroupUUID = "_" + belGroupUUID; // Make sure the group name is valid for a Bella node name
            belGroupNodes[belGroupUUID] = belScene.createNode("xform", belGroupUUID, belGroupUUID); // Create a Bella node for the group


            oom::vmax::VmaxMatrix4x4 objectMat4 = oom::vmax::combineVmaxTransforms(groupInfo.rotation[0], 
                                              groupInfo.rotation[1], 
                                              groupInfo.rotation[2], 
                                              groupInfo.rotation[3],
                                              groupInfo.position[0], 
                                              groupInfo.position[1], 
                                              groupInfo.position[2], 
                                              groupInfo.scale[0], 
                                              groupInfo.scale[1], 
                                              groupInfo.scale[2]);

            belGroupNodes[belGroupUUID]["steps"][0]["xform"] = dl::Mat4({
                objectMat4.m[0][0], objectMat4.m[0][1], objectMat4.m[0][2], objectMat4.m[0][3],
                objectMat4.m[1][0], objectMat4.m[1][1], objectMat4.m[1][2], objectMat4.m[1][3],
                objectMat4.m[2][0], objectMat4.m[2][1], objectMat4.m[2][2], objectMat4.m[2][3],
                objectMat4.m[3][0], objectMat4.m[3][1], objectMat4.m[3][2], objectMat4.m[3][3]
                });
        }

        // json file is allowed the parent to be defined after the child, requiring us to create all the bella nodes before we can parent them
        for (const auto& [groupName, groupInfo] : jsonGroups) { 
            dl::String belGroupUUID = dl::String(groupName.c_str());
            belGroupUUID = belGroupUUID.replace("-", "_");
            belGroupUUID = "_" + belGroupUUID;
            if (groupInfo.parentId == "") {
                belGroupNodes[belGroupUUID].parentTo(belWorld); // Group without a parent is a child of the world
            } else {
                dl::String belPPPGroupUUID = dl::String(groupInfo.parentId.c_str());
                belPPPGroupUUID = belPPPGroupUUID.replace("-", "_");
                belPPPGroupUUID = "_" + belPPPGroupUUID;
                dl::bella_sdk::Node myParentGroup = belGroupNodes[belPPPGroupUUID]; // Get bella obj
                belGroupNodes[belGroupUUID].parentTo(myParentGroup); // Group underneath a group
            }
        }

        // Efficiently process unique models by examining only the first instance of each model type.
        // Example: If we have 100 instances of 3 different models:
        //   "model1.vmaxb": [instance1, instance2, ..., instance50],
        //   "model2.vmaxb": [instance1, ..., instance30],
        //   "model3.vmaxb": [instance1, ..., instance20]
        // This loop runs only 3 times (once per unique model), not 100 times (once per instance)
        
        auto modelVmaxbMap = vmaxSceneParser.getModelContentVMaxbMap(); 
        std::vector<oom::vmax::VmaxModel> allModels;
        std::vector<std::vector<oom::vmax::VmaxRGBA>> vmaxPalettes; // one palette per model
        std::vector<std::array<oom::vmax::VmaxMaterial, 8>> vmaxMaterials; // one material per model

        oom::bella::essentialsToScene(belScene); // create the basic scene elements in Bella
        
        // Loop over each model defined in scene.json and process the first instance 
        // This will be out canonical models, not instances
        // todo rename model to objects as per vmax
        for (const auto& [vmaxContentName, vmaxModelList] : modelVmaxbMap) { 
            std::cout << "vmaxContentName: " << vmaxContentName << std::endl;
            oom::vmax::VmaxModel currentVmaxModel(vmaxContentName);
            const auto& jsonModelInfo = vmaxModelList.front(); // get the first model, others are instances at the scene level
            std::vector<double> position = jsonModelInfo.position;
            std::vector<double> rotation = jsonModelInfo.rotation;
            std::vector<double> scale = jsonModelInfo.scale;
            std::vector<double> extentCenter = jsonModelInfo.extentCenter;

            // Get file names
            dl::String materialName = vmaxDirName + "/" + jsonModelInfo.paletteFile.c_str();
            materialName = materialName.replace(".png", ".settings.vmaxpsb");

            // Get this models colors from the paletteN.png 
            dl::String pngName = vmaxDirName + "/" + jsonModelInfo.paletteFile.c_str();
            vmaxPalettes.push_back(oom::vmax::read256x1PaletteFromPNG(pngName.buf())); // gather all models palettes
            //allPalettes.push_back(read256x1PaletteFromPNG(pngName.buf())); // gather all models palettes
            if (vmaxPalettes.empty()) { throw std::runtime_error("Failed to read palette from: png " ); }

            // Read contentsN.vmaxb plist file, lzfse compressed
            dl::String modelFileName = vmaxDirName + "/" + jsonModelInfo.dataFile.c_str();
            plist_t plist_model_root = oom::vmax::readPlist(modelFileName.buf(), true); // decompress=true

            plist_t plist_snapshots_array = plist_dict_get_item(plist_model_root, "snapshots");
            uint32_t snapshots_array_size = plist_array_get_size(plist_snapshots_array);
            //std::cout << "snapshots_array_size: " << snapshots_array_size << std::endl;

            // Create a VmaxModel object
            //VmaxModel currentVmaxModel(vmaxContentName);
            for (uint32_t i = 0; i < snapshots_array_size; i++) {
                plist_t plist_snapshot = plist_array_get_item(plist_snapshots_array, i);
                plist_t plist_chunk = oom::vmax::getNestedPlistNode(plist_snapshot, {"s", "id", "c"});
                plist_t plist_datastream = oom::vmax::getNestedPlistNode(plist_snapshot, {"s", "ds"});
                uint64_t chunkID;
                plist_get_uint_val(plist_chunk, &chunkID);
                oom::vmax::VmaxChunkInfo chunkInfo = oom::vmax::vmaxChunkInfo(plist_snapshot);
                //std::cout << "\nChunkID: " << chunkInfo.id << std::endl;
                //std::cout << "TypeID: " << chunkInfo.type << std::endl;
                //std::cout << "MortonCode: " << chunkInfo.mortoncode << "\n" <<std::endl;

                std::vector<oom::vmax::VmaxVoxel> xvoxels = oom::vmax::vmaxVoxelInfo(plist_datastream, chunkInfo.id, chunkInfo.mortoncode);
                //std::cout << "xxxvoxels: " << xvoxels.size() << std::endl;

                for (const auto& voxel : xvoxels) {
                    currentVmaxModel.addVoxel(voxel.x, voxel.y, voxel.z, voxel.material, voxel.palette ,chunkInfo.id, chunkInfo.mortoncode);
                }
            }
            allModels.push_back(currentVmaxModel);
            // Parse the materials store in paletteN.settings.vmaxpsb    
            plist_t plist_material = oom::vmax::readPlist(materialName.buf(),false); // decompress=false
            std::array<oom::vmax::VmaxMaterial, 8> currentMaterials = oom::vmax::getVmaxMaterials(plist_material);
            vmaxMaterials.push_back(currentMaterials);
        }
        //}
        int modelIndex=0;
        // Need to access voxles by material and color groupings
        // Models are canonical models, not instances
        // Vmax objects are instances of models

        // First create canonical models and they are NOT attached to belWorld
        for (const auto& eachModel : allModels) {
            //if (modelIndex == 0) { // only process the first model
            std::cout << modelIndex << "Model: " << eachModel.vmaxbFileName << std::endl;
            std::cout << "Voxel Count Model: " << eachModel.getTotalVoxelCount() << std::endl;
            
            dl::bella_sdk::Node belModel = addModelToScene( args,
                                                            belScene, 
                                                            belWorld, 
                                                            eachModel, 
                                                            vmaxPalettes[modelIndex], 
                                                            vmaxMaterials[modelIndex]);
            // TODO add to a map00000 of canonical models
            dl::String lllmodelName = dl::String(eachModel.vmaxbFileName.c_str());
            dl::String lllcanonicalName = lllmodelName.replace(".vmaxb", "");
            belCanonicalNodes[lllcanonicalName.buf()] = belModel;
            modelIndex++;
        }

        // Second Loop through each vmax object and create an instance of the canonical model
        // This is the instances of the models, we did a pass to create the canonical models earlier
        for (const auto& [vmaxContentName, vmaxModelList] : modelVmaxbMap) { 
            //std::cout << "model: " << vmaxContentName << std::endl;
            oom::vmax::VmaxModel currentVmaxModel(vmaxContentName);
            for(const auto& jsonModelInfo : vmaxModelList) {
                std::vector<double> position = jsonModelInfo.position;
                std::vector<double> rotation = jsonModelInfo.rotation;
                std::vector<double> scale = jsonModelInfo.scale;
                std::vector<double> extentCenter = jsonModelInfo.extentCenter;
                auto jsonParentId = jsonModelInfo.parentId;
                auto belParentId = dl::String(jsonParentId.c_str());
                dl::String belParentGroupUUID = belParentId.replace("-", "_"); // Make sure the group name is valid for a Bella node name
                belParentGroupUUID = "_" + belParentGroupUUID; // Make sure the group name is valid for a Bella node name

                auto belObjectId = dl::String(jsonModelInfo.id.c_str());
                belObjectId = belObjectId.replace("-", "_"); // Make sure the object name is valid for a Bella node name
                belObjectId = "_" + belObjectId; // Make sure the object name is valid for a Bella node name

                dl::String getCanonicalName = dl::String(jsonModelInfo.dataFile.c_str());
                dl::String canonicalName = getCanonicalName.replace(".vmaxb", "");
                //get bel node from canonical name
                auto belCanonicalNode = belCanonicalNodes[canonicalName.buf()];
                auto foofoo = belScene.findNode(canonicalName);

                oom::vmax::VmaxMatrix4x4 objectMat4 = oom::vmax::combineVmaxTransforms(rotation[0], 
                                                                 rotation[1], 
                                                                 rotation[2], 
                                                                 rotation[3],
                                                                 position[0], 
                                                                 position[1], 
                                                                 position[2], 
                                                                 scale[0], 
                                                                 scale[1], 
                                                                 scale[2]);

                auto belNodeObjectInstance = belScene.createNode("xform", belObjectId, belObjectId);
                belNodeObjectInstance["steps"][0]["xform"] = dl::Mat4({
                    objectMat4.m[0][0], objectMat4.m[0][1], objectMat4.m[0][2], objectMat4.m[0][3],
                    objectMat4.m[1][0], objectMat4.m[1][1], objectMat4.m[1][2], objectMat4.m[1][3],
                    objectMat4.m[2][0], objectMat4.m[2][1], objectMat4.m[2][2], objectMat4.m[2][3],
                    objectMat4.m[3][0], objectMat4.m[3][1], objectMat4.m[3][2], objectMat4.m[3][3]
                    });

                if (jsonParentId == "") {
                    belNodeObjectInstance.parentTo(belScene.world());
                } else {
                    dl::bella_sdk::Node myParentGroup = belGroupNodes[belParentGroupUUID]; // Get bella obj
                    belNodeObjectInstance.parentTo(myParentGroup); // Group underneath a group
                }
                foofoo.parentTo(belNodeObjectInstance);
            }
        }

        // Write Bella File .bsz=compressed .bsa=ascii .bsx=binary
        belScene.write(bszName.buf());
    }
    return 0;
}

// Only add the canonical model to the scene
// We'll use xforms to instance the model
// Each model is stores in contentsN.vmaxb as a lzfe compressed plist
// Each model has a paletteN.png that maps 0-255 to colors
// The model stored in contentsN.vmaxb can have mulitple snapshots
// Each snapshot contains a chunkID, and a datastream
// The datastream contains the voxels for the snapshot
// The voxels are stored in chunks, each chunk is 8x8x8 voxels
// The chunks are stored in a morton order
dl::bella_sdk::Node addModelToScene(dl::Args& args,
                                    dl::bella_sdk::Scene& belScene, 
                                    dl::bella_sdk::Node& belWorld, 
                                    const oom::vmax::VmaxModel& vmaxModel, 
                                    const std::vector<oom::vmax::VmaxRGBA>& vmaxPalette, 
                                    const std::array<oom::vmax::VmaxMaterial, 8>& vmaxMaterial) {
    // Create Bella scene nodes for each voxel
    int i = 0;
    dl::String modelName = dl::String(vmaxModel.vmaxbFileName.c_str());
    dl::String canonicalName = modelName.replace(".vmaxb", "");
    dl::bella_sdk::Node belCanonicalNode;
    {
        dl::bella_sdk::Scene::EventScope es(belScene);

        auto belVoxel = belScene.findNode("oomerVoxel");
        auto belLiqVoxel = belScene.findNode("oomerLiqVoxel");
        auto belMeshVoxel = belScene.findNode("oomerMeshVoxel");
        auto belVoxelForm = belScene.findNode("oomerVoxelXform");
        auto belLiqVoxelForm = belScene.findNode("oomerLiqVoxelXform");
        auto belBevel = belScene.findNode("oomerBevel");

        auto modelXform = belScene.createNode("xform", canonicalName, canonicalName);
        modelXform["steps"][0]["xform"] = dl::Mat4 {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for (const auto& [material, colorID] : vmaxModel.getUsedMaterialsAndColors()) {
            for (int color : colorID) {

                auto thisname = canonicalName + dl::String("Material") + dl::String(material) + dl::String("Color") + dl::String(color);

                auto belMaterial  = belScene.createNode("quickMaterial",
                                canonicalName + dl::String("vmaxMat") + dl::String(material) + dl::String("Color") + dl::String(color));
                bool isMesh = false;
                bool isBox = true;
                std::cout << vmaxMaterial[material].roughness << std::endl;

                if(material==7) {
                    belMaterial["type"] = "liquid";
                    //belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                    belMaterial["liquidDepth"] = 300.0f;
                    belMaterial["liquidIor"] = 1.33f;
                    isMesh = true;
                    isBox = false;
                } else if(material==6 || vmaxPalette[color-1].a < 255) {
                    belMaterial["type"] = "glass";
                    belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                    belMaterial["glassDepth"] = 500.0f;
                } else if(vmaxMaterial[material].metalness > 0.1f) {
                    belMaterial["type"] = "metal";
                    belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                } else if(vmaxMaterial[material].transmission > 0.0f) {
                    belMaterial["type"] = "dielectric";
                    belMaterial["transmission"] = vmaxMaterial[material].transmission;
                } else if(vmaxMaterial[material].emission > 0.0f) {
                    belMaterial["type"] = "emitter";
                    belMaterial["emitterUnit"] = "radiance";
                    belMaterial["energy"] = vmaxMaterial[material].emission*1.0f;
                } else if(vmaxMaterial[material].roughness > 0.8999f) {
                    belMaterial["type"] = "diffuse";
                } else {
                    belMaterial["type"] = "plastic";
                    belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                }

                if (args.have("bevel") && material != 7) {
                    belMaterial["bevel"] = belBevel;
                }
                if (args.have("mode") && args.value("mode") == "mesh" || args.value("mode") == "both") {
                    isMesh = true;
                    isBox = false;
                }

                // Convert 0-255 to 0-1 , remember to -1 color index becuase voxelmax needs 0 to indicate no voxel
                double bellaR = static_cast<double>(vmaxPalette[color-1].r)/255.0;
                double bellaG = static_cast<double>(vmaxPalette[color-1].g)/255.0;
                double bellaB = static_cast<double>(vmaxPalette[color-1].b)/255.0;
                double bellaA = static_cast<double>(vmaxPalette[color-1].a)/255.0;
                belMaterial["color"] = dl::Rgba{ // convert sRGB to linear
                    oom::misc::srgbToLinear(bellaR), 
                    oom::misc::srgbToLinear(bellaG), 
                    oom::misc::srgbToLinear(bellaB), 
                    bellaA // alpha is already linear
                }; // colors ready to use in Bella

                // Get all voxels for this material/color combination
                const std::vector<oom::vmax::VmaxVoxel>& voxelsOfType = vmaxModel.getVoxels(material, color);
                int showchunk =0;

                if (isMesh) {
                    auto belMeshXform  = belScene.createNode("xform",
                        thisname+dl::String("Xform"));
                    belMeshXform.parentTo(modelXform);

                    std::cout << "Converting voxels to mesh\n";
                    // Convert voxels of a particular color to ogt_vox_model
                    ogt_vox_model* ogt_model = oom::ogt::convert_voxelsoftype_to_ogt_vox(voxelsOfType);
                    ogt_mesh_rgba* palette = new ogt_mesh_rgba[256]; // Create a palette array
                    for (int i = 0; i < 256; i++) { // Copy palette from Vmax to OGT
                        palette[i] = ogt_mesh_rgba{vmaxPalette[i].r, vmaxPalette[i].g, vmaxPalette[i].b, vmaxPalette[i].a};
                    }
                    ogt_voxel_meshify_context ctx = {}; // Create a context struct, not a pointer, todo , what is this for

                    // Convert ogt voxels to mesh
                    ogt_mesh* mesh = ogt_mesh_from_paletted_voxels_simple(  &ctx,
                                                                            ogt_model->voxel_data, 
                                                                            ogt_model->size_x, 
                                                                            ogt_model->size_y, 
                                                                            ogt_model->size_z, 
                                                                            palette ); 
                        
                    if (voxelsOfType.size() > 0) {
                        auto belMesh = add_ogt_mesh_to_scene(   thisname,
                                                                mesh,
                                                                belScene,
                                                                belWorld
                                                            );
                        belMesh.parentTo(belMeshXform);
                        belMeshXform["material"] = belMaterial;
                    } else { 
                        std::cout << "skipping" << color << "\n";
                    }
                }
                if (isBox) {
                    auto belInstancer  = belScene.createNode("instancer",
                        thisname);
                    auto xformsArray = dl::ds::Vector<dl::Mat4f>();
                    belInstancer["steps"][0]["xform"] = dl::Mat4 {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                    belInstancer.parentTo(modelXform);

                    std::cout << "Converting voxels to bella boxes\n";
                    //WARNING we use to do morton decoding above but now VoxModel does it when addVoxel is called
                    // So we can just use the x,y,z values
                    for (const auto& eachvoxel : voxelsOfType) {
                        xformsArray.push_back( dl::Mat4f{  1, 0, 0, 0, 
                                                        0, 1, 0, 0, 
                                                        0, 0, 1, 0, 
                                                        (static_cast<float>(eachvoxel.x))+0.5f,  // offset center of voxel to match mesh
                                                        (static_cast<float>(eachvoxel.y))+0.5f,
                                                        (static_cast<float>(eachvoxel.z))+0.5f, 1 });
                    };
                    belInstancer["steps"][0]["instances"] = xformsArray;
                    belInstancer["material"] = belMaterial;
                    if(material==7) {
                        belLiqVoxel.parentTo(belInstancer);
                    } else {
                        belMeshVoxel.parentTo(belInstancer);
                    }
                    if(vmaxMaterial[material].emission > 0.0f) {
                        belVoxelForm.parentTo(belInstancer);
                    }
                }
            }
        }
        return modelXform;
    }
    return dl::bella_sdk::Node();
}

dl::bella_sdk::Node add_ogt_mesh_to_scene(dl::String name, ogt_mesh* meshmesh, dl::bella_sdk::Scene& belScene, dl::bella_sdk::Node& belWorld ) {

    //auto ogtXform = belScene.createNode("xform", name+"ogtXform", name+"ogtXform");
    //ogtXform["steps"][0]["xform"] = dl::Mat4 {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    //ogtXform.parentTo(belWorld);
    auto ogtMesh = belScene.createNode("mesh", name+"ogtmesh", name+"ogtmesh");
    ogtMesh["normals"] = "flat";
    // Add vertices and faces to the mesh
    dl::ds::Vector<dl::Pos3f> verticesArray;
    for (uint32_t i = 0; i < meshmesh->vertex_count; i++) {
        const auto& vertex = meshmesh->vertices[i];
        uint32_t xx = static_cast<uint32_t>(vertex.pos.x);
        uint32_t yy = static_cast<uint32_t>(vertex.pos.y);
        uint32_t zz = static_cast<uint32_t>(vertex.pos.z);
        verticesArray.push_back(dl::Pos3f{ static_cast<float>(xx), 
                                            static_cast<float>(yy), 
                                            static_cast<float>(zz) });

    }

    ogtMesh["steps"][0]["points"] = verticesArray;


    dl::ds::Vector<dl::Vec4u> facesArray;
    for (size_t i = 0; i < meshmesh->index_count; i+=3) {
        facesArray.push_back(dl::Vec4u{ static_cast<unsigned int>(meshmesh->indices[i]), 
                                        static_cast<unsigned int>(meshmesh->indices[i+1]), 
                                        static_cast<unsigned int>(meshmesh->indices[i+2]), 
                                        static_cast<unsigned int>(meshmesh->indices[i+2]) });
    }
    ogtMesh["polygons"] = facesArray;

    return ogtMesh;
}