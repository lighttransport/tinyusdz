#!/usr/bin/env python3
"""
Blender Python script to generate test materials with chained shader nodes.
Run this script in Blender to create test USD files for the NodeGraph optimizer.

Usage:
  1. Open Blender 4.5+
  2. Run this script in the Text Editor or via: blender --background --python blender_chain_test_generator.py
  3. Export the scene as USD with MaterialX enabled
"""

import bpy
import math

def clear_scene():
    """Clear all objects and materials from the scene"""
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

    for mat in bpy.data.materials:
        bpy.data.materials.remove(mat)

def create_test_cube(name, location):
    """Create a cube with a given name and location"""
    bpy.ops.mesh.primitive_cube_add(location=location)
    cube = bpy.context.active_object
    cube.name = name
    return cube

def create_material(name):
    """Create a new material with nodes enabled"""
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    return mat

def get_node_tree(mat):
    """Get the node tree from a material"""
    return mat.node_tree

def clear_nodes(node_tree):
    """Remove all nodes except output"""
    nodes = node_tree.nodes
    for node in list(nodes):
        if node.type != 'OUTPUT_MATERIAL':
            nodes.remove(node)

def create_chain_invert_invert(mat, depth=2):
    """
    Create a chain of Invert nodes (double invert = identity)
    Invert -> Invert -> ... -> Base Color
    """
    tree = get_node_tree(mat)
    clear_nodes(tree)
    nodes = tree.nodes
    links = tree.links

    # Get the output node
    output = None
    for node in nodes:
        if node.type == 'OUTPUT_MATERIAL':
            output = node
            break

    # Create Principled BSDF
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (0, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    # Create RGB node as input
    rgb = nodes.new('ShaderNodeRGB')
    rgb.location = (-600 - depth * 200, 0)
    rgb.outputs[0].default_value = (0.8, 0.2, 0.1, 1.0)  # Red-ish color

    # Create chain of Invert nodes
    prev_output = rgb.outputs[0]
    for i in range(depth):
        invert = nodes.new('ShaderNodeInvert')
        invert.location = (-400 - (depth - i - 1) * 200, 0)
        invert.name = f"Invert_{i+1}"
        links.new(prev_output, invert.inputs['Color'])
        prev_output = invert.outputs['Color']

    # Connect to BSDF
    links.new(prev_output, bsdf.inputs['Base Color'])

def create_chain_gamma_inverse(mat, depth=2):
    """
    Create a chain of Gamma nodes with inverse values
    Gamma(2.2) -> Gamma(1/2.2) = identity
    """
    tree = get_node_tree(mat)
    clear_nodes(tree)
    nodes = tree.nodes
    links = tree.links

    output = None
    for node in nodes:
        if node.type == 'OUTPUT_MATERIAL':
            output = node
            break

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (0, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    rgb = nodes.new('ShaderNodeRGB')
    rgb.location = (-600 - depth * 200, 0)
    rgb.outputs[0].default_value = (0.5, 0.7, 0.3, 1.0)

    prev_output = rgb.outputs[0]
    gamma_values = [2.2, 1/2.2] * (depth // 2) + ([2.2] if depth % 2 else [])

    for i, gamma in enumerate(gamma_values[:depth]):
        gamma_node = nodes.new('ShaderNodeGamma')
        gamma_node.location = (-400 - (depth - i - 1) * 200, 0)
        gamma_node.name = f"Gamma_{i+1}"
        gamma_node.inputs['Gamma'].default_value = gamma
        links.new(prev_output, gamma_node.inputs['Color'])
        prev_output = gamma_node.outputs['Color']

    links.new(prev_output, bsdf.inputs['Base Color'])

def create_chain_separate_combine(mat, depth=2):
    """
    Create chains of Separate RGB -> Combine RGB (passthrough)
    """
    tree = get_node_tree(mat)
    clear_nodes(tree)
    nodes = tree.nodes
    links = tree.links

    output = None
    for node in nodes:
        if node.type == 'OUTPUT_MATERIAL':
            output = node
            break

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (0, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    rgb = nodes.new('ShaderNodeRGB')
    rgb.location = (-600 - depth * 400, 0)
    rgb.outputs[0].default_value = (0.3, 0.6, 0.9, 1.0)

    prev_output = rgb.outputs[0]
    for i in range(depth):
        sep = nodes.new('ShaderNodeSeparateColor')
        sep.location = (-400 - (depth - i - 1) * 400, 0)
        sep.name = f"Separate_{i+1}"
        links.new(prev_output, sep.inputs['Color'])

        comb = nodes.new('ShaderNodeCombineColor')
        comb.location = (-200 - (depth - i - 1) * 400, 0)
        comb.name = f"Combine_{i+1}"
        links.new(sep.outputs['Red'], comb.inputs['Red'])
        links.new(sep.outputs['Green'], comb.inputs['Green'])
        links.new(sep.outputs['Blue'], comb.inputs['Blue'])

        prev_output = comb.outputs['Color']

    links.new(prev_output, bsdf.inputs['Base Color'])

def create_chain_multiply(mat, depth=4):
    """
    Create a chain of multiply nodes that cancel out
    multiply(2) -> multiply(0.5) -> multiply(3) -> multiply(1/3) = identity
    """
    tree = get_node_tree(mat)
    clear_nodes(tree)
    nodes = tree.nodes
    links = tree.links

    output = None
    for node in nodes:
        if node.type == 'OUTPUT_MATERIAL':
            output = node
            break

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (0, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    rgb = nodes.new('ShaderNodeRGB')
    rgb.location = (-600 - depth * 200, 0)
    rgb.outputs[0].default_value = (0.4, 0.5, 0.6, 1.0)

    # Create multiply factors that cancel out: [2, 0.5, 3, 1/3, 4, 0.25, ...]
    factors = []
    for i in range(depth):
        if i % 2 == 0:
            factors.append(float(i + 2))
        else:
            factors.append(1.0 / float(i + 1))

    prev_output = rgb.outputs[0]
    for i, factor in enumerate(factors):
        mix = nodes.new('ShaderNodeMixRGB')
        mix.blend_type = 'MULTIPLY'
        mix.location = (-400 - (depth - i - 1) * 200, 0)
        mix.name = f"Multiply_{i+1}"
        mix.inputs['Fac'].default_value = 1.0
        mix.inputs['Color2'].default_value = (factor, factor, factor, 1.0)
        links.new(prev_output, mix.inputs['Color1'])
        prev_output = mix.outputs['Color']

    links.new(prev_output, bsdf.inputs['Base Color'])

def create_chain_add_subtract(mat, depth=4):
    """
    Create a chain of add/subtract that cancels out
    add(0.2) -> subtract(0.2) -> add(0.3) -> subtract(0.3) = identity
    """
    tree = get_node_tree(mat)
    clear_nodes(tree)
    nodes = tree.nodes
    links = tree.links

    output = None
    for node in nodes:
        if node.type == 'OUTPUT_MATERIAL':
            output = node
            break

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (0, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    rgb = nodes.new('ShaderNodeRGB')
    rgb.location = (-600 - depth * 200, 0)
    rgb.outputs[0].default_value = (0.5, 0.5, 0.5, 1.0)

    prev_output = rgb.outputs[0]
    for i in range(depth):
        offset = (i // 2 + 1) * 0.1

        if i % 2 == 0:
            # Add
            mix = nodes.new('ShaderNodeMixRGB')
            mix.blend_type = 'ADD'
            mix.name = f"Add_{i//2 + 1}"
            mix.inputs['Color2'].default_value = (offset, offset, offset, 1.0)
        else:
            # Subtract
            mix = nodes.new('ShaderNodeMixRGB')
            mix.blend_type = 'SUBTRACT'
            mix.name = f"Subtract_{i//2 + 1}"
            mix.inputs['Color2'].default_value = (offset, offset, offset, 1.0)

        mix.location = (-400 - (depth - i - 1) * 200, 0)
        mix.inputs['Fac'].default_value = 1.0
        links.new(prev_output, mix.inputs['Color1'])
        prev_output = mix.outputs['Color']

    links.new(prev_output, bsdf.inputs['Base Color'])

def create_chain_hue_saturation(mat, depth=4):
    """
    Create a chain of Hue/Saturation/Value nodes
    """
    tree = get_node_tree(mat)
    clear_nodes(tree)
    nodes = tree.nodes
    links = tree.links

    output = None
    for node in nodes:
        if node.type == 'OUTPUT_MATERIAL':
            output = node
            break

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (0, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    rgb = nodes.new('ShaderNodeRGB')
    rgb.location = (-600 - depth * 200, 0)
    rgb.outputs[0].default_value = (0.8, 0.4, 0.2, 1.0)

    prev_output = rgb.outputs[0]
    for i in range(depth):
        hsv = nodes.new('ShaderNodeHueSaturation')
        hsv.location = (-400 - (depth - i - 1) * 200, 0)
        hsv.name = f"HSV_{i+1}"
        # Alternate between slight adjustments that should compound
        hsv.inputs['Hue'].default_value = 0.5 + (0.1 if i % 2 == 0 else -0.1)
        hsv.inputs['Saturation'].default_value = 1.0
        hsv.inputs['Value'].default_value = 1.0
        links.new(prev_output, hsv.inputs['Color'])
        prev_output = hsv.outputs['Color']

    links.new(prev_output, bsdf.inputs['Base Color'])

def create_deep_chain_mixed(mat, depth=16):
    """
    Create a deep chain mixing multiple operations (up to 16 nodes)
    """
    tree = get_node_tree(mat)
    clear_nodes(tree)
    nodes = tree.nodes
    links = tree.links

    output = None
    for node in nodes:
        if node.type == 'OUTPUT_MATERIAL':
            output = node
            break

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (0, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    rgb = nodes.new('ShaderNodeRGB')
    rgb.location = (-600 - depth * 200, 0)
    rgb.outputs[0].default_value = (0.6, 0.4, 0.8, 1.0)

    prev_output = rgb.outputs[0]

    # Mix of operations that should partially cancel
    operations = ['invert', 'gamma', 'invert', 'gamma_inv', 'multiply', 'divide',
                  'add', 'subtract', 'invert', 'invert', 'gamma', 'gamma_inv',
                  'multiply', 'divide', 'add', 'subtract']

    for i in range(min(depth, len(operations))):
        op = operations[i]
        x_loc = -400 - (depth - i - 1) * 200

        if op == 'invert':
            node = nodes.new('ShaderNodeInvert')
            node.name = f"Invert_{i+1}"
            links.new(prev_output, node.inputs['Color'])
            prev_output = node.outputs['Color']
        elif op == 'gamma':
            node = nodes.new('ShaderNodeGamma')
            node.name = f"Gamma_{i+1}"
            node.inputs['Gamma'].default_value = 2.2
            links.new(prev_output, node.inputs['Color'])
            prev_output = node.outputs['Color']
        elif op == 'gamma_inv':
            node = nodes.new('ShaderNodeGamma')
            node.name = f"GammaInv_{i+1}"
            node.inputs['Gamma'].default_value = 1/2.2
            links.new(prev_output, node.inputs['Color'])
            prev_output = node.outputs['Color']
        elif op == 'multiply':
            node = nodes.new('ShaderNodeMixRGB')
            node.blend_type = 'MULTIPLY'
            node.name = f"Multiply_{i+1}"
            node.inputs['Fac'].default_value = 1.0
            node.inputs['Color2'].default_value = (2.0, 2.0, 2.0, 1.0)
            links.new(prev_output, node.inputs['Color1'])
            prev_output = node.outputs['Color']
        elif op == 'divide':
            node = nodes.new('ShaderNodeMixRGB')
            node.blend_type = 'DIVIDE'
            node.name = f"Divide_{i+1}"
            node.inputs['Fac'].default_value = 1.0
            node.inputs['Color2'].default_value = (2.0, 2.0, 2.0, 1.0)
            links.new(prev_output, node.inputs['Color1'])
            prev_output = node.outputs['Color']
        elif op == 'add':
            node = nodes.new('ShaderNodeMixRGB')
            node.blend_type = 'ADD'
            node.name = f"Add_{i+1}"
            node.inputs['Fac'].default_value = 1.0
            node.inputs['Color2'].default_value = (0.2, 0.2, 0.2, 1.0)
            links.new(prev_output, node.inputs['Color1'])
            prev_output = node.outputs['Color']
        elif op == 'subtract':
            node = nodes.new('ShaderNodeMixRGB')
            node.blend_type = 'SUBTRACT'
            node.name = f"Subtract_{i+1}"
            node.inputs['Fac'].default_value = 1.0
            node.inputs['Color2'].default_value = (0.2, 0.2, 0.2, 1.0)
            links.new(prev_output, node.inputs['Color1'])
            prev_output = node.outputs['Color']

        node.location = (x_loc, 0)

    links.new(prev_output, bsdf.inputs['Base Color'])

def main():
    """Main function to create all test materials"""
    clear_scene()

    # Create test cubes with different chained materials
    test_configs = [
        ("Chain_DoubleInvert", create_chain_invert_invert, 2),
        ("Chain_QuadInvert", create_chain_invert_invert, 4),
        ("Chain_GammaInverse", create_chain_gamma_inverse, 2),
        ("Chain_GammaInverse4", create_chain_gamma_inverse, 4),
        ("Chain_SeparateCombine", create_chain_separate_combine, 2),
        ("Chain_SeparateCombine4", create_chain_separate_combine, 4),
        ("Chain_Multiply4", create_chain_multiply, 4),
        ("Chain_AddSubtract", create_chain_add_subtract, 4),
        ("Chain_HSV4", create_chain_hue_saturation, 4),
        ("Chain_Deep16", create_deep_chain_mixed, 16),
    ]

    for i, (name, func, depth) in enumerate(test_configs):
        row = i // 5
        col = i % 5
        cube = create_test_cube(name, (col * 3, row * 3, 0))
        mat = create_material(name)
        func(mat, depth)
        cube.data.materials.append(mat)
        print(f"Created: {name} with depth {depth}")

    print("\nAll test materials created!")
    print("Export with: File -> Export -> Universal Scene Description (.usd)")
    print("Enable 'MaterialX' in export options")

if __name__ == "__main__":
    main()
