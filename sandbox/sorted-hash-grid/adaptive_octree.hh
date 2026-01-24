#pragma once

#include <vector>
#include <memory>
#include <array>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace tinyusdz {
namespace spatial {

template <typename T = float>
class AdaptiveOctree {
public:
    struct Point {
        T x, y, z;
        uint32_t id;
        T normal[3];  // Optional surface normal
        uint32_t surfaceId;  // Surface clustering ID
    };
    
    struct AABB {
        T min[3];
        T max[3];
        
        T center(int axis) const { return (min[axis] + max[axis]) * 0.5f; }
        T size(int axis) const { return max[axis] - min[axis]; }
        
        bool contains(T x, T y, T z) const {
            return x >= min[0] && x <= max[0] &&
                   y >= min[1] && y <= max[1] &&
                   z >= min[2] && z <= max[2];
        }
    };
    
    enum NodeType {
        EMPTY,
        LEAF,
        SURFACE,     // Planar surface node
        INTERIOR     // Has children
    };
    
    struct Surface {
        T normal[3];
        T d;  // Plane equation: nx*x + ny*y + nz*z + d = 0
        T thickness;
        std::vector<uint32_t> indices;
        
        T distanceToPoint(T x, T y, T z) const {
            return std::abs(normal[0]*x + normal[1]*y + normal[2]*z + d);
        }
    };
    
    struct Node {
        NodeType type = EMPTY;
        AABB bounds;
        std::vector<uint32_t> indices;
        std::unique_ptr<Surface> surface;
        std::array<std::unique_ptr<Node>, 8> children;
        uint32_t depth = 0;
        
        // Adaptive splitting criteria
        bool shouldSplit(size_t maxPoints, size_t maxDepth) const {
            if (type != LEAF || depth >= maxDepth) return false;
            if (indices.size() <= maxPoints) return false;
            
            // Don't split if points form a surface
            if (detectPlanarSurface()) return false;
            
            return true;
        }
        
        bool detectPlanarSurface() const {
            // Simplified plane detection using PCA or RANSAC
            // Returns true if >90% of points lie within threshold of a plane
            return false;  // Placeholder
        }
    };
    
    class SurfaceDetector {
    public:
        // RANSAC-based plane detection
        static bool detectPlane(const std::vector<Point>& points,
                               const std::vector<uint32_t>& indices,
                               T inlierThreshold,
                               T minInlierRatio,
                               Surface& outSurface) {
            if (indices.size() < 3) return false;
            
            const int maxIterations = 100;
            size_t bestInliers = 0;
            Surface bestSurface;
            
            std::srand(42);
            for (int iter = 0; iter < maxIterations; ++iter) {
                // Sample 3 random points
                uint32_t i1 = indices[rand() % indices.size()];
                uint32_t i2 = indices[rand() % indices.size()];
                uint32_t i3 = indices[rand() % indices.size()];
                
                const auto& p1 = points[i1];
                const auto& p2 = points[i2];
                const auto& p3 = points[i3];
                
                // Compute plane from 3 points
                T v1[3] = {p2.x - p1.x, p2.y - p1.y, p2.z - p1.z};
                T v2[3] = {p3.x - p1.x, p3.y - p1.y, p3.z - p1.z};
                
                // Cross product for normal
                T normal[3] = {
                    v1[1]*v2[2] - v1[2]*v2[1],
                    v1[2]*v2[0] - v1[0]*v2[2],
                    v1[0]*v2[1] - v1[1]*v2[0]
                };
                
                // Normalize
                T len = std::sqrt(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
                if (len < 1e-6) continue;
                
                normal[0] /= len;
                normal[1] /= len;
                normal[2] /= len;
                
                T d = -(normal[0]*p1.x + normal[1]*p1.y + normal[2]*p1.z);
                
                // Count inliers
                size_t inliers = 0;
                for (uint32_t idx : indices) {
                    const auto& p = points[idx];
                    T dist = std::abs(normal[0]*p.x + normal[1]*p.y + normal[2]*p.z + d);
                    if (dist <= inlierThreshold) {
                        inliers++;
                    }
                }
                
                if (inliers > bestInliers) {
                    bestInliers = inliers;
                    bestSurface.normal[0] = normal[0];
                    bestSurface.normal[1] = normal[1];
                    bestSurface.normal[2] = normal[2];
                    bestSurface.d = d;
                    bestSurface.thickness = inlierThreshold;
                }
            }
            
            T inlierRatio = static_cast<T>(bestInliers) / indices.size();
            if (inlierRatio >= minInlierRatio) {
                outSurface = bestSurface;
                
                // Collect actual inliers
                for (uint32_t idx : indices) {
                    const auto& p = points[idx];
                    T dist = outSurface.distanceToPoint(p.x, p.y, p.z);
                    if (dist <= inlierThreshold) {
                        outSurface.indices.push_back(idx);
                    }
                }
                return true;
            }
            
            return false;
        }
    };
    
private:
    std::vector<Point> points_;
    std::unique_ptr<Node> root_;
    size_t maxPointsPerLeaf_;
    size_t maxDepth_;
    T surfaceThreshold_;
    T minSurfaceInlierRatio_;
    
public:
    AdaptiveOctree(size_t maxPointsPerLeaf = 32,
                   size_t maxDepth = 10,
                   T surfaceThreshold = 0.01f,
                   T minSurfaceInlierRatio = 0.9f)
        : maxPointsPerLeaf_(maxPointsPerLeaf),
          maxDepth_(maxDepth),
          surfaceThreshold_(surfaceThreshold),
          minSurfaceInlierRatio_(minSurfaceInlierRatio) {}
    
    void addPoint(T x, T y, T z, uint32_t id) {
        points_.push_back({x, y, z, id, {0, 0, 0}, 0});
    }
    
    void build() {
        if (points_.empty()) return;
        
        // Compute bounds
        AABB bounds;
        bounds.min[0] = bounds.min[1] = bounds.min[2] = std::numeric_limits<T>::max();
        bounds.max[0] = bounds.max[1] = bounds.max[2] = std::numeric_limits<T>::lowest();
        
        for (const auto& p : points_) {
            bounds.min[0] = std::min(bounds.min[0], p.x);
            bounds.min[1] = std::min(bounds.min[1], p.y);
            bounds.min[2] = std::min(bounds.min[2], p.z);
            bounds.max[0] = std::max(bounds.max[0], p.x);
            bounds.max[1] = std::max(bounds.max[1], p.y);
            bounds.max[2] = std::max(bounds.max[2], p.z);
        }
        
        // Extend bounds slightly
        for (int i = 0; i < 3; ++i) {
            T extend = (bounds.max[i] - bounds.min[i]) * 0.01f;
            bounds.min[i] -= extend;
            bounds.max[i] += extend;
        }
        
        // Create root with all points
        root_ = std::make_unique<Node>();
        root_->type = LEAF;
        root_->bounds = bounds;
        root_->indices.reserve(points_.size());
        for (size_t i = 0; i < points_.size(); ++i) {
            root_->indices.push_back(i);
        }
        
        // Recursively build tree
        buildNode(root_.get());
    }
    
    std::vector<uint32_t> findNearby(T x, T y, T z, T radius) const {
        std::vector<uint32_t> results;
        if (!root_) return results;
        
        T radiusSq = radius * radius;
        searchNode(root_.get(), x, y, z, radiusSq, results);
        
        return results;
    }
    
    void getStatistics(size_t& nodeCount, size_t& leafCount, size_t& surfaceCount,
                      size_t& maxDepth, size_t& totalPoints) const {
        nodeCount = leafCount = surfaceCount = maxDepth = totalPoints = 0;
        if (root_) {
            collectStats(root_.get(), nodeCount, leafCount, surfaceCount, maxDepth, totalPoints);
        }
    }
    
private:
    void buildNode(Node* node) {
        if (!node || node->type == EMPTY) return;
        
        // Try to detect if this node contains a surface
        Surface surface;
        if (SurfaceDetector::detectPlane(points_, node->indices,
                                        surfaceThreshold_, minSurfaceInlierRatio_,
                                        surface)) {
            node->type = SURFACE;
            node->surface = std::make_unique<Surface>(std::move(surface));
            
            // Remove surface points from indices, keep outliers
            std::vector<uint32_t> outliers;
            for (uint32_t idx : node->indices) {
                bool onSurface = false;
                for (uint32_t surfIdx : node->surface->indices) {
                    if (idx == surfIdx) {
                        onSurface = true;
                        break;
                    }
                }
                if (!onSurface) {
                    outliers.push_back(idx);
                }
            }
            node->indices = std::move(outliers);
            
            // If we still have outliers and should split, continue
            if (node->indices.empty() || !node->shouldSplit(maxPointsPerLeaf_, maxDepth_)) {
                return;
            }
        }
        
        // Check if we should split this node
        if (!node->shouldSplit(maxPointsPerLeaf_, maxDepth_)) {
            return;
        }
        
        // Split into 8 octants
        node->type = INTERIOR;
        T cx = node->bounds.center(0);
        T cy = node->bounds.center(1);
        T cz = node->bounds.center(2);
        
        for (int i = 0; i < 8; ++i) {
            auto& child = node->children[i];
            child = std::make_unique<Node>();
            child->type = EMPTY;
            child->depth = node->depth + 1;
            
            // Compute child bounds
            child->bounds.min[0] = (i & 4) ? cx : node->bounds.min[0];
            child->bounds.min[1] = (i & 2) ? cy : node->bounds.min[1];
            child->bounds.min[2] = (i & 1) ? cz : node->bounds.min[2];
            child->bounds.max[0] = (i & 4) ? node->bounds.max[0] : cx;
            child->bounds.max[1] = (i & 2) ? node->bounds.max[1] : cy;
            child->bounds.max[2] = (i & 1) ? node->bounds.max[2] : cz;
        }
        
        // Distribute points to children
        for (uint32_t idx : node->indices) {
            const auto& p = points_[idx];
            int childIdx = 0;
            if (p.x > cx) childIdx |= 4;
            if (p.y > cy) childIdx |= 2;
            if (p.z > cz) childIdx |= 1;
            
            if (node->children[childIdx]->type == EMPTY) {
                node->children[childIdx]->type = LEAF;
            }
            node->children[childIdx]->indices.push_back(idx);
        }
        
        // Clear parent indices
        node->indices.clear();
        node->indices.shrink_to_fit();
        
        // Recursively build children
        for (auto& child : node->children) {
            if (child && child->type == LEAF) {
                buildNode(child.get());
            }
        }
    }
    
    void searchNode(const Node* node, T x, T y, T z, T radiusSq,
                   std::vector<uint32_t>& results) const {
        if (!node) return;
        
        // Check if search sphere intersects node bounds
        T closestPoint[3];
        for (int i = 0; i < 3; ++i) {
            T p = (i == 0) ? x : (i == 1) ? y : z;
            closestPoint[i] = std::max(node->bounds.min[i], std::min(p, node->bounds.max[i]));
        }
        
        T dx = closestPoint[0] - x;
        T dy = closestPoint[1] - y;
        T dz = closestPoint[2] - z;
        T distSq = dx*dx + dy*dy + dz*dz;
        
        if (distSq > radiusSq) return;
        
        switch (node->type) {
            case LEAF:
                for (uint32_t idx : node->indices) {
                    const auto& p = points_[idx];
                    T pdx = p.x - x;
                    T pdy = p.y - y;
                    T pdz = p.z - z;
                    T pDistSq = pdx*pdx + pdy*pdy + pdz*pdz;
                    if (pDistSq <= radiusSq) {
                        results.push_back(p.id);
                    }
                }
                break;
                
            case SURFACE:
                if (node->surface) {
                    // Check distance to plane
                    T planeDist = node->surface->distanceToPoint(x, y, z);
                    if (planeDist <= std::sqrt(radiusSq)) {
                        // Check points on surface
                        for (uint32_t idx : node->surface->indices) {
                            const auto& p = points_[idx];
                            T pdx = p.x - x;
                            T pdy = p.y - y;
                            T pdz = p.z - z;
                            T pDistSq = pdx*pdx + pdy*pdy + pdz*pdz;
                            if (pDistSq <= radiusSq) {
                                results.push_back(p.id);
                            }
                        }
                    }
                }
                // Also check outliers
                for (uint32_t idx : node->indices) {
                    const auto& p = points_[idx];
                    T pdx = p.x - x;
                    T pdy = p.y - y;
                    T pdz = p.z - z;
                    T pDistSq = pdx*pdx + pdy*pdy + pdz*pdz;
                    if (pDistSq <= radiusSq) {
                        results.push_back(p.id);
                    }
                }
                break;
                
            case INTERIOR:
                for (const auto& child : node->children) {
                    if (child) {
                        searchNode(child.get(), x, y, z, radiusSq, results);
                    }
                }
                break;
                
            default:
                break;
        }
    }
    
    void collectStats(const Node* node, size_t& nodeCount, size_t& leafCount,
                     size_t& surfaceCount, size_t& maxDepth, size_t& totalPoints) const {
        if (!node) return;
        
        nodeCount++;
        maxDepth = std::max(maxDepth, static_cast<size_t>(node->depth));
        
        switch (node->type) {
            case LEAF:
                leafCount++;
                totalPoints += node->indices.size();
                break;
            case SURFACE:
                surfaceCount++;
                if (node->surface) {
                    totalPoints += node->surface->indices.size();
                }
                totalPoints += node->indices.size();
                break;
            case INTERIOR:
                for (const auto& child : node->children) {
                    collectStats(child.get(), nodeCount, leafCount, surfaceCount, maxDepth, totalPoints);
                }
                break;
            default:
                break;
        }
    }
};

} // namespace spatial
} // namespace tinyusdz