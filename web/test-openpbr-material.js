// Test script for OpenPBR material serialization
// This demonstrates how to use the new getMaterial method with JSON and XML formats

// Assuming the TinyUSDZ module is loaded as Module

async function testOpenPBRSerialization() {
  // Create loader instance
  const loader = new Module.TinyUSDZLoaderNative();

  // Load a USD file with OpenPBR materials
  // (replace with actual USD binary data)
  const usdBinaryData = await fetchUSDFile('path/to/openpbr-material.usdz');

  // Load the USD file
  const success = loader.loadFromBinary(usdBinaryData, 'test.usdz');

  if (!success) {
    console.error('Failed to load USD file');
    return;
  }

  // Get material count
  const numMaterials = loader.numMaterials();
  console.log(`Number of materials: ${numMaterials}`);

  // Test different serialization formats
  for (let i = 0; i < numMaterials; i++) {
    console.log(`\n=== Material ${i} ===`);

    // 1. Get material using legacy format (backward compatibility)
    const legacyMaterial = loader.getMaterial(i);
    console.log('Legacy format:', legacyMaterial);

    // 2. Get material as JSON (includes OpenPBR data)
    const jsonResult = loader.getMaterialWithFormat(i, 'json');
    if (jsonResult.error) {
      console.error('JSON Error:', jsonResult.error);
    } else {
      console.log('JSON format:', jsonResult.format);
      const jsonData = JSON.parse(jsonResult.data);
      console.log('Parsed JSON:', jsonData);

      // Check what material types are available
      if (jsonData.hasOpenPBR) {
        console.log('Material has OpenPBR shader');
        console.log('OpenPBR base color:', jsonData.openPBR.base.color);
        console.log('OpenPBR emission:', jsonData.openPBR.emission);
      }

      if (jsonData.hasUsdPreviewSurface) {
        console.log('Material has UsdPreviewSurface shader');
      }
    }

    // 3. Get material as XML (MaterialX format)
    const xmlResult = loader.getMaterialWithFormat(i, 'xml');
    if (xmlResult.error) {
      console.error('XML Error:', xmlResult.error);
    } else {
      console.log('XML format:', xmlResult.format);
      console.log('MaterialX XML:\n', xmlResult.data);

      // You can parse the XML if needed
      const parser = new DOMParser();
      const xmlDoc = parser.parseFromString(xmlResult.data, 'text/xml');

      // Find all OpenPBR parameters
      const parameters = xmlDoc.getElementsByTagName('parameter');
      console.log(`Found ${parameters.length} OpenPBR parameters`);

      // Example: Get base_color parameter
      for (let param of parameters) {
        if (param.getAttribute('name') === 'base_color') {
          console.log('Base color value:', param.textContent);
          break;
        }
      }
    }
  }

  // Clean up
  loader.delete();
}

// Helper function to fetch USD file (implement as needed)
async function fetchUSDFile(url) {
  const response = await fetch(url);
  const arrayBuffer = await response.arrayBuffer();
  return new Uint8Array(arrayBuffer);
}

// Usage in Three.js context
function applyOpenPBRMaterialToThreeJS(loader, materialId, threeMesh) {
  // Get the material as JSON
  const result = loader.getMaterialWithFormat(materialId, 'json');

  if (result.error) {
    console.error('Failed to get material:', result.error);
    return;
  }

  const materialData = JSON.parse(result.data);

  if (!materialData.hasOpenPBR) {
    console.warn('Material does not have OpenPBR data');
    return;
  }

  const openPBR = materialData.openPBR;

  // Create Three.js material based on OpenPBR data
  // Note: This is a simplified example. Full implementation would need
  // to handle all OpenPBR parameters and texture references
  const material = new THREE.MeshPhysicalMaterial({
    // Base layer
    color: new THREE.Color(
      openPBR.base.color.value[0],
      openPBR.base.color.value[1],
      openPBR.base.color.value[2]
    ),
    metalness: openPBR.base.metalness.value,
    roughness: openPBR.base.roughness.value,

    // Transmission
    transmission: openPBR.transmission.weight.value,

    // Clear coat
    clearcoat: openPBR.coat.weight.value,
    clearcoatRoughness: openPBR.coat.roughness.value,

    // Sheen
    sheen: openPBR.sheen.weight.value,
    sheenColor: new THREE.Color(
      openPBR.sheen.color.value[0],
      openPBR.sheen.color.value[1],
      openPBR.sheen.color.value[2]
    ),
    sheenRoughness: openPBR.sheen.roughness.value,

    // Emission
    emissive: new THREE.Color(
      openPBR.emission.color.value[0],
      openPBR.emission.color.value[1],
      openPBR.emission.color.value[2]
    ),
    emissiveIntensity: openPBR.emission.luminance.value,

    // Geometry
    opacity: openPBR.geometry.opacity.value,
  });

  // Handle textures if present
  if (openPBR.base.color.textureId >= 0) {
    // Load and apply base color texture
    const textureData = loader.getTexture(openPBR.base.color.textureId);
    // ... convert to Three.js texture and apply
  }

  // Apply the material to the mesh
  threeMesh.material = material;
}

// Run the test
if (typeof Module !== 'undefined' && Module.TinyUSDZLoaderNative) {
  testOpenPBRSerialization().catch(console.error);
}