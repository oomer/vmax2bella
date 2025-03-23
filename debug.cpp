#include "debug.h"


/**
 * Decodes a voxel's layercolor and color from the ds data stream
 * 
 * @param dsData The raw ds data stream containing layer-color pairs
 * @return A vector of Voxel structures with explicit coordinates and colors
 */
std::vector<newVoxel> decodeVoxels2(const std::vector<uint8_t>& dsData, int mortonOffset) {
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
 * Print a plist node's contents recursively.
 * This function takes a plist node and prints its contents in a human-readable format.
 * It handles all types of plist nodes (dictionaries, arrays, strings, etc.) by using
 * recursion to traverse the entire plist structure.
 * 
 * @param node The plist node to print (plist_t is a pointer to the internal plist structure)
 * @param indent The current indentation level (defaults to 0 for the root node)
 */
void printPlistNode(const plist_t& node, int indent ) {
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
 * New visualization function that definitely uses the correct z-plane
 * 
 * @param voxels The vector of decoded voxels
 * @param zPlane The z-coordinate of the plane to visualize
 * @param size The size of the grid (default: 32x32)
 */
void visualizeZPlaneFixed(const std::vector<newVoxel>& voxels, int zPlane, int size ) {
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
                        std::vector<newVoxel> voxels = decodeVoxels2(std::vector<uint8_t>(data, data + length), 0);
                        
                        printVoxelTable(voxels, 100);
                        
                        // Explicitly decode the voxels for visualization
                        char* data = nullptr;
                        uint64_t length = 0;
                        plist_get_data_val(dsNode, &data, &length);
                        
                        if (length > 0 && data) {
                            std::vector<newVoxel> voxels = decodeVoxels2(std::vector<uint8_t>(data, data + length), 0);
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

/**
 * Prints a table of voxel positions and colors
 * 
 * @param voxels The vector of decoded voxels
 * @param limit Maximum number of voxels to display (0 for all)
 * @param filterZ Optional z-value to filter by
 */
void printVoxelTable(const std::vector<newVoxel>& voxels, size_t limit , int filterZ ) {
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
