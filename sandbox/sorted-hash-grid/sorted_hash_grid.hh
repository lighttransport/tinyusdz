#pragma once

#include <vector>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <functional>
#include <array>
#include <limits>

#include "morton.hh"

namespace tinyusdz {
namespace spatial {

template <typename T = float>
class SortedHashGrid {
public:
    struct Vertex {
        T x, y, z;
        uint32_t id;
        
        Vertex() : x(0), y(0), z(0), id(0) {}
        Vertex(T px, T py, T pz, uint32_t vid) : x(px), y(py), z(pz), id(vid) {}
    };
    
    struct Cell {
        std::vector<uint32_t> indices;
        std::unique_ptr<SortedHashGrid> subcell;
        uint32_t level = 0;
    };
    
    struct SearchResult {
        uint32_t vertexId;
        T distanceSquared;
    };
    
    using SimilarityFunc = std::function<bool(const Vertex&, const Vertex&, T)>;
    
protected:
    std::vector<Vertex> vertices_;
    std::unordered_map<uint64_t, Cell> grid_;
    T cellSize_;
    T origin_[3];
    T bounds_[3];
    uint32_t maxItemsPerCell_;
    uint32_t currentLevel_;
    uint32_t maxLevel_;
    
    std::vector<std::pair<uint64_t, uint32_t>> sortedEntries_;
    bool needsRebuild_ = true;
    
public:
    SortedHashGrid(T cellSize = 0.01f, uint32_t maxItemsPerCell = 128, 
                   uint32_t level = 0, uint32_t maxLevel = 5)
        : cellSize_(cellSize), 
          maxItemsPerCell_(maxItemsPerCell),
          currentLevel_(level),
          maxLevel_(maxLevel) {
        origin_[0] = origin_[1] = origin_[2] = std::numeric_limits<T>::max();
        bounds_[0] = bounds_[1] = bounds_[2] = std::numeric_limits<T>::lowest();
    }
    
    void addVertex(T x, T y, T z, uint32_t id) {
        vertices_.emplace_back(x, y, z, id);
        
        origin_[0] = std::min(origin_[0], x);
        origin_[1] = std::min(origin_[1], y);
        origin_[2] = std::min(origin_[2], z);
        
        bounds_[0] = std::max(bounds_[0], x);
        bounds_[1] = std::max(bounds_[1], y);
        bounds_[2] = std::max(bounds_[2], z);
        
        needsRebuild_ = true;
    }
    
    void addVertices(const T* positions, size_t count, uint32_t startId = 0) {
        vertices_.reserve(vertices_.size() + count);
        for (size_t i = 0; i < count; ++i) {
            addVertex(positions[i*3], positions[i*3+1], positions[i*3+2], 
                     startId + static_cast<uint32_t>(i));
        }
    }
    
    void build() {
        if (!needsRebuild_ || vertices_.empty()) return;
        
        grid_.clear();
        sortedEntries_.clear();
        sortedEntries_.reserve(vertices_.size());
        
        T extendedOrigin[3] = {
            origin_[0] - cellSize_,
            origin_[1] - cellSize_,
            origin_[2] - cellSize_
        };
        
        for (size_t i = 0; i < vertices_.size(); ++i) {
            const auto& v = vertices_[i];
            uint32_t gx, gy, gz;
            T pos[3] = {v.x, v.y, v.z};
            computeGridCoords(pos, extendedOrigin, cellSize_, gx, gy, gz);
            
            uint64_t mortonCode = morton3D64(gx, gy, gz);
            sortedEntries_.emplace_back(mortonCode, static_cast<uint32_t>(i));
        }
        
        std::sort(sortedEntries_.begin(), sortedEntries_.end());
        
        for (const auto& entry : sortedEntries_) {
            grid_[entry.first].indices.push_back(entry.second);
        }
        
        if (currentLevel_ < maxLevel_) {
            for (auto& [mortonCode, cell] : grid_) {
                if (cell.indices.size() > maxItemsPerCell_) {
                    subdivideCell(mortonCode, cell);
                }
            }
        }
        
        needsRebuild_ = false;
    }
    
    std::vector<SearchResult> findSimilarVertices(T x, T y, T z, T threshold,
                                                  const SimilarityFunc& simFunc = nullptr) {
        if (needsRebuild_) build();
        
        std::vector<SearchResult> results;
        T thresholdSq = threshold * threshold;
        
        T extendedOrigin[3] = {
            origin_[0] - cellSize_,
            origin_[1] - cellSize_,
            origin_[2] - cellSize_
        };
        
        uint32_t gx, gy, gz;
        T pos[3] = {x, y, z};
        computeGridCoords(pos, extendedOrigin, cellSize_, gx, gy, gz);
        
        static const auto neighborOffsets = getNeighborOffsets();
        
        for (const auto& offset : neighborOffsets) {
            int nx = static_cast<int>(gx) + offset[0];
            int ny = static_cast<int>(gy) + offset[1];
            int nz = static_cast<int>(gz) + offset[2];
            
            if (nx < 0 || ny < 0 || nz < 0) continue;
            
            uint64_t neighborCode = morton3D64(
                static_cast<uint32_t>(nx),
                static_cast<uint32_t>(ny),
                static_cast<uint32_t>(nz)
            );
            
            auto it = grid_.find(neighborCode);
            if (it != grid_.end()) {
                searchInCell(it->second, x, y, z, thresholdSq, simFunc, results);
            }
        }
        
        std::sort(results.begin(), results.end(), 
                 [](const SearchResult& a, const SearchResult& b) {
                     return a.distanceSquared < b.distanceSquared;
                 });
        
        return results;
    }
    
    std::vector<uint32_t> findExactVertex(T x, T y, T z, T epsilon = 1e-7f) {
        auto results = findSimilarVertices(x, y, z, epsilon);
        std::vector<uint32_t> exactMatches;
        
        for (const auto& r : results) {
            if (r.distanceSquared < epsilon * epsilon) {
                exactMatches.push_back(r.vertexId);
            }
        }
        
        return exactMatches;
    }
    
    void clear() {
        vertices_.clear();
        grid_.clear();
        sortedEntries_.clear();
        origin_[0] = origin_[1] = origin_[2] = std::numeric_limits<T>::max();
        bounds_[0] = bounds_[1] = bounds_[2] = std::numeric_limits<T>::lowest();
        needsRebuild_ = true;
    }
    
    size_t getVertexCount() const { return vertices_.size(); }
    size_t getCellCount() const { return grid_.size(); }
    T getCellSize() const { return cellSize_; }
    
    const Vertex& getVertex(uint32_t idx) const { return vertices_[idx]; }
    
    void getStatistics(size_t& totalCells, size_t& maxCellSize, 
                      size_t& avgCellSize, size_t& subdivisionCount) const {
        totalCells = grid_.size();
        maxCellSize = 0;
        size_t totalItems = 0;
        subdivisionCount = 0;
        
        for (const auto& [code, cell] : grid_) {
            size_t cellItemCount = cell.indices.size();
            if (cell.subcell) {
                cellItemCount = 0;
                size_t subCells, subMax, subAvg, subSubdiv;
                cell.subcell->getStatistics(subCells, subMax, subAvg, subSubdiv);
                subdivisionCount += 1 + subSubdiv;
            }
            maxCellSize = std::max(maxCellSize, cellItemCount);
            totalItems += cellItemCount;
        }
        
        avgCellSize = totalCells > 0 ? totalItems / totalCells : 0;
    }
    
private:
    void subdivideCell(uint64_t mortonCode, Cell& cell) {
        uint32_t x, y, z;
        decodeMorton3D64(mortonCode, x, y, z);
        
        T subcellSize = cellSize_ / 2.0f;
        cell.subcell = std::make_unique<SortedHashGrid>(
            subcellSize, maxItemsPerCell_, currentLevel_ + 1, maxLevel_
        );
        
        T extendedOrigin[3] = {
            origin_[0] - cellSize_,
            origin_[1] - cellSize_,
            origin_[2] - cellSize_
        };
        
        T cellOrigin[3] = {
            extendedOrigin[0] + x * cellSize_,
            extendedOrigin[1] + y * cellSize_,
            extendedOrigin[2] + z * cellSize_
        };
        
        cell.subcell->origin_[0] = cellOrigin[0];
        cell.subcell->origin_[1] = cellOrigin[1];
        cell.subcell->origin_[2] = cellOrigin[2];
        
        cell.subcell->bounds_[0] = cellOrigin[0] + cellSize_;
        cell.subcell->bounds_[1] = cellOrigin[1] + cellSize_;
        cell.subcell->bounds_[2] = cellOrigin[2] + cellSize_;
        
        for (uint32_t idx : cell.indices) {
            const auto& v = vertices_[idx];
            cell.subcell->vertices_.push_back(v);
        }
        
        cell.subcell->build();
        
        cell.indices.clear();
        cell.level = currentLevel_ + 1;
    }
    
    void searchInCell(const Cell& cell, T x, T y, T z, T thresholdSq,
                     const SimilarityFunc& simFunc,
                     std::vector<SearchResult>& results) {
        if (cell.subcell) {
            auto subResults = cell.subcell->findSimilarVertices(
                x, y, z, std::sqrt(thresholdSq), simFunc
            );
            results.insert(results.end(), subResults.begin(), subResults.end());
        } else {
            Vertex queryVertex(x, y, z, 0);
            
            for (uint32_t idx : cell.indices) {
                const auto& v = vertices_[idx];
                T dx = v.x - x;
                T dy = v.y - y;
                T dz = v.z - z;
                T distSq = dx*dx + dy*dy + dz*dz;
                
                if (distSq <= thresholdSq) {
                    if (!simFunc || simFunc(queryVertex, v, std::sqrt(thresholdSq))) {
                        results.push_back({v.id, distSq});
                    }
                }
            }
        }
    }
};

} // namespace spatial
} // namespace tinyusdz