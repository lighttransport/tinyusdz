// Material JSON Viewer
// Displays Tydra converted MaterialX and UsdPreviewSurface material data

let currentMaterialData = null;
let currentActiveTab = 'openpbr';

// Syntax highlight JSON
export function syntaxHighlightJSON(json) {
    if (typeof json !== 'string') {
        json = JSON.stringify(json, null, 2);
    }

    // Replace special characters and add syntax highlighting
    json = json.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

    return json.replace(/("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?)/g, function (match) {
        let cls = 'json-number';
        if (/^"/.test(match)) {
            if (/:$/.test(match)) {
                cls = 'json-key';
            } else {
                cls = 'json-string';
            }
        } else if (/true|false/.test(match)) {
            cls = 'json-boolean';
        } else if (/null/.test(match)) {
            cls = 'json-null';
        }
        return '<span class="' + cls + '">' + match + '</span>';
    });
}

// Extract OpenPBR data from material
function extractOpenPBRData(materialData) {
    if (!materialData || !materialData.hasOpenPBR) {
        return null;
    }

    return {
        name: materialData.name,
        type: "OpenPBR Surface",
        hasOpenPBR: true,
        openPBR: materialData.openPBR
    };
}

// Extract UsdPreviewSurface data from material
function extractUsdPreviewSurfaceData(materialData) {
    if (!materialData || !materialData.hasUsdPreviewSurface) {
        return null;
    }

    return {
        name: materialData.name,
        type: "UsdPreviewSurface",
        hasUsdPreviewSurface: true,
        usdPreviewSurface: materialData.usdPreviewSurface
    };
}

// Extract Three.js material properties
function extractThreeMaterialData(threeMaterial) {
    if (!threeMaterial) {
        return null;
    }

    // Extract relevant properties
    const data = {
        type: threeMaterial.type,
        name: threeMaterial.name,
        uuid: threeMaterial.uuid,

        // Basic properties
        color: threeMaterial.color ? {
            r: threeMaterial.color.r,
            g: threeMaterial.color.g,
            b: threeMaterial.color.b
        } : undefined,

        // PBR properties
        metalness: threeMaterial.metalness,
        roughness: threeMaterial.roughness,
        ior: threeMaterial.ior,

        // Specular
        specularIntensity: threeMaterial.specularIntensity,
        specularColor: threeMaterial.specularColor ? {
            r: threeMaterial.specularColor.r,
            g: threeMaterial.specularColor.g,
            b: threeMaterial.specularColor.b
        } : undefined,

        // Transmission
        transmission: threeMaterial.transmission,
        attenuationColor: threeMaterial.attenuationColor ? {
            r: threeMaterial.attenuationColor.r,
            g: threeMaterial.attenuationColor.g,
            b: threeMaterial.attenuationColor.b
        } : undefined,
        attenuationDistance: threeMaterial.attenuationDistance,

        // Clearcoat
        clearcoat: threeMaterial.clearcoat,
        clearcoatRoughness: threeMaterial.clearcoatRoughness,

        // Sheen
        sheen: threeMaterial.sheen,
        sheenRoughness: threeMaterial.sheenRoughness,
        sheenColor: threeMaterial.sheenColor ? {
            r: threeMaterial.sheenColor.r,
            g: threeMaterial.sheenColor.g,
            b: threeMaterial.sheenColor.b
        } : undefined,

        // Iridescence
        iridescence: threeMaterial.iridescence,
        iridescenceIOR: threeMaterial.iridescenceIOR,
        iridescenceThicknessRange: threeMaterial.iridescenceThicknessRange,

        // Emission
        emissive: threeMaterial.emissive ? {
            r: threeMaterial.emissive.r,
            g: threeMaterial.emissive.g,
            b: threeMaterial.emissive.b
        } : undefined,
        emissiveIntensity: threeMaterial.emissiveIntensity,

        // Other
        opacity: threeMaterial.opacity,
        transparent: threeMaterial.transparent,
        side: threeMaterial.side,

        // Textures
        textures: {
            map: threeMaterial.map ? `Texture(${threeMaterial.map.id})` : null,
            normalMap: threeMaterial.normalMap ? `Texture(${threeMaterial.normalMap.id})` : null,
            roughnessMap: threeMaterial.roughnessMap ? `Texture(${threeMaterial.roughnessMap.id})` : null,
            metalnessMap: threeMaterial.metalnessMap ? `Texture(${threeMaterial.metalnessMap.id})` : null,
            emissiveMap: threeMaterial.emissiveMap ? `Texture(${threeMaterial.emissiveMap.id})` : null,
            aoMap: threeMaterial.aoMap ? `Texture(${threeMaterial.aoMap.id})` : null,
        },

        // Environment
        envMapIntensity: threeMaterial.envMapIntensity
    };

    // Remove undefined properties
    Object.keys(data).forEach(key => {
        if (data[key] === undefined) {
            delete data[key];
        }
    });

    if (data.textures) {
        Object.keys(data.textures).forEach(key => {
            if (data.textures[key] === null) {
                delete data.textures[key];
            }
        });
        if (Object.keys(data.textures).length === 0) {
            delete data.textures;
        }
    }

    return data;
}

// Show material JSON viewer
export function showMaterialJSON(material) {
    if (!material) {
        console.error('No material to display');
        return;
    }

    currentMaterialData = material;

    const wrapper = document.getElementById('material-json-wrapper');
    const title = document.getElementById('material-json-title');

    if (!wrapper) {
        console.error('Material JSON wrapper not found');
        return;
    }

    // Update title
    const materialName = material.name || 'Unknown Material';
    title.textContent = `Material Data - ${materialName}`;

    // Update all tab contents
    updateTabContent('openpbr', material);
    updateTabContent('usdpreview', material);
    updateTabContent('raw', material);
    updateTabContent('threejs', material);

    // Show wrapper
    wrapper.classList.add('visible');

    console.log('Material JSON viewer displayed');
}

// Hide material JSON viewer
export function hideMaterialJSON() {
    const wrapper = document.getElementById('material-json-wrapper');
    if (wrapper) {
        wrapper.classList.remove('visible');
    }
}

// Toggle material JSON visibility
export function toggleMaterialJSONVisibility() {
    const wrapper = document.getElementById('material-json-wrapper');
    if (!wrapper) return;

    if (wrapper.classList.contains('visible')) {
        hideMaterialJSON();
    } else {
        // Show JSON for currently selected material
        const selectedMaterial = window.selectedMaterialForExport;
        if (selectedMaterial) {
            showMaterialJSON(selectedMaterial);
        } else {
            alert('Please select a material from the Materials panel first');
        }
    }
}

// Update tab content
function updateTabContent(tabName, material) {
    const contentElement = document.getElementById(`json-content-${tabName}`);
    if (!contentElement) return;

    let data = null;
    let jsonString = '';

    switch (tabName) {
        case 'openpbr':
            data = extractOpenPBRData(material.data);
            if (data) {
                jsonString = syntaxHighlightJSON(data);
            } else {
                jsonString = '<span class="json-null">No OpenPBR data available for this material</span>';
            }
            break;

        case 'usdpreview':
            data = extractUsdPreviewSurfaceData(material.data);
            if (data) {
                jsonString = syntaxHighlightJSON(data);
            } else {
                jsonString = '<span class="json-null">No UsdPreviewSurface data available for this material</span>';
            }
            break;

        case 'raw':
            if (material.data) {
                jsonString = syntaxHighlightJSON(material.data);
            } else {
                jsonString = '<span class="json-null">No raw material data available</span>';
            }
            break;

        case 'threejs':
            data = extractThreeMaterialData(material.threeMaterial);
            if (data) {
                jsonString = syntaxHighlightJSON(data);
            } else {
                jsonString = '<span class="json-null">No Three.js material data available</span>';
            }
            break;
    }

    contentElement.innerHTML = `<pre>${jsonString}</pre>`;
}

// Switch between tabs
export function switchMaterialTab(tabName) {
    // Update tabs
    document.querySelectorAll('.material-json-tab').forEach(tab => {
        tab.classList.remove('active');
    });
    document.querySelector(`[data-tab="${tabName}"]`)?.classList.add('active');

    // Update content
    document.querySelectorAll('.material-json-content').forEach(content => {
        content.classList.remove('active');
    });
    document.getElementById(`json-content-${tabName}`)?.classList.add('active');

    currentActiveTab = tabName;
}

// Copy current tab JSON to clipboard
export function copyMaterialJSONToClipboard() {
    if (!currentMaterialData) {
        alert('No material data to copy');
        return;
    }

    let data = null;

    switch (currentActiveTab) {
        case 'openpbr':
            data = extractOpenPBRData(currentMaterialData.data);
            break;
        case 'usdpreview':
            data = extractUsdPreviewSurfaceData(currentMaterialData.data);
            break;
        case 'raw':
            data = currentMaterialData.data;
            break;
        case 'threejs':
            data = extractThreeMaterialData(currentMaterialData.threeMaterial);
            break;
    }

    if (!data) {
        alert('No data available for this tab');
        return;
    }

    const jsonString = JSON.stringify(data, null, 2);

    navigator.clipboard.writeText(jsonString).then(() => {
        console.log('Material JSON copied to clipboard');
        // Show temporary notification
        const btn = event.target;
        const originalText = btn.textContent;
        btn.textContent = '✓ Copied!';
        setTimeout(() => {
            btn.textContent = originalText;
        }, 2000);
    }).catch(err => {
        console.error('Failed to copy to clipboard:', err);
        alert('Failed to copy to clipboard. See console for details.');
    });
}

// Download current tab JSON
export function downloadMaterialJSONFile() {
    if (!currentMaterialData) {
        alert('No material data to download');
        return;
    }

    let data = null;
    let suffix = '';

    switch (currentActiveTab) {
        case 'openpbr':
            data = extractOpenPBRData(currentMaterialData.data);
            suffix = '_openpbr';
            break;
        case 'usdpreview':
            data = extractUsdPreviewSurfaceData(currentMaterialData.data);
            suffix = '_usdpreview';
            break;
        case 'raw':
            data = currentMaterialData.data;
            suffix = '_raw';
            break;
        case 'threejs':
            data = extractThreeMaterialData(currentMaterialData.threeMaterial);
            suffix = '_threejs';
            break;
    }

    if (!data) {
        alert('No data available for this tab');
        return;
    }

    const jsonString = JSON.stringify(data, null, 2);
    const blob = new Blob([jsonString], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `${currentMaterialData.name || 'material'}${suffix}.json`;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);

    console.log('Material JSON downloaded');
}

// Make functions globally accessible
if (typeof window !== 'undefined') {
    window.toggleMaterialJSON = toggleMaterialJSONVisibility;
    window.switchMaterialTab = switchMaterialTab;
    window.copyMaterialJSON = copyMaterialJSONToClipboard;
    window.downloadMaterialJSON = downloadMaterialJSONFile;
}
