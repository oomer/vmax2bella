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

#include "oomer_voxel_vmax.h"   // common vmax voxel code and structures
#include "oomer_misc.h"         // common misc code

dl::bella_sdk::Node essentialsToScene(dl::bella_sdk::Scene& belScene);
dl::bella_sdk::Node addModelToScene(dl::bella_sdk::Scene& belScene, dl::bella_sdk::Node& belWorld, const VmaxModel& vmaxModel, const std::vector<VmaxRGBA>& vmaxPalette, const std::array<VmaxMaterial, 8>& vmaxMaterial); 

int DL_main(dl::Args& args) {
    args.add("i", "input", "", "vmax directory or vmax.zip file");
    args.add("o", "output", "", "set output bella file name");
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
        std::cout << initializeGlobalLicense() << std::endl;
        return 0;
    }
 
    // If --thirdparty was requested, print third-party licenses and exit
    if (args.have("--thirdparty"))
    {
        std::cout << initializeGlobalThirdPartyLicences() << std::endl;
        return 0;
    }

    if (args.have("--input"))
    {
        dl::String bszName;
        dl::String vmaxDirName;
        vmaxDirName = args.value("--input");
        bszName = vmaxDirName.replace("vmax", "bsz");

        // Create a new scene
        dl::bella_sdk::Scene belScene;
        belScene.loadDefs();
        auto belWorld = belScene.world(true);

        // Parse the scene.json file to get the models
        JsonVmaxSceneParser vmaxSceneParser;
        vmaxSceneParser.parseScene((vmaxDirName+"/scene.json").buf());
        //auto models = vmaxSceneParser.getModels();
        vmaxSceneParser.printSummary();
        std::map<std::string, JsonGroupInfo> jsonGroups = vmaxSceneParser.getGroups();
        std::map<dl::String, dl::bella_sdk::Node> belGroupNodes; // Map of UUID to bella node
        std::map<dl::String, dl::bella_sdk::Node> belCanonicalNodes; // Map of UUID to bella node

        // First pass to create all the Bella nodes for the groups
        for (const auto& [groupName, groupInfo] : jsonGroups) { 
            dl::String belGroupUUID = dl::String(groupName.c_str());
            belGroupUUID = belGroupUUID.replace("-", "_"); // Make sure the group name is valid for a Bella node name
            belGroupUUID = "_" + belGroupUUID; // Make sure the group name is valid for a Bella node name
            belGroupNodes[belGroupUUID] = belScene.createNode("xform", belGroupUUID, belGroupUUID); // Create a Bella node for the group

            // Rotate the object
            VmaxMatrix4x4 objectMat4 = axisAngleToMatrix4x4(  groupInfo.rotation[0], 
                                                                groupInfo.rotation[1], 
                                                                groupInfo.rotation[2], 
                                                                groupInfo.rotation[3]);

            // Translate the object
            VmaxMatrix4x4 objectTransMat4 = VmaxMatrix4x4();
            objectTransMat4 = objectTransMat4.createTranslation(groupInfo.position[0], 
                                                                groupInfo.position[1], 
                                                                groupInfo.position[2]);

            // Scale the object
            VmaxMatrix4x4 objectScaleMat4 = VmaxMatrix4x4();
            objectScaleMat4 = objectScaleMat4.createScale(groupInfo.scale[0], 
                                                          groupInfo.scale[1], 
                                                          groupInfo.scale[2]);
            objectMat4 = objectScaleMat4 * objectMat4 * objectTransMat4;

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
        std::vector<VmaxModel> allModels;
        std::vector<std::vector<VmaxRGBA>> vmaxPalettes; // one palette per model
        std::vector<std::array<VmaxMaterial, 8>> vmaxMaterials; // one material per model
        //std::vector<std::array<VmaxMaterial, 8>> allMaterials; // one material per model
        //std::vector<std::vector<VmaxRGBA>> allPalettes;

        essentialsToScene(belScene); // create the basic scene elements in Bella
        
        // Loop over each model defined in scene.json and process the first instance 
        // This will be out canonical models, not instances
        // todo rename model to objects as per vmax
        for (const auto& [vmaxContentName, vmaxModelList] : modelVmaxbMap) { 
            std::cout << "vmaxContentName: " << vmaxContentName << std::endl;
            VmaxModel currentVmaxModel(vmaxContentName);
            const auto& jsonModelInfo = vmaxModelList.front(); // get the first model, others are instances at the scene level
            std::vector<double> position = jsonModelInfo.position;
            std::vector<double> rotation = jsonModelInfo.rotation;
            std::vector<double> scale = jsonModelInfo.scale;
            std::vector<double> extentCenter = jsonModelInfo.extentCenter;

            // Rotate the object
            VmaxMatrix4x4 modelMatrix = axisAngleToMatrix4x4(rotation[0], rotation[1], rotation[2], rotation[3]);

            //  Translate the object
            VmaxMatrix4x4 transMatrix = VmaxMatrix4x4();
            transMatrix = transMatrix.createTranslation(position[0], 
                                                        position[1], 
                                                        position[2]);
            VmaxMatrix4x4 scaleMatrix = VmaxMatrix4x4();
            scaleMatrix = scaleMatrix.createScale(scale[0], 
                                                  scale[1], 
                                                  scale[2]);
            modelMatrix = scaleMatrix * modelMatrix * transMatrix;

            // Get file names
            dl::String materialName = vmaxDirName + "/" + jsonModelInfo.paletteFile.c_str();
            materialName = materialName.replace(".png", ".settings.vmaxpsb");

            // Get this models colors from the paletteN.png 
            dl::String pngName = vmaxDirName + "/" + jsonModelInfo.paletteFile.c_str();
            vmaxPalettes.push_back(read256x1PaletteFromPNG(pngName.buf())); // gather all models palettes
            //allPalettes.push_back(read256x1PaletteFromPNG(pngName.buf())); // gather all models palettes
            if (vmaxPalettes.empty()) { throw std::runtime_error("Failed to read palette from: png " ); }

            // Read contentsN.vmaxb plist file, lzfse compressed
            dl::String modelFileName = vmaxDirName + "/" + jsonModelInfo.dataFile.c_str();
            plist_t plist_model_root = readPlist(modelFileName.buf(), true); // decompress=true

            plist_t plist_snapshots_array = plist_dict_get_item(plist_model_root, "snapshots");
            uint32_t snapshots_array_size = plist_array_get_size(plist_snapshots_array);
            //std::cout << "snapshots_array_size: " << snapshots_array_size << std::endl;

            // Create a VmaxModel object
            //VmaxModel currentVmaxModel(vmaxContentName);
            for (uint32_t i = 0; i < snapshots_array_size; i++) {
                plist_t plist_snapshot = plist_array_get_item(plist_snapshots_array, i);
                plist_t plist_chunk = getNestedPlistNode(plist_snapshot, {"s", "id", "c"});
                plist_t plist_datastream = getNestedPlistNode(plist_snapshot, {"s", "ds"});
                uint64_t chunkID;
                plist_get_uint_val(plist_chunk, &chunkID);
                VmaxChunkInfo chunkInfo = vmaxChunkInfo(plist_snapshot);
                //std::cout << "\nChunkID: " << chunkInfo.id << std::endl;
                //std::cout << "TypeID: " << chunkInfo.type << std::endl;
                //std::cout << "MortonCode: " << chunkInfo.mortoncode << "\n" <<std::endl;


                std::vector<VmaxVoxel> xvoxels = vmaxVoxelInfo(plist_datastream, chunkInfo.id, chunkInfo.mortoncode);
                //std::cout << "xxxvoxels: " << xvoxels.size() << std::endl;

                for (const auto& voxel : xvoxels) {
                    currentVmaxModel.addVoxel(voxel.x, voxel.y, voxel.z, voxel.material, voxel.palette ,chunkInfo.id, chunkInfo.mortoncode);
                }
            }
            allModels.push_back(currentVmaxModel);
            // Parse the materials store in paletteN.settings.vmaxpsb    
            plist_t plist_material = readPlist(materialName.buf(),false); // decompress=false
            std::array<VmaxMaterial, 8> currentMaterials = getVmaxMaterials(plist_material);
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
            
            dl::bella_sdk::Node belModel = addModelToScene(belScene, belWorld, eachModel, vmaxPalettes[modelIndex], vmaxMaterials[modelIndex]);
            // TODO add to a map of canonical models
            dl::String lllmodelName = dl::String(eachModel.vmaxbFileName.c_str());
            dl::String lllcanonicalName = lllmodelName.replace(".vmaxb", "");
            std::cout << "========lllcanonicalName: " << lllcanonicalName.buf() << std::endl;
            belCanonicalNodes[lllcanonicalName.buf()] = belModel;
            modelIndex++;
        }

        // Second Loop through each vmax object and create an instance of the canonical model
        // This is the instances of the models, we did a pass to create the canonical models earlier
        for (const auto& [vmaxContentName, vmaxModelList] : modelVmaxbMap) { 
            //std::cout << "model: " << vmaxContentName << std::endl;
            VmaxModel currentVmaxModel(vmaxContentName);
            for(const auto& jsonModelInfo : vmaxModelList) {
                std::vector<double> position = jsonModelInfo.position;
                std::vector<double> rotation = jsonModelInfo.rotation;
                std::vector<double> scale = jsonModelInfo.scale;
                std::vector<double> extentCenter = jsonModelInfo.extentCenter;
                auto jsonParentId = jsonModelInfo.parentId;
                auto belParentId = dl::String(jsonParentId.c_str());
                dl::String belParentGroupUUID = belParentId.replace("-", "_");
                belParentGroupUUID = "_" + belParentGroupUUID;

                auto belObjectId = dl::String(jsonModelInfo.id.c_str());
                belObjectId = belObjectId.replace("-", "_");
                belObjectId = "_" + belObjectId;

                dl::String getCanonicalName = dl::String(jsonModelInfo.dataFile.c_str());
                dl::String canonicalName = getCanonicalName.replace(".vmaxb", "");
                //get bel node from canonical name
                auto belCanonicalNode = belCanonicalNodes[canonicalName.buf()];
                auto foofoo = belScene.findNode(canonicalName);

                VmaxMatrix4x4 objectMat4 = axisAngleToMatrix4x4(  rotation[0], 
                                                                    rotation[1], 
                                                                    rotation[2], 
                                                                    rotation[3]);
                VmaxMatrix4x4 objectTransMat4 = VmaxMatrix4x4();
                objectTransMat4 = objectTransMat4.createTranslation(position[0], 
                                                                    position[1], 
                                                                    position[2]);

                VmaxMatrix4x4 objectScaleMat4 = VmaxMatrix4x4();
                objectScaleMat4 = objectScaleMat4.createScale(scale[0], 
                                                              scale[1], 
                                                              scale[2]);

                objectMat4 = objectScaleMat4 * objectMat4 * objectTransMat4;

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
                    belNodeObjectInstance.parentTo(belGroupNodes[belParentGroupUUID]);
                }
                foofoo.parentTo(belNodeObjectInstance);
            }
        }

        // Write Bella File .bsz=compressed .bsa=ascii .bsx=binary
        belScene.write(bszName.buf());
    }
    return 0;
}



// @param belScene - the scene to create the essentials in
// @return - the world node
dl::bella_sdk::Node essentialsToScene(dl::bella_sdk::Scene& belScene) {
    // Create the basic scene elements in Bella
    // Each line creates a different type of node in the scene auto belBeautyPass     = belScene.createNode("beautyPass","oomerBeautyPass","oomerBeautyPass");
    auto belWorld = belScene.world();       // Get scene world root
    {
        dl::bella_sdk::Scene::EventScope es(belScene);

        auto belCamForm    = belScene.createNode("xform","oomerCameraXform","oomerCameraXform");
        auto belCam        = belScene.createNode("camera","oomerCamera","oomerCamera");
        auto belSensor         = belScene.createNode("sensor","oomerSensor","oomerSensor");
        auto belLens           = belScene.createNode("thinLens","oomerThinLens","oomerThinLens");
        auto belImageDome      = belScene.createNode("imageDome","oomerImageDome","oomerImageDome");
        auto belGroundPlane    = belScene.createNode("groundPlane","oomerGroundPlane","oomerGroundPlane");

        auto belBeautyPass     = belScene.createNode("beautyPass","oomerBeautyPass","oomerBeautyPass");
        auto belGroundMat      = belScene.createNode("quickMaterial","oomerGroundMat","oomerGroundMat");
        auto belSun            = belScene.createNode("sun","oomerSun","oomerSun");
        auto belColorDome   = belScene.createNode("colorDome","oomerColorDome","oomerColorDome");
        auto belSettings = belScene.settings(); // Get scene settings
        // Configure camera
        belCam["resolution"]    = dl::Vec2 {1920, 1080};  // Set resolution to 1080p
        belCam["lens"]          = belLens;               // Connect camera to lens
        belCam["sensor"]        = belSensor;             // Connect camera to sensor
        belCamForm.parentTo(belWorld);                  // Parent camera transform to world
        belCam.parentTo(belCamForm);                   // Parent camera to camera transform

        // Position the camera with a transformation matrix
        belCamForm["steps"][0]["xform"] = dl::Mat4 {0.525768608156, -0.850627633385, 0, 0, -0.234464751651, -0.144921468924, -0.961261695938, 0, 0.817675761479, 0.505401223947, -0.275637355817, 0, -88.12259018466, -54.468125200218, 50.706001690932, 1};

        // Configure environment (image-based lighting)
        belImageDome["ext"]            = ".jpg";
        belImageDome["dir"]            = "./res";
        belImageDome["multiplier"]     = 6.0f;
        belImageDome["file"]           = "DayEnvironmentHDRI019_1K-TONEMAPPED";
        belImageDome["overrides"]["background"]     = belColorDome;
        belColorDome["zenith"] = dl::Rgba{1.0f, 1.0f, 1.0f, 1.0f};
        belColorDome["horizon"] = dl::Rgba{.85f, 0.76f, 0.294f, 1.0f};
        belColorDome["altitude"] = 14.0f;
        // Configure ground plane
        belGroundPlane["elevation"]    = -.5f;
        belGroundPlane["material"]     = belGroundMat;

        /* Commented out: Sun configuration
        belSun["size"]    = 20.0f;
        belSun["month"]    = "july";
        belSun["rotation"]    = 50.0f;*/

        // Configure materials
        belGroundMat["type"] = "metal";
        belGroundMat["roughness"] = 22.0f;
        belGroundMat["color"] = dl::Rgba{0.138431623578, 0.5, 0.3, 1.0};

        // Set up scene settings
        belSettings["beautyPass"]  = belBeautyPass;
        belSettings["camera"]      = belCam;
        belSettings["environment"] = belColorDome;
        belSettings["iprScale"]    = 100.0f;
        belSettings["threads"]     = dl::bella_sdk::Input(0);  // Auto-detect thread count
        belSettings["groundPlane"] = belGroundPlane;
        belSettings["iprNavigation"] = "maya";  // Use Maya-like navigation in viewer
        //settings["sun"] = sun;

        auto belVoxel          = belScene.createNode("box","oomerVoxel","oomerVoxel");
        auto belLiqVoxel       = belScene.createNode("box","oomerLiqVoxel","oomerLiqVoxel");
        auto belVoxelForm      = belScene.createNode("xform","oomerVoxelXform","oomerVoxelXform");
        auto belLiqVoxelForm   = belScene.createNode("xform","oomerLiqVoxelXform","oomerLiqVoxelXform");
        auto belVoxelMat       = belScene.createNode("orenNayar","oomerVoxelMat","oomerVoxelMat");
        auto belMeshVoxel   = belScene.createNode("mesh", "oomerMeshVoxel");
        #include "resources/smoothcube.h"
       // Configure voxel box dimensions
        belVoxel["radius"]  = 0.33f;
        belVoxel["sizeX"]   = 0.99f;
        belVoxel["sizeY"]   = 0.99f;
        belVoxel["sizeZ"]   = 0.99f;

        // Less gap to make liquid look better, allows more light to pass through
        belLiqVoxel["sizeX"]    = 0.99945f;
        belLiqVoxel["sizeY"]    = 0.99945f;
        belLiqVoxel["sizeZ"]    = 0.99945f;

        belVoxel.parentTo(belVoxelForm);
        belVoxelForm["steps"][0]["xform"] = dl::Mat4 {0.999,0,0,0,0,0.999,0,0,0,0,0.999,0,0,0,0,1};
        belVoxelMat["reflectance"] = dl::Rgba{0.0, 0.0, 0.0, 1.0};
        belVoxelForm["material"] = belVoxelMat;


    }
    return belWorld;
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
dl::bella_sdk::Node addModelToScene(dl::bella_sdk::Scene& belScene, dl::bella_sdk::Node& belWorld, const VmaxModel& vmaxModel, const std::vector<VmaxRGBA>& vmaxPalette, const std::array<VmaxMaterial, 8>& vmaxMaterial) {
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

        auto modelXform = belScene.createNode("xform", canonicalName, canonicalName);
        modelXform["steps"][0]["xform"] = dl::Mat4 {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for (const auto& [material, colorID] : vmaxModel.getUsedMaterialsAndColors()) {
            for (int color : colorID) {
                auto belInstancer  = belScene.createNode("instancer",
                                canonicalName + dl::String("Material") + dl::String(material) + dl::String("Color") + dl::String(color));
                auto xformsArray = dl::ds::Vector<dl::Mat4f>();
                belInstancer["steps"][0]["xform"] = dl::Mat4 {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                belInstancer.parentTo(modelXform);

                auto belMaterial  = belScene.createNode("quickMaterial",
                                canonicalName + dl::String("vmaxMat") + dl::String(material) + dl::String("Color") + dl::String(color));


                if(material==7) {
                    belMaterial["type"] = "liquid";
                    //belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                    belMaterial["liquidDepth"] = 100.0f;
                    belMaterial["ior"] = 1.11f;
                } else if(material==6 || vmaxPalette[color-1].a < 255) {
                    belMaterial["type"] = "glass";
                    belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                    belMaterial["glassDepth"] = 200.0f;
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
                } else {
                    belMaterial["type"] = "plastic";
                    belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                }
                belInstancer["material"] = belMaterial;
                // Convert 0-255 to 0-1 , remember to -1 color index becuase voxelmax needs 0 to indicate no voxel
                double bellaR = static_cast<double>(vmaxPalette[color-1].r)/255.0;
                double bellaG = static_cast<double>(vmaxPalette[color-1].g)/255.0;
                double bellaB = static_cast<double>(vmaxPalette[color-1].b)/255.0;
                double bellaA = static_cast<double>(vmaxPalette[color-1].a)/255.0;
                belMaterial["color"] = dl::Rgba{ // convert sRGB to linear
                    srgbToLinear(bellaR), 
                    srgbToLinear(bellaG), 
                    srgbToLinear(bellaB), 
                    bellaA // alpha is already linear
                }; // colors ready to use in Bella

                // Get all voxels for this material/color combination
                const std::vector<VmaxVoxel>& voxelsOfType = vmaxModel.getVoxels(material, color);
                int showchunk =0;

                // Right now we group voxels by MatCol ie Mat0Col2
                // But voxels are stored in chunks with many colors 
                // Since we aren't grouping voxels in chunks, we need to traverse the voxels 
                // and offset each voxel by the morton decode of chunk index
                for (const auto& eachvoxel : voxelsOfType) {
                    // Get chunk coordinates and world origin
                    uint32_t _tempx, _tempy, _tempz;
                    decodeMorton3DOptimized(eachvoxel.chunkID, _tempx, _tempy, _tempz); // index IS the morton code
                    int worldOffsetX = _tempx * 24; // get world loc within 256x256x256 grid
                    int worldOffsetY = _tempy * 24; // Don't know why we need to multiply by 24
                    int worldOffsetZ = _tempz * 24; // use to be 32
                    xformsArray.push_back( dl::Mat4f{  1, 0, 0, 0, 
                                                        0, 1, 0, 0, 
                                                        0, 0, 1, 0, 
                                                        static_cast<float>(eachvoxel.x + worldOffsetX),
                                                        static_cast<float>(eachvoxel.y + worldOffsetY),
                                                        static_cast<float>(eachvoxel.z + worldOffsetZ), 1 });
                }
                belInstancer["steps"][0]["instances"] = xformsArray;
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
        return modelXform;
    }
    return dl::bella_sdk::Node();
}
