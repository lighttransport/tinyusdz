#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cmath>
#include "morton.hh"

namespace tinyusdz {
namespace spatial {

template <typename T = float>
class HybridCityGrid {
public:
    struct Vertex {
        T x, y, z;
        uint32_t id;
        uint32_t layerType;  // 0=ground, 1=building, 2=aerial
    };
    
    // Layer-specific acceleration structures
    struct GroundLayer {
        // 2D grid for ground plane (most dense)
        std::unordered_map<uint32_t, std::vector<uint32_t>> grid2D;
        T cellSize;
        T groundLevel;
        T tolerance;
        
        uint32_t getGridKey(T x, T z) const {
            uint32_t gx = static_cast<uint32_t>(std::max(T(0), x / cellSize));
            uint32_t gz = static_cast<uint32_t>(std::max(T(0), z / cellSize));
            return (gx << 16) | gz;  // 2D Morton code or simple packing
        }
    };
    
    struct BuildingLayer {
        // Separate vertical planes (walls) and horizontal planes (roofs/floors)
        struct Plane {
            T normal[3];
            T d;  // ax + by + cz + d = 0
            T thickness;
            std::vector<uint32_t> indices;
            T bounds[6];  // min/max xyz
        };
        
        std::vector<Plane> verticalPlanes;   // Walls
        std::vector<Plane> horizontalPlanes; // Roofs/floors
        std::unordered_map<uint64_t, std::vector<uint32_t>> volumeGrid; // For complex geometry
        T cellSize;
    };
    
    struct AerialLayer {
        // Sparse 3D grid for aerial/floating points
        std::unordered_map<uint64_t, std::vector<uint32_t>> sparseGrid;
        T cellSize;
    };
    
private:
    std::vector<Vertex> vertices_;
    GroundLayer ground_;
    BuildingLayer buildings_;
    AerialLayer aerial_;
    T origin_[3];
    T bounds_[3];
    
    // Layer detection thresholds
    T groundThreshold_ = 0.5f;      // Height threshold for ground
    T buildingMinHeight_ = 2.0f;    // Min height for buildings
    T buildingMaxHeight_ = 100.0f;  // Max height for buildings
    
public:
    HybridCityGrid(T groundCellSize = 0.5f,
                   T buildingCellSize = 1.0f,
                   T aerialCellSize = 2.0f)
        : ground_{nullptr, groundCellSize, 0, 0.1f},
          buildings_{std::vector<typename BuildingLayer::Plane>(), 
                     std::vector<typename BuildingLayer::Plane>(),
                     std::unordered_map<uint64_t, std::vector<uint32_t>>(),
                     buildingCellSize},
          aerial_{std::unordered_map<uint64_t, std::vector<uint32_t>>(), aerialCellSize} {
        
        origin_[0] = origin_[1] = origin_[2] = std::numeric_limits<T>::max();
        bounds_[0] = bounds_[1] = bounds_[2] = std::numeric_limits<T>::lowest();
    }
    
    void addVertex(T x, T y, T z, uint32_t id) {
        // Classify vertex by layer
        uint32_t layerType = classifyVertex(x, y, z);
        vertices_.emplace_back(Vertex{x, y, z, id, layerType});
        
        // Update bounds
        origin_[0] = std::min(origin_[0], x);
        origin_[1] = std::min(origin_[1], y);
        origin_[2] = std::min(origin_[2], z);
        bounds_[0] = std::max(bounds_[0], x);
        bounds_[1] = std::max(bounds_[1], y);
        bounds_[2] = std::max(bounds_[2], z);
    }
    
    void build() {
        if (vertices_.empty()) return;
        
        // Detect ground level
        detectGroundLevel();
        
        // Separate vertices by layer
        std::vector<uint32_t> groundIndices;
        std::vector<uint32_t> buildingIndices;
        std::vector<uint32_t> aerialIndices;
        
        for (size_t i = 0; i < vertices_.size(); ++i) {
            const auto& v = vertices_[i];
            if (v.layerType == 0) {
                groundIndices.push_back(i);
            } else if (v.layerType == 1) {
                buildingIndices.push_back(i);
            } else {
                aerialIndices.push_back(i);
            }
        }
        
        // Build layer-specific structures
        buildGroundLayer(groundIndices);
        buildBuildingLayer(buildingIndices);
        buildAerialLayer(aerialIndices);
    }
    
    std::vector<uint32_t> findSimilarVertices(T x, T y, T z, T radius) {
        std::vector<uint32_t> results;
        T radiusSq = radius * radius;
        
        // Determine which layers to search
        bool searchGround = (y - ground_.groundLevel) <= radius + ground_.tolerance;
        bool searchBuildings = y >= buildingMinHeight_ - radius && y <= buildingMaxHeight_ + radius;
        bool searchAerial = y >= buildingMaxHeight_ - radius;
        
        // Search appropriate layers
        if (searchGround) {
            searchGroundLayer(x, y, z, radiusSq, results);
        }
        
        if (searchBuildings) {
            searchBuildingLayer(x, y, z, radiusSq, results);
        }
        
        if (searchAerial) {
            searchAerialLayer(x, y, z, radiusSq, results);
        }
        
        // Remove duplicates
        std::sort(results.begin(), results.end());
        results.erase(std::unique(results.begin(), results.end()), results.end());
        
        return results;
    }
    
    void printStatistics() const {
        size_t groundPoints = 0, buildingPoints = 0, aerialPoints = 0;
        
        for (const auto& v : vertices_) {
            if (v.layerType == 0) groundPoints++;
            else if (v.layerType == 1) buildingPoints++;
            else aerialPoints++;
        }
        
        std::cout << "\n=== Hybrid City Grid Statistics ===\n";
        std::cout << "Total vertices: " << vertices_.size() << "\n";
        std::cout << "\nLayer Distribution:\n";
        std::cout << "  Ground layer: " << groundPoints << " points ("
                  << (100.0 * groundPoints / vertices_.size()) << "%)\n";
        std::cout << "  Building layer: " << buildingPoints << " points ("
                  << (100.0 * buildingPoints / vertices_.size()) << "%)\n";
        std::cout << "  Aerial layer: " << aerialPoints << " points ("
                  << (100.0 * aerialPoints / vertices_.size()) << "%)\n";
        
        std::cout << "\nGround Layer:\n";
        std::cout << "  Ground level: " << ground_.groundLevel << "\n";
        std::cout << "  2D grid cells: " << ground_.grid2D.size() << "\n";
        std::cout << "  Cell size: " << ground_.cellSize << "\n";
        
        std::cout << "\nBuilding Layer:\n";
        std::cout << "  Vertical planes: " << buildings_.verticalPlanes.size() << "\n";
        std::cout << "  Horizontal planes: " << buildings_.horizontalPlanes.size() << "\n";
        std::cout << "  Volume grid cells: " << buildings_.volumeGrid.size() << "\n";
        
        std::cout << "\nAerial Layer:\n";
        std::cout << "  Sparse grid cells: " << aerial_.sparseGrid.size() << "\n";
        std::cout << "  Cell size: " << aerial_.cellSize << "\n";
    }
    
private:
    uint32_t classifyVertex(T x, T y, T z) {
        if (y <= groundThreshold_) {
            return 0;  // Ground
        } else if (y >= buildingMinHeight_ && y <= buildingMaxHeight_) {
            return 1;  // Building
        } else {
            return 2;  // Aerial
        }
    }
    
    void detectGroundLevel() {
        // Find mode of Y coordinates near zero
        std::vector<T> lowYValues;
        for (const auto& v : vertices_) {
            if (v.y < groundThreshold_) {
                lowYValues.push_back(v.y);
            }
        }
        
        if (!lowYValues.empty()) {
            std::sort(lowYValues.begin(), lowYValues.end());
            ground_.groundLevel = lowYValues[lowYValues.size() / 2];  // Median
        }
    }
    
    void buildGroundLayer(const std::vector<uint32_t>& indices) {
        for (uint32_t idx : indices) {
            const auto& v = vertices_[idx];
            uint32_t key = ground_.getGridKey(v.x - origin_[0], v.z - origin_[2]);
            ground_.grid2D[key].push_back(idx);
        }
    }
    
    void buildBuildingLayer(const std::vector<uint32_t>& indices) {
        // Detect planes using RANSAC or Hough transform
        detectPlanes(indices);
        
        // Put remaining points in volume grid
        for (uint32_t idx : indices) {
            bool onPlane = false;
            
            // Check if point is on any detected plane
            for (const auto& plane : buildings_.verticalPlanes) {
                for (uint32_t pIdx : plane.indices) {
                    if (pIdx == idx) {
                        onPlane = true;
                        break;
                    }
                }
                if (onPlane) break;
            }
            
            if (!onPlane) {
                for (const auto& plane : buildings_.horizontalPlanes) {
                    for (uint32_t pIdx : plane.indices) {
                        if (pIdx == idx) {
                            onPlane = true;
                            break;
                        }
                    }
                    if (onPlane) break;
                }
            }
            
            // If not on plane, add to volume grid
            if (!onPlane) {
                const auto& v = vertices_[idx];
                uint32_t gx, gy, gz;
                T pos[3] = {v.x - origin_[0], v.y - origin_[1], v.z - origin_[2]};
                computeGridCoords(pos, origin_, buildings_.cellSize, gx, gy, gz);
                uint64_t mortonCode = morton3D64(gx, gy, gz);
                buildings_.volumeGrid[mortonCode].push_back(idx);
            }
        }
    }
    
    void buildAerialLayer(const std::vector<uint32_t>& indices) {
        for (uint32_t idx : indices) {
            const auto& v = vertices_[idx];
            uint32_t gx, gy, gz;
            T pos[3] = {v.x - origin_[0], v.y - origin_[1], v.z - origin_[2]};
            computeGridCoords(pos, origin_, aerial_.cellSize, gx, gy, gz);
            uint64_t mortonCode = morton3D64(gx, gy, gz);
            aerial_.sparseGrid[mortonCode].push_back(idx);
        }
    }
    
    void detectPlanes(const std::vector<uint32_t>& indices) {
        // Simplified plane detection
        // In production, use RANSAC or Hough transform
        // For now, just detect axis-aligned planes
        
        const T planeThreshold = 0.1f;
        std::unordered_map<int, std::vector<uint32_t>> xPlanes;
        std::unordered_map<int, std::vector<uint32_t>> yPlanes;
        std::unordered_map<int, std::vector<uint32_t>> zPlanes;
        
        for (uint32_t idx : indices) {
            const auto& v = vertices_[idx];
            
            // Quantize to detect planes
            int qx = static_cast<int>(v.x / planeThreshold);
            int qy = static_cast<int>(v.y / planeThreshold);
            int qz = static_cast<int>(v.z / planeThreshold);
            
            xPlanes[qx].push_back(idx);
            yPlanes[qy].push_back(idx);
            zPlanes[qz].push_back(idx);
        }
        
        // Create plane structures for large clusters
        const size_t minPlanePoints = 100;
        
        // Vertical planes (X and Z aligned)
        for (const auto& [coord, planeIndices] : xPlanes) {
            if (planeIndices.size() >= minPlanePoints) {
                typename BuildingLayer::Plane plane;
                plane.normal[0] = 1; plane.normal[1] = 0; plane.normal[2] = 0;
                plane.d = -coord * planeThreshold;
                plane.thickness = planeThreshold;
                plane.indices = planeIndices;
                computePlaneBounds(plane);
                buildings_.verticalPlanes.push_back(plane);
            }
        }
        
        for (const auto& [coord, planeIndices] : zPlanes) {
            if (planeIndices.size() >= minPlanePoints) {
                typename BuildingLayer::Plane plane;
                plane.normal[0] = 0; plane.normal[1] = 0; plane.normal[2] = 1;
                plane.d = -coord * planeThreshold;
                plane.thickness = planeThreshold;
                plane.indices = planeIndices;
                computePlaneBounds(plane);
                buildings_.verticalPlanes.push_back(plane);
            }
        }
        
        // Horizontal planes (Y aligned)
        for (const auto& [coord, planeIndices] : yPlanes) {
            if (planeIndices.size() >= minPlanePoints) {
                typename BuildingLayer::Plane plane;
                plane.normal[0] = 0; plane.normal[1] = 1; plane.normal[2] = 0;
                plane.d = -coord * planeThreshold;
                plane.thickness = planeThreshold;
                plane.indices = planeIndices;
                computePlaneBounds(plane);
                buildings_.horizontalPlanes.push_back(plane);
            }
        }
    }
    
    void computePlaneBounds(typename BuildingLayer::Plane& plane) {
        plane.bounds[0] = plane.bounds[2] = plane.bounds[4] = std::numeric_limits<T>::max();
        plane.bounds[1] = plane.bounds[3] = plane.bounds[5] = std::numeric_limits<T>::lowest();
        
        for (uint32_t idx : plane.indices) {
            const auto& v = vertices_[idx];
            plane.bounds[0] = std::min(plane.bounds[0], v.x);
            plane.bounds[1] = std::max(plane.bounds[1], v.x);
            plane.bounds[2] = std::min(plane.bounds[2], v.y);
            plane.bounds[3] = std::max(plane.bounds[3], v.y);
            plane.bounds[4] = std::min(plane.bounds[4], v.z);
            plane.bounds[5] = std::max(plane.bounds[5], v.z);
        }
    }
    
    void searchGroundLayer(T x, T y, T z, T radiusSq, std::vector<uint32_t>& results) {
        // Search 2D grid
        T searchRadius = std::sqrt(radiusSq);
        int cellRadius = static_cast<int>(std::ceil(searchRadius / ground_.cellSize));
        
        uint32_t centerKey = ground_.getGridKey(x - origin_[0], z - origin_[2]);
        uint32_t cx = centerKey >> 16;
        uint32_t cz = centerKey & 0xFFFF;
        
        for (int dx = -cellRadius; dx <= cellRadius; ++dx) {
            for (int dz = -cellRadius; dz <= cellRadius; ++dz) {
                int nx = cx + dx;
                int nz = cz + dz;
                if (nx < 0 || nz < 0) continue;
                
                uint32_t key = (nx << 16) | nz;
                auto it = ground_.grid2D.find(key);
                if (it != ground_.grid2D.end()) {
                    for (uint32_t idx : it->second) {
                        const auto& v = vertices_[idx];
                        T distSq = (v.x - x) * (v.x - x) + 
                                  (v.y - y) * (v.y - y) + 
                                  (v.z - z) * (v.z - z);
                        if (distSq <= radiusSq) {
                            results.push_back(v.id);
                        }
                    }
                }
            }
        }
    }
    
    void searchBuildingLayer(T x, T y, T z, T radiusSq, std::vector<uint32_t>& results) {
        T searchRadius = std::sqrt(radiusSq);
        
        // Search planes
        for (const auto& plane : buildings_.verticalPlanes) {
            if (pointNearPlane(x, y, z, plane, searchRadius)) {
                searchPlane(x, y, z, radiusSq, plane, results);
            }
        }
        
        for (const auto& plane : buildings_.horizontalPlanes) {
            if (pointNearPlane(x, y, z, plane, searchRadius)) {
                searchPlane(x, y, z, radiusSq, plane, results);
            }
        }
        
        // Search volume grid
        searchVolumeGrid(x, y, z, radiusSq, buildings_.volumeGrid, 
                        buildings_.cellSize, results);
    }
    
    void searchAerialLayer(T x, T y, T z, T radiusSq, std::vector<uint32_t>& results) {
        searchVolumeGrid(x, y, z, radiusSq, aerial_.sparseGrid, 
                        aerial_.cellSize, results);
    }
    
    bool pointNearPlane(T x, T y, T z, const typename BuildingLayer::Plane& plane, T radius) {
        // Check bounds first
        if (x < plane.bounds[0] - radius || x > plane.bounds[1] + radius ||
            y < plane.bounds[2] - radius || y > plane.bounds[3] + radius ||
            z < plane.bounds[4] - radius || z > plane.bounds[5] + radius) {
            return false;
        }
        
        // Check distance to plane
        T dist = std::abs(plane.normal[0] * x + plane.normal[1] * y + 
                         plane.normal[2] * z + plane.d);
        return dist <= radius + plane.thickness;
    }
    
    void searchPlane(T x, T y, T z, T radiusSq, 
                    const typename BuildingLayer::Plane& plane,
                    std::vector<uint32_t>& results) {
        for (uint32_t idx : plane.indices) {
            const auto& v = vertices_[idx];
            T distSq = (v.x - x) * (v.x - x) + 
                      (v.y - y) * (v.y - y) + 
                      (v.z - z) * (v.z - z);
            if (distSq <= radiusSq) {
                results.push_back(v.id);
            }
        }
    }
    
    void searchVolumeGrid(T x, T y, T z, T radiusSq,
                         const std::unordered_map<uint64_t, std::vector<uint32_t>>& grid,
                         T cellSize, std::vector<uint32_t>& results) {
        uint32_t gx, gy, gz;
        T pos[3] = {x - origin_[0], y - origin_[1], z - origin_[2]};
        computeGridCoords(pos, origin_, cellSize, gx, gy, gz);
        
        T searchRadius = std::sqrt(radiusSq);
        int cellRadius = static_cast<int>(std::ceil(searchRadius / cellSize));
        
        for (int dx = -cellRadius; dx <= cellRadius; ++dx) {
            for (int dy = -cellRadius; dy <= cellRadius; ++dy) {
                for (int dz = -cellRadius; dz <= cellRadius; ++dz) {
                    int nx = gx + dx;
                    int ny = gy + dy;
                    int nz = gz + dz;
                    if (nx < 0 || ny < 0 || nz < 0) continue;
                    
                    uint64_t mortonCode = morton3D64(nx, ny, nz);
                    auto it = grid.find(mortonCode);
                    if (it != grid.end()) {
                        for (uint32_t idx : it->second) {
                            const auto& v = vertices_[idx];
                            T distSq = (v.x - x) * (v.x - x) + 
                                      (v.y - y) * (v.y - y) + 
                                      (v.z - z) * (v.z - z);
                            if (distSq <= radiusSq) {
                                results.push_back(v.id);
                            }
                        }
                    }
                }
            }
        }
    }
};

} // namespace spatial
} // namespace tinyusdz