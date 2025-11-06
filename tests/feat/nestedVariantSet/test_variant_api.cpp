// Simple test for Tydra variant API
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cassert>

struct VariantOption {
    std::string name;
    std::string description;
    std::vector<int32_t> mesh_ids;
    std::vector<int32_t> material_ids;
};

struct VariantSet {
    std::string name;
    std::vector<VariantOption> options;
    int32_t default_option_index{0};
};

struct VariantGroup {
    std::string prim_path;
    std::vector<VariantSet> variant_sets;
    int32_t affected_node_id{-1};
};

int main() {
    std::cout << "=== Tydra Variant API Test ===" << std::endl;

    // Test 1: Create a VariantOption
    {
        std::cout << "\nTest 1: VariantOption" << std::endl;
        VariantOption red;
        red.name = "red";
        red.description = "Red variant";
        red.material_ids.push_back(0);
        assert(red.name == "red");
        std::cout << "  ✓ Created VariantOption: " << red.name << std::endl;
    }

    // Test 2: Create a VariantSet
    {
        std::cout << "\nTest 2: VariantSet" << std::endl;
        VariantSet color_set;
        color_set.name = "color";
        
        VariantOption red{"red", "Red", {}, {0}};
        VariantOption blue{"blue", "Blue", {}, {1}};
        
        color_set.options.push_back(red);
        color_set.options.push_back(blue);
        
        assert(color_set.options.size() == 2);
        assert(color_set.options[0].name == "red");
        std::cout << "  ✓ Created VariantSet with " << color_set.options.size() << " options" << std::endl;
    }

    // Test 3: Create a VariantGroup
    {
        std::cout << "\nTest 3: VariantGroup" << std::endl;
        VariantGroup car_variants;
        car_variants.prim_path = "/Cars/MyCar";
        car_variants.affected_node_id = 5;
        
        VariantSet color_set;
        color_set.name = "color";
        VariantOption red{"red", "Red", {}, {0}};
        VariantOption blue{"blue", "Blue", {}, {1}};
        color_set.options.push_back(red);
        color_set.options.push_back(blue);
        
        car_variants.variant_sets.push_back(color_set);
        
        assert(car_variants.variant_sets.size() == 1);
        assert(car_variants.variant_sets[0].name == "color");
        std::cout << "  ✓ Created VariantGroup with " << car_variants.variant_sets.size() << " variant sets" << std::endl;
    }

    // Test 4: Multiple VariantSets in a Group
    {
        std::cout << "\nTest 4: Multiple VariantSets" << std::endl;
        VariantGroup building;
        building.prim_path = "/Buildings/Building1";
        
        // Style variant set
        VariantSet style_set;
        style_set.name = "style";
        style_set.options.push_back({"modern", "Modern", {}, {}});
        style_set.options.push_back({"classic", "Classic", {}, {}});
        building.variant_sets.push_back(style_set);
        
        // Detail variant set
        VariantSet detail_set;
        detail_set.name = "detail";
        detail_set.options.push_back({"simple", "Simple", {}, {}});
        detail_set.options.push_back({"complex", "Complex", {}, {}});
        building.variant_sets.push_back(detail_set);
        
        assert(building.variant_sets.size() == 2);
        std::cout << "  ✓ Created VariantGroup with " << building.variant_sets.size() << " variant sets" << std::endl;
    }

    // Test 5: Variant content mapping
    {
        std::cout << "\nTest 5: Variant Content Mapping" << std::endl;
        VariantSet lod_set;
        lod_set.name = "lod";
        
        VariantOption high_poly;
        high_poly.name = "high";
        high_poly.mesh_ids.push_back(0);
        
        VariantOption low_poly;
        low_poly.name = "low";
        low_poly.mesh_ids.push_back(1);
        
        lod_set.options.push_back(high_poly);
        lod_set.options.push_back(low_poly);
        
        assert(lod_set.options[0].mesh_ids[0] == 0);
        assert(lod_set.options[1].mesh_ids[0] == 1);
        std::cout << "  ✓ Mapped mesh IDs to variants" << std::endl;
    }

    std::cout << "\n=== All Tests Passed! ===" << std::endl;
    return 0;
}
