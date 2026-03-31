#pragma once

#include "sorted_hash_grid.hh"
#include <map>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace tinyusdz {
namespace spatial {

template <typename T = float>
class SortedHashGridExtended : public SortedHashGrid<T> {
public:
    using Base = SortedHashGrid<T>;
    using typename Base::Vertex;
    using typename Base::Cell;
    
    SortedHashGridExtended(T cellSize = 0.01f, uint32_t maxItemsPerCell = 128, 
                           uint32_t level = 0, uint32_t maxLevel = 5)
        : Base(cellSize, maxItemsPerCell, level, maxLevel) {}
    
    size_t getMemoryUsage() const {
        size_t totalMemory = 0;
        
        // Base class storage
        totalMemory += sizeof(*this);
        
        // Vertices vector
        totalMemory += this->vertices_.size() * sizeof(Vertex);
        totalMemory += this->vertices_.capacity() * sizeof(Vertex) - this->vertices_.size() * sizeof(Vertex); // unused capacity
        
        // Grid unordered_map
        totalMemory += this->grid_.size() * (sizeof(uint64_t) + sizeof(Cell));
        totalMemory += this->grid_.bucket_count() * sizeof(void*); // hash table buckets
        
        // Cell indices storage
        for (const auto& [code, cell] : this->grid_) {
            totalMemory += cell.indices.capacity() * sizeof(uint32_t);
            if (cell.subcell) {
                totalMemory += estimateSubcellMemory(cell.subcell.get());
            }
        }
        
        // Sorted entries vector
        totalMemory += this->sortedEntries_.capacity() * sizeof(std::pair<uint64_t, uint32_t>);
        
        return totalMemory;
    }
    
    std::map<size_t, size_t> getCellItemHistogram() const {
        std::map<size_t, size_t> histogram;
        collectHistogram(this->grid_, histogram);
        return histogram;
    }
    
    void printDetailedStatistics() const {
        size_t totalCells = 0, maxCellSize = 0, totalItems = 0, subdivCount = 0;
        size_t emptyCells = 0, leafCells = 0;
        
        analyzeGrid(this->grid_, this->currentLevel_, totalCells, maxCellSize, 
                   totalItems, subdivCount, emptyCells, leafCells);
        
        auto histogram = getCellItemHistogram();
        
        std::cout << "\n=== Detailed Grid Statistics ===\n";
        std::cout << "Grid Parameters:\n";
        std::cout << "  Cell size: " << this->cellSize_ << "\n";
        std::cout << "  Max items per cell: " << this->maxItemsPerCell_ << "\n";
        std::cout << "  Max subdivision level: " << this->maxLevel_ << "\n";
        
        std::cout << "\nGrid Structure:\n";
        std::cout << "  Total vertices: " << this->vertices_.size() << "\n";
        std::cout << "  Total cells: " << totalCells << "\n";
        std::cout << "  Leaf cells: " << leafCells << "\n";
        std::cout << "  Empty cells: " << emptyCells << "\n";
        std::cout << "  Subdivided cells: " << subdivCount << "\n";
        
        std::cout << "\nCell Occupancy:\n";
        std::cout << "  Max items in a cell: " << maxCellSize << "\n";
        std::cout << "  Average items per leaf cell: " 
                  << (leafCells > 0 ? static_cast<double>(totalItems) / leafCells : 0) << "\n";
        
        std::cout << "\nMemory Usage:\n";
        size_t memUsage = getMemoryUsage();
        std::cout << "  Total memory: " << formatBytes(memUsage) << "\n";
        std::cout << "  Memory per vertex: " 
                  << (this->vertices_.size() > 0 ? memUsage / this->vertices_.size() : 0) << " bytes\n";
        
        std::cout << "\nCell Item Distribution Histogram:\n";
        std::cout << "  Items | Count | Percentage | Bar\n";
        std::cout << "  ------|-------|------------|--------------------\n";
        
        size_t maxCount = 0;
        for (const auto& [items, count] : histogram) {
            maxCount = std::max(maxCount, count);
        }
        
        for (const auto& [items, count] : histogram) {
            double percentage = (leafCells > 0) ? 100.0 * count / leafCells : 0;
            int barLength = (maxCount > 0) ? static_cast<int>(50.0 * count / maxCount) : 0;
            
            std::cout << "  " << std::setw(5) << items 
                      << " | " << std::setw(5) << count
                      << " | " << std::setw(10) << std::fixed << std::setprecision(2) << percentage << "%"
                      << " | ";
            
            for (int i = 0; i < barLength; ++i) std::cout << "█";
            std::cout << "\n";
        }
        
        // Calculate percentiles
        std::vector<size_t> cellSizes;
        collectCellSizes(this->grid_, cellSizes);
        if (!cellSizes.empty()) {
            std::sort(cellSizes.begin(), cellSizes.end());
            
            std::cout << "\nCell Size Percentiles:\n";
            std::cout << "  50th percentile (median): " << cellSizes[cellSizes.size() * 50 / 100] << " items\n";
            std::cout << "  75th percentile: " << cellSizes[cellSizes.size() * 75 / 100] << " items\n";
            std::cout << "  90th percentile: " << cellSizes[cellSizes.size() * 90 / 100] << " items\n";
            std::cout << "  95th percentile: " << cellSizes[cellSizes.size() * 95 / 100] << " items\n";
            std::cout << "  99th percentile: " << cellSizes[cellSizes.size() * 99 / 100] << " items\n";
        }
        
        std::cout << "\nSpatial Bounds:\n";
        std::cout << "  Origin: (" << this->origin_[0] << ", " << this->origin_[1] << ", " << this->origin_[2] << ")\n";
        std::cout << "  Bounds: (" << this->bounds_[0] << ", " << this->bounds_[1] << ", " << this->bounds_[2] << ")\n";
        std::cout << "  Dimensions: (" 
                  << (this->bounds_[0] - this->origin_[0]) << ", "
                  << (this->bounds_[1] - this->origin_[1]) << ", "
                  << (this->bounds_[2] - this->origin_[2]) << ")\n";
    }
    
private:
    size_t estimateSubcellMemory(const SortedHashGrid<T>* subcell) const {
        if (!subcell) return 0;
        
        // Estimate based on visible members
        size_t mem = sizeof(SortedHashGrid<T>);
        mem += subcell->getVertexCount() * sizeof(Vertex);
        mem += subcell->getCellCount() * (sizeof(uint64_t) + sizeof(Cell) + 10 * sizeof(uint32_t)); // estimate
        return mem;
    }
    
    std::string formatBytes(size_t bytes) const {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unitIndex = 0;
        double size = static_cast<double>(bytes);
        
        while (size >= 1024 && unitIndex < 3) {
            size /= 1024;
            unitIndex++;
        }
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
        return oss.str();
    }
    
    void collectHistogram(const std::unordered_map<uint64_t, Cell>& grid,
                         std::map<size_t, size_t>& histogram) const {
        for (const auto& [code, cell] : grid) {
            if (cell.subcell) {
                // For subdivided cells, we need to manually count
                std::map<size_t, size_t> subHistogram;
                collectSubcellHistogram(cell.subcell.get(), subHistogram);
                for (const auto& [items, count] : subHistogram) {
                    histogram[items] += count;
                }
            } else {
                histogram[cell.indices.size()]++;
            }
        }
    }
    
    void collectSubcellHistogram(const SortedHashGrid<T>* subcell,
                                 std::map<size_t, size_t>& histogram) const {
        if (!subcell) return;
        
        // We can only count based on what we can observe
        // This is an approximation since we can't access private grid_
        size_t cellCount = subcell->getCellCount();
        size_t vertexCount = subcell->getVertexCount();
        
        if (cellCount > 0) {
            size_t avgPerCell = vertexCount / cellCount;
            histogram[avgPerCell] += cellCount;
        }
    }
    
    void collectCellSizes(const std::unordered_map<uint64_t, Cell>& grid,
                         std::vector<size_t>& sizes) const {
        for (const auto& [code, cell] : grid) {
            if (cell.subcell) {
                // Approximation for subdivided cells
                size_t cellCount = cell.subcell->getCellCount();
                size_t vertexCount = cell.subcell->getVertexCount();
                if (cellCount > 0) {
                    size_t avgPerCell = vertexCount / cellCount;
                    for (size_t i = 0; i < cellCount; ++i) {
                        sizes.push_back(avgPerCell);
                    }
                }
            } else {
                sizes.push_back(cell.indices.size());
            }
        }
    }
    
    void analyzeGrid(const std::unordered_map<uint64_t, Cell>& grid, uint32_t level,
                    size_t& totalCells, size_t& maxCellSize, size_t& totalItems,
                    size_t& subdivCount, size_t& emptyCells, size_t& leafCells) const {
        totalCells += grid.size();
        
        for (const auto& [code, cell] : grid) {
            if (cell.subcell) {
                subdivCount++;
                // Approximate analysis for subcells
                size_t subCells = cell.subcell->getCellCount();
                size_t subVertices = cell.subcell->getVertexCount();
                totalCells += subCells;
                leafCells += subCells;
                totalItems += subVertices;
                if (subCells > 0) {
                    size_t avgPerCell = subVertices / subCells;
                    maxCellSize = std::max(maxCellSize, avgPerCell);
                }
            } else {
                leafCells++;
                if (cell.indices.empty()) {
                    emptyCells++;
                } else {
                    maxCellSize = std::max(maxCellSize, cell.indices.size());
                    totalItems += cell.indices.size();
                }
            }
        }
    }
};

} // namespace spatial
} // namespace tinyusdz