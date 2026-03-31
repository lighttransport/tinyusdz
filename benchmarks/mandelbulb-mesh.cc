#include <cmath>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "ubench.h"
#include "value-types.hh"
#include "core/prim.hh"
#include "usdGeom.hh"
#include "tinyusdz.hh"
#include "usda-writer.hh"

using namespace tinyusdz;

struct MandelbulbGenerator {
    struct Vertex {
        value::point3f position;
        value::normal3f normal;
        
        bool operator==(const Vertex& other) const {
            return position[0] == other.position[0] && 
                   position[1] == other.position[1] && 
                   position[2] == other.position[2];
        }
    };
    
    struct VertexHasher {
        size_t operator()(const Vertex& v) const {
            size_t h1 = std::hash<float>{}(v.position[0]);
            size_t h2 = std::hash<float>{}(v.position[1]);
            size_t h3 = std::hash<float>{}(v.position[2]);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
    
    int resolution;
    int iterations;
    float power;
    float bailout;
    float threshold;
    
    MandelbulbGenerator(int res, int iter = 8, float pow = 8.0f, float bail = 2.0f, float thresh = 2.0f) 
        : resolution(res), iterations(iter), power(pow), bailout(bail), threshold(thresh) {}
    
    float mandelbulbDistance(const value::point3f& pos) {
        value::point3f z = pos;
        float dr = 1.0f;
        float r = 0.0f;
        
        for (int i = 0; i < iterations; i++) {
            r = sqrt(z[0]*z[0] + z[1]*z[1] + z[2]*z[2]);
            
            if (r > bailout) break;
            
            // Convert to spherical coordinates
            float theta = acos(z[2] / r);
            float phi = atan2(z[1], z[0]);
            
            // Derivative calculation
            dr = pow(r, power - 1.0f) * power * dr + 1.0f;
            
            // Scale and rotate the point
            float zr = pow(r, power);
            theta *= power;
            phi *= power;
            
            // Convert back to cartesian
            z[0] = zr * sin(theta) * cos(phi) + pos[0];
            z[1] = zr * sin(theta) * sin(phi) + pos[1];
            z[2] = zr * cos(theta) + pos[2];
        }
        
        return 0.5f * log(r) * r / dr;
    }
    
    value::normal3f calculateNormal(const value::point3f& pos) {
        const float epsilon = 0.001f;
        value::normal3f normal;
        
        normal[0] = mandelbulbDistance({pos[0] + epsilon, pos[1], pos[2]}) - 
                   mandelbulbDistance({pos[0] - epsilon, pos[1], pos[2]});
        normal[1] = mandelbulbDistance({pos[0], pos[1] + epsilon, pos[2]}) - 
                   mandelbulbDistance({pos[0], pos[1] - epsilon, pos[2]});
        normal[2] = mandelbulbDistance({pos[0], pos[1], pos[2] + epsilon}) - 
                   mandelbulbDistance({pos[0], pos[1], pos[2] - epsilon});
        
        float length = sqrt(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
        if (length > 0.0f) {
            normal[0] /= length;
            normal[1] /= length;
            normal[2] /= length;
        }
        
        return normal;
    }
    
    // Marching cubes implementation
    GeomMesh generateMesh() {
        GeomMesh mesh;
        
        std::vector<value::point3f> vertices;
        std::vector<value::normal3f> normals;
        std::vector<int> faceVertexCounts;
        std::vector<int> faceVertexIndices;
        
        std::unordered_map<Vertex, int, VertexHasher> vertexMap;
        int vertexIndex = 0;
        
        float step = 4.0f / resolution; // Cover range from -2 to 2
        
        // Simple isosurface extraction using marching cubes concept
        for (int x = 0; x < resolution - 1; x++) {
            for (int y = 0; y < resolution - 1; y++) {
                for (int z = 0; z < resolution - 1; z++) {
                    float px = -2.0f + x * step;
                    float py = -2.0f + y * step;
                    float pz = -2.0f + z * step;
                    
                    // Sample 8 corners of the cube
                    float samples[8];
                    value::point3f corners[8] = {
                        {px, py, pz},
                        {px + step, py, pz},
                        {px + step, py + step, pz},
                        {px, py + step, pz},
                        {px, py, pz + step},
                        {px + step, py, pz + step},
                        {px + step, py + step, pz + step},
                        {px, py + step, pz + step}
                    };
                    
                    for (int i = 0; i < 8; i++) {
                        samples[i] = mandelbulbDistance(corners[i]);
                    }
                    
                    // Simple triangle extraction when surface crosses cube
                    int config = 0;
                    for (int i = 0; i < 8; i++) {
                        if (samples[i] < 0.01f) config |= (1 << i);
                    }
                    
                    if (config != 0 && config != 255) {
                        // Surface intersection found - add triangles
                        // Simplified: add center point triangulated with edges
                        value::point3f center = {px + step*0.5f, py + step*0.5f, pz + step*0.5f};
                        
                        if (mandelbulbDistance(center) < 0.05f) {
                            Vertex centerVertex;
                            centerVertex.position = center;
                            centerVertex.normal = calculateNormal(center);
                            
                            if (vertexMap.find(centerVertex) == vertexMap.end()) {
                                vertexMap[centerVertex] = vertexIndex++;
                                vertices.push_back(centerVertex.position);
                                normals.push_back(centerVertex.normal);
                            }
                            int centerIdx = vertexMap[centerVertex];
                            
                            // Create triangles with nearby vertices
                            for (int i = 0; i < 8; i++) {
                                if (samples[i] < 0.05f) {
                                    Vertex v;
                                    v.position = corners[i];
                                    v.normal = calculateNormal(corners[i]);
                                    
                                    if (vertexMap.find(v) == vertexMap.end()) {
                                        vertexMap[v] = vertexIndex++;
                                        vertices.push_back(v.position);
                                        normals.push_back(v.normal);
                                    }
                                    int vIdx = vertexMap[v];
                                    
                                    // Create triangle with next corner
                                    int nextI = (i + 1) % 8;
                                    if (samples[nextI] < 0.05f) {
                                        Vertex nextV;
                                        nextV.position = corners[nextI];
                                        nextV.normal = calculateNormal(corners[nextI]);
                                        
                                        if (vertexMap.find(nextV) == vertexMap.end()) {
                                            vertexMap[nextV] = vertexIndex++;
                                            vertices.push_back(nextV.position);
                                            normals.push_back(nextV.normal);
                                        }
                                        int nextVIdx = vertexMap[nextV];
                                        
                                        faceVertexCounts.push_back(3);
                                        faceVertexIndices.push_back(centerIdx);
                                        faceVertexIndices.push_back(vIdx);
                                        faceVertexIndices.push_back(nextVIdx);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Set mesh data
        mesh.points.set_value(vertices);
        mesh.normals.set_value(normals);
        mesh.faceVertexCounts.set_value(faceVertexCounts);
        mesh.faceVertexIndices.set_value(faceVertexIndices);
        
        return mesh;
    }
};

// Benchmarks
UBENCH(mandelbulb, generate_lod_16) {
    MandelbulbGenerator generator(16);
    GeomMesh mesh = generator.generateMesh();
    (void)mesh; // Suppress unused variable warning
}

UBENCH(mandelbulb, generate_lod_32) {
    MandelbulbGenerator generator(32);
    GeomMesh mesh = generator.generateMesh();
    (void)mesh;
}

UBENCH(mandelbulb, generate_lod_64) {
    MandelbulbGenerator generator(64);
    GeomMesh mesh = generator.generateMesh();
    (void)mesh;
}

UBENCH(mandelbulb, create_usd_stage_lod_32) {
    MandelbulbGenerator generator(32);
    GeomMesh mesh = generator.generateMesh();
    
    // Create USD stage and add mesh
    Stage stage;
    
    // Create Xform and Mesh prims
    Xform xform;
    xform.name = "root";
    
    mesh.name = "mandelbulb";
    
    Prim meshPrim(mesh);
    Prim xformPrim(xform);
    
    // Add mesh as child of xform
    xformPrim.children().emplace_back(std::move(meshPrim));
    stage.root_prims().emplace_back(std::move(xformPrim));
}

UBENCH(mandelbulb, write_usd_file_lod_16) {
    MandelbulbGenerator generator(16);
    GeomMesh mesh = generator.generateMesh();
    
    Stage stage;
    
    // Create Xform and Mesh prims
    Xform xform;
    xform.name = "root";
    
    mesh.name = "mandelbulb";
    
    Prim meshPrim(mesh);
    Prim xformPrim(xform);
    
    // Add mesh as child of xform
    xformPrim.children().emplace_back(std::move(meshPrim));
    stage.root_prims().emplace_back(std::move(xformPrim));
    
    // Write to file
    std::string warn, err;
    tinyusdz::usda::SaveAsUSDA("mandelbulb_lod16.usda", stage, &warn, &err);
}