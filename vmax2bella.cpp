#include <iostream>
#include "../bella_scene_sdk/src/bella_sdk/bella_scene.h"
#include "../bella_scene_sdk/src/dl_core/dl_main.inl"

#include "oomer_voxel_vmax.h" // common voxel code and structures
#include "oomer_misc.h"  

int addModelToScene(dl::bella_sdk::Scene& belScene, const VmaxModel& vmaxModel, const std::vector<VoxelRGBA>& vmaxPalette, const std::array<VmaxMaterial, 8>& vmaxMaterial); 

int DL_main(dl::Args& args) {
    args.add("he", "hello", "world", "Print a hello message with custom text");
    args.add("vi", "voxin", "voxin", "voxin");

    dl::String bszName;
    dl::String vmaxDirName;
    if (args.have("--voxin"))
    {
        vmaxDirName = args.value("--voxin");
        bszName = vmaxDirName.replace("vmax", "bsa");

        JsonVmaxSceneParser vmaxSceneParser;
        vmaxSceneParser.parseScene((vmaxDirName+"/scene.json").buf());
        auto models = vmaxSceneParser.getModels();

        // Efficiently process unique models by examining only the first instance of each model type.
        // Example: If we have 100 instances of 3 different models:
        //   "model1.vmaxb": [instance1, instance2, ..., instance50],
        //   "model2.vmaxb": [instance1, ..., instance30],
        //   "model3.vmaxb": [instance1, ..., instance20]
        // This loop runs only 3 times (once per unique model), not 100 times (once per instance)
        //std::map<std::string, std::vector<JsonModelInfo>> modelVmaxbMap = vmaxSceneParser.getModelContentVMaxbMap(); 
        auto modelVmaxbMap = vmaxSceneParser.getModelContentVMaxbMap(); 
        for (auto& model : modelVmaxbMap) {
            for (const auto& [vmaxContentName, vmaxModelList] : modelVmaxbMap) { 
                std::cout << "model: " << vmaxContentName << std::endl;
                const auto& jsonModelInfo = vmaxModelList.front(); // get the first model, others are instances at the scene level
                std::cout << "name: " << jsonModelInfo.name << std::endl;
                std::cout << "content: " << jsonModelInfo.dataFile << std::endl;
                std::cout << "palette: " << jsonModelInfo.paletteFile << std::endl;
                dl::String materialName = vmaxDirName + "/" + jsonModelInfo.paletteFile.c_str();
                materialName = materialName.replace(".png", ".settings.vmaxpsb");
                std::cout << "material: " << materialName << std::endl;
                plist_t pnod_material = readPlist(materialName.buf(),false);
                std::cout << "pnod_material: " << pnod_material << std::endl;
                std::array<VmaxMaterial, 8> vmaxMaterials = getVmaxMaterials(pnod_material);
            }
        }
    }

    if (args.helpRequested()) {
        std::cout << args.help("Hello App", "hello", "1.0") << std::endl;
        return 0;
    }
    
    if (args.have("--hello")) {
        dl::String helloText = args.value("--hello", "world");
    }

    // Create a new scene
    dl::bella_sdk::Scene belScene;
    belScene.loadDefs();

    // Root node
    auto belWorld = belScene.world(true);

    JsonVmaxSceneParser vmaxSceneParser;
    vmaxSceneParser.parseScene((vmaxDirName+"/scene.json").buf());
    auto models = vmaxSceneParser.getModels();

    // Efficiently process unique models by examining only the first instance of each model type.
    // Example: If we have 100 instances of 3 different models:
    //   "model1.vmaxb": [instance1, instance2, ..., instance50],
    //   "model2.vmaxb": [instance1, ..., instance30],
    //   "model3.vmaxb": [instance1, ..., instance20]
    // This loop runs only 3 times (once per unique model), not 100 times (once per instance)
    //std::map<std::string, std::vector<JsonModelInfo>> modelVmaxbMap = vmaxSceneParser.getModelContentVMaxbMap(); 
    
    auto modelVmaxbMap = vmaxSceneParser.getModelContentVMaxbMap(); 

    std::vector<std::vector<VoxelRGBA>> vmaxPalettes;
    std::vector<std::array<VmaxMaterial, 8>> vmaxMaterials;
    std::vector<VmaxModel> allModels;
    std::vector<std::array<VmaxMaterial, 8>> allMaterials; // dynamic vector of fixed arrays
    std::vector<std::vector<VoxelRGBA>> allPalettes;
    for (auto& model : modelVmaxbMap) {
        for (const auto& [vmaxContentName, vmaxModelList] : modelVmaxbMap) { 
            std::cout << "model: " << vmaxContentName << std::endl;
            const auto& jsonModelInfo = vmaxModelList.front(); // get the first model, others are instances at the scene level
            std::cout << "name: " << jsonModelInfo.name << std::endl;
            std::cout << "content: " << jsonModelInfo.dataFile << std::endl;
            std::cout << "palette: " << jsonModelInfo.paletteFile << std::endl;
            // Get file names
            dl::String materialName = vmaxDirName + "/" + jsonModelInfo.paletteFile.c_str();
            materialName = materialName.replace(".png", ".settings.vmaxpsb");

            // Get this models colors from the paletteN.png 
            dl::String pngName = vmaxDirName + "/" + jsonModelInfo.paletteFile.c_str();
            vmaxPalettes.push_back(read256x1PaletteFromPNG(pngName.buf())); // gather all models palettes
            allPalettes.push_back(read256x1PaletteFromPNG(pngName.buf())); // gather all models palettes
            if (vmaxPalettes.empty()) { throw std::runtime_error("Failed to read palette from: png " ); }

            // lstSnapshots contains ALL snapshots
            // if a chunkID is present more than once it holds the past history of a snapshot's state 
            // The playback of each of these snapshots is the entire creation history of a chunk
            // Therefore a chunkID's last snapshot is the latest user edit
            // Therefore we need to process each chunkdID's snapshot in reverse order and ignore 
            // all previous snapshots for that chunkID unless we need animation
            // [todo] implement animation

            // Read contentsN.vmaxb plist file, lzfse compressed
            dl::String modelName2 = vmaxDirName + "/" + jsonModelInfo.dataFile.c_str();
            plist_t plist_model_root = readPlist(modelName2.buf(), true); // decompress=true

            plist_t plist_snapshots_array = plist_dict_get_item(plist_model_root, "snapshots");
            uint32_t snapshots_array_size = plist_array_get_size(plist_snapshots_array);
            std::cout << "snapshots_array_size: " << snapshots_array_size << std::endl;

            // Create a VmaxModel object
            VmaxModel currentVmaxModel(vmaxContentName);

            // Add a voxel to this model
            //void addVoxel(int x, int y, int z, int material, int color, int chunk, int chunkMin) {
            //    if (material >= 0 && material < 8 && color > 0 && color < 256) {
            //        voxels[material][color].emplace_back(x, y, z, material, color, chunk, chunkMin);
            //    }
            //}

            for (uint32_t i = 0; i < snapshots_array_size; i++) {
                plist_t plist_snapshot = plist_array_get_item(plist_snapshots_array, i);
                plist_t plist_chunk = getNestedPlistNode(plist_snapshot, {"s", "id", "c"});
                plist_t plist_datastream = getNestedPlistNode(plist_snapshot, {"s", "ds"});
                uint64_t chunkID;
                plist_get_uint_val(plist_chunk, &chunkID);
                VmaxChunkInfo chunkInfo = vmaxChunkInfo(plist_snapshot);
                std::cout << "\nChunkID: " << chunkInfo.id << std::endl;
                std::cout << "TypeID: " << chunkInfo.type << std::endl;
                std::cout << "MortonCode: " << chunkInfo.mortoncode << "\n" <<std::endl;


                std::vector<VmaxVoxel> xvoxels = vmaxVoxelInfo(plist_datastream, chunkInfo.id, chunkInfo.mortoncode);
                std::cout << "xxxvoxels: " << xvoxels.size() << std::endl;

                for (const auto& voxel : xvoxels) {
                    currentVmaxModel.addVoxel(voxel.x, voxel.y, voxel.z, voxel.material, voxel.palette ,chunkInfo.id, chunkInfo.mortoncode);
                }
            }
            allModels.push_back(currentVmaxModel);
            // Parse the materials store in paletteN.settings.vmaxpsb    
            plist_t plist_material = readPlist(materialName.buf(),false); // decompress=false
            std::array<VmaxMaterial, 8> currentMaterials = getVmaxMaterials(plist_material);
            vmaxMaterials.push_back(currentMaterials);
            allMaterials.push_back(currentMaterials);
            //allPalettes.push_back(vmaxPalettes);
            int modelIndex=0;
            // Need to access voxles by material and color groupings
            for (const auto& eachModel : allModels) {
                if (modelIndex == 0) {
                    std::cout << "Model: " << eachModel.vmaxbFileName << std::endl;
                    std::cout << "Voxel Count Model: " << eachModel.getTotalVoxelCount() << std::endl;

                    addModelToScene(belScene, eachModel, allPalettes[modelIndex], allMaterials[modelIndex]);
                    std::map<int, std::set<int>> materialColorMap = eachModel.getUsedMaterialsAndColors();
                    //auto materialColorMap = eachModel.getUsedMaterialsAndColors();
                    for (const auto& [eachMaterial, eachColorPalette] : materialColorMap) {
                        // Create Bella material for this material type
                        // ... your Bella material creation code here ...
                        std::cout << eachMaterial << std::endl;
                        // Iterate through each color used by this material
                        for (int eachColor : eachColorPalette) {
                            // Get all voxels for this material/color combination
                            const std::vector<VmaxVoxel>& voxelsOfType = eachModel.getVoxels(eachMaterial, eachColor);
                            std::cout << "voxelsOfType: " << voxelsOfType.size() << std::endl;
                            // Now voxelsOfType contains all voxels sharing this material/color
                            // Create a single Bella object for all these voxels
                            // ... your Bella object creation code here ...
                        }
                    }
                }
                modelIndex++;
            } 
        }
    }


    // Write Bella File .bsz=compressed .bsa=ascii .bsx=binary
    std::cout << "writing to: " << bszName.buf() << std::endl;
    belScene.write(bszName.buf());
    //writeBszScene(bszName.buf(), AllModels[0], vmaxPalettes);
    return 0;
}


int addModelToScene(dl::bella_sdk::Scene& belScene, const VmaxModel& vmaxModel, const std::vector<VoxelRGBA>& vmaxPalette, const std::array<VmaxMaterial, 8>& vmaxMaterial) {
    // Create a new Bella scene

    // Create the basic scene elements in Bella
    // Each line creates a different type of node in the scene
    auto belBeautyPass     = belScene.createNode("beautyPass","beautyPass1","beautyPass1");
    auto belCamForm    = belScene.createNode("xform","cameraXform1","cameraXform1");
    auto belCam        = belScene.createNode("camera","camera1","camera1");
    auto belSensor         = belScene.createNode("sensor","sensor1","sensor1");
    auto belLens           = belScene.createNode("thinLens","thinLens1","thinLens1");
    auto belImageDome      = belScene.createNode("imageDome","imageDome1","imageDome1");
    auto belGroundPlane    = belScene.createNode("groundPlane","groundPlane1","groundPlane1");
    auto belVoxel          = belScene.createNode("box","box1","box1");
    auto belVoxelForm      = belScene.createNode("xform","voxelXform1","voxelXform1");
    auto belVoxelMat       = belScene.createNode("orenNayar","voxelMat1","voxelMat1");
    auto belGroundMat      = belScene.createNode("quickMaterial","groundMat1","groundMat1");
    auto belSun            = belScene.createNode("sun","sun1","sun1");
    auto belColorDome   = belScene.createNode("colorDome","colorDome1","colorDome1");
    auto oomerSmoothCube = belScene.createNode("mesh", "oomerSmoothCube");

    // Set up the scene with an EventScope 
    // EventScope groups multiple changes together for efficiency
    {
        #include "smoothcube.h"
        dl::bella_sdk::Scene::EventScope es(belScene);
        auto belSettings = belScene.settings(); // Get scene settings
        auto belWorld = belScene.world();       // Get scene world root

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

        // Configure voxel box dimensions
        belVoxel["radius"]           = 0.33f;
        belVoxel["sizeX"]            = 0.99f;
        belVoxel["sizeY"]            = 0.99f;
        belVoxel["sizeZ"]            = 0.99f;
        belVoxel.parentTo(belVoxelForm);
        belVoxelForm["steps"][0]["xform"] = dl::Mat4 {0.999,0,0,0,0,0.999,0,0,0,0,0.999,0,0,0,0,1};
        belVoxelMat["reflectance"] = dl::Rgba{0.0, 0.0, 0.0, 1.0};
        belVoxelForm["material"] = belVoxelMat;

        // Set up scene settings
        belSettings["beautyPass"]  = belBeautyPass;
        belSettings["camera"]      = belCam;
        belSettings["environment"] = belColorDome;
        belSettings["iprScale"]    = 100.0f;
        belSettings["threads"]     = dl::bella_sdk::Input(0);  // Auto-detect thread count
        belSettings["groundPlane"] = belGroundPlane;
        belSettings["iprNavigation"] = "maya";  // Use Maya-like navigation in viewer
        //settings["sun"] = sun;
    }
    // Create Bella scene nodes for each voxel
    int i = 0;
    dl::String modelName = dl::String(vmaxModel.vmaxbFileName.c_str());
    auto modelXform = belScene.createNode("xform", modelName, modelName);
    modelXform.parentTo(belScene.world());

    for (const auto& [material, colorID] : vmaxModel.getUsedMaterialsAndColors()) {
        for (int color : colorID) {
            auto belInstancer  = belScene.createNode("instancer",
                              dl::String("instMat") + dl::String(material) + dl::String("Color") + dl::String(color));
            auto xformsArray = dl::ds::Vector<dl::Mat4f>();
            belInstancer.parentTo(modelXform);


            auto belMaterial  = belScene.createNode("quickMaterial",
                              dl::String("vmaxMat") + dl::String(material) + dl::String("Color") + dl::String(color));

            std::cout << "vmaxMaterial: " << vmaxMaterial[material].materialName << std::endl;
            std::cout << "metalness: " << vmaxMaterial[material].metalness << std::endl;
            std::cout << "roughness: " << vmaxMaterial[material].roughness << std::endl;
            std::cout << "transmission: " << vmaxMaterial[material].transmission << std::endl;
            std::cout << "emission: " << vmaxMaterial[material].emission << std::endl;

            if(material==7) {
                belMaterial["type"] = "liquid";
                belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
            } else if(material==6) {
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
            std::cout << "material: " << material << std::endl;
            // Convert 0-255 to 0-1
            double bellaR = static_cast<double>(vmaxPalette[color-1].r)/255.0;
            double bellaG = static_cast<double>(vmaxPalette[color-1].g)/255.0;
            double bellaB = static_cast<double>(vmaxPalette[color-1].b)/255.0;
            double bellaA = static_cast<double>(vmaxPalette[color-1].a)/255.0;
            /*
            belMaterial["color"] = dl::Rgba{ // convert sRGB to linear
                bellaR, 
                bellaG, 
                bellaB, 
                bellaA // alpha is already linear
            }; // colors ready to use in Bella
            */
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
            oomerSmoothCube.parentTo(belInstancer);
            if(vmaxMaterial[material].emission > 0.0f) {
               belVoxelForm.parentTo(belInstancer);
            }
        }
        i++;
    }
    return 0;
}
