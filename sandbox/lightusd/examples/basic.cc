// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Basic example

#include "lightusd/lightusd.hh"
#include <cstdio>

int main() {
    using namespace lightusd;

    printf("LightUSD version %s\n\n", version_string());

    // Create a stage
    Stage stage = Stage::create();
    stage.set_up_axis("Y");
    stage.set_meters_per_unit(0.01);
    stage.set_frames_per_second(24.0);

    // Create root prim
    Prim root("World", "Xform");

    // Create a mesh prim
    Prim mesh("Cube", "Mesh");

    // Add some attributes
    Attribute extent(TypeId::Float3);
    extent.set_default(Value::from_float3(1.0f, 1.0f, 1.0f));
    mesh.set_attribute("extent", std::move(extent));

    // Add animated attribute
    Attribute translate(TypeId::Double3);
    TimeSamples ts;
    ts.add_sample(0.0, Value::from_double3(0.0, 0.0, 0.0));
    ts.add_sample(24.0, Value::from_double3(10.0, 0.0, 0.0));
    ts.add_sample(48.0, Value::from_double3(10.0, 10.0, 0.0));
    translate.set_timesamples(std::move(ts));
    mesh.set_attribute("xformOp:translate", std::move(translate));

    // Add child to root
    root.add_child(std::move(mesh));

    // Add to stage
    stage.add_root_prim(std::move(root));
    stage.set_default_prim("World");
    stage.set_start_time_code(0.0);
    stage.set_end_time_code(48.0);

    // Export to USDA
    std::string usda = stage.to_usda();
    printf("Generated USDA:\n");
    printf("%s\n", usda.c_str());

    // Traverse the stage
    printf("\nTraversing stage:\n");
    stage.traverse([](const Prim& prim, int depth, void*) -> bool {
        for (int i = 0; i < depth; ++i) printf("  ");
        printf("/%s (%s)\n", prim.name().c_str(), prim.type_name().c_str());
        return true;  // Continue traversal
    }, nullptr);

    return 0;
}
