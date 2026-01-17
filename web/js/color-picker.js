// Color Picker - Pick color values from rendered framebuffer
// Reads pixel values directly from WebGL renderer

let colorPickerActive = false;
let colorPickerRenderer = null;
let lastPickedColor = null;

// sRGB to Linear conversion (for color space display)
function sRGBToLinear(value) {
    if (value <= 0.04045) {
        return value / 12.92;
    } else {
        return Math.pow((value + 0.055) / 1.055, 2.4);
    }
}

// Linear to sRGB conversion
function linearToSRGB(value) {
    if (value <= 0.0031308) {
        return value * 12.92;
    } else {
        return 1.055 * Math.pow(value, 1.0 / 2.4) - 0.055;
    }
}

// Convert RGB to Hex
function rgbToHex(r, g, b) {
    const toHex = (n) => {
        const hex = Math.round(n).toString(16).padStart(2, '0');
        return hex;
    };
    return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
}

// Initialize color picker system
export function initializeColorPicker(renderer) {
    colorPickerRenderer = renderer;
    console.log('Color picker initialized');
}

// Toggle color picker mode
export function toggleColorPickerMode() {
    colorPickerActive = !colorPickerActive;

    const panel = document.getElementById('color-picker-panel');
    const button = document.getElementById('color-picker-btn');
    const body = document.body;

    if (colorPickerActive) {
        // Enable picker mode
        panel.classList.add('active');
        button.classList.add('active');
        body.classList.add('color-picker-mode');
        console.log('Color picker mode: ON');
    } else {
        // Disable picker mode
        panel.classList.remove('active');
        button.classList.remove('active');
        body.classList.remove('color-picker-mode');
        console.log('Color picker mode: OFF');
    }
}

// Check if color picker is active
export function isColorPickerActive() {
    return colorPickerActive;
}

// Pick color at mouse position
export function pickColorAtPosition(x, y, renderer) {
    if (!renderer) {
        console.error('No renderer provided for color picking');
        return null;
    }

    // Get renderer size
    const width = renderer.domElement.width;
    const height = renderer.domElement.height;

    // Convert mouse coordinates to WebGL coordinates
    // WebGL origin is bottom-left, mouse origin is top-left
    const pixelX = Math.floor(x);
    const pixelY = Math.floor(height - y); // Flip Y coordinate

    // Clamp to valid range
    const clampedX = Math.max(0, Math.min(width - 1, pixelX));
    const clampedY = Math.max(0, Math.min(height - 1, pixelY));

    // Read pixel from framebuffer
    const pixelBuffer = new Uint8Array(4);
    const gl = renderer.getContext();

    try {
        // Read pixel at position
        gl.readPixels(
            clampedX,
            clampedY,
            1, // width
            1, // height
            gl.RGBA,
            gl.UNSIGNED_BYTE,
            pixelBuffer
        );

        // Extract RGBA values (0-255)
        const r = pixelBuffer[0];
        const g = pixelBuffer[1];
        const b = pixelBuffer[2];
        const a = pixelBuffer[3];

        // Convert to float (0-1)
        const rf = r / 255.0;
        const gf = g / 255.0;
        const bf = b / 255.0;
        const af = a / 255.0;

        // Convert to linear space (assuming sRGB framebuffer)
        const rLinear = sRGBToLinear(rf);
        const gLinear = sRGBToLinear(gf);
        const bLinear = sRGBToLinear(bf);

        const colorData = {
            // Integer RGB (0-255)
            rgb: { r, g, b, a },

            // Float RGB (0-1)
            float: { r: rf, g: gf, b: bf, a: af },

            // Linear RGB (0-1)
            linear: { r: rLinear, g: gLinear, b: bLinear, a: af },

            // Hex color
            hex: rgbToHex(r, g, b),

            // Position
            position: { x: clampedX, y: clampedY }
        };

        return colorData;
    } catch (error) {
        console.error('Error reading pixel:', error);
        return null;
    }
}

// Display picked color in UI
export function displayPickedColor(colorData, mouseX, mouseY) {
    if (!colorData) return;

    lastPickedColor = colorData;

    // Update color swatch
    const swatch = document.getElementById('color-swatch');
    if (swatch) {
        swatch.style.backgroundColor = colorData.hex;
    }

    // Update RGB value (0-255)
    const rgbElement = document.getElementById('color-rgb');
    if (rgbElement) {
        rgbElement.textContent = `${colorData.rgb.r}, ${colorData.rgb.g}, ${colorData.rgb.b}`;
    }

    // Update Hex value
    const hexElement = document.getElementById('color-hex');
    if (hexElement) {
        hexElement.textContent = colorData.hex.toUpperCase();
    }

    // Update Float value (0-1, sRGB)
    const floatElement = document.getElementById('color-float');
    if (floatElement) {
        const r = colorData.float.r.toFixed(4);
        const g = colorData.float.g.toFixed(4);
        const b = colorData.float.b.toFixed(4);
        floatElement.textContent = `${r}, ${g}, ${b}`;
    }

    // Update Linear value (0-1, linear RGB)
    const linearElement = document.getElementById('color-linear');
    if (linearElement) {
        const r = colorData.linear.r.toFixed(4);
        const g = colorData.linear.g.toFixed(4);
        const b = colorData.linear.b.toFixed(4);
        linearElement.textContent = `${r}, ${g}, ${b}`;
    }

    // Update position
    const positionElement = document.getElementById('color-position');
    if (positionElement) {
        positionElement.textContent = `(${mouseX}, ${mouseY}) → (${colorData.position.x}, ${colorData.position.y})`;
    }

    console.log('Picked color:', colorData);
}

// Handle click for color picking
export function handleColorPickerClick(event, renderer) {
    if (!colorPickerActive || !renderer) return false;

    // Get mouse position relative to canvas
    const rect = renderer.domElement.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;

    // Convert to device pixels
    const dpr = window.devicePixelRatio || 1;
    const canvasX = x * dpr;
    const canvasY = y * dpr;

    // Pick color at position
    const colorData = pickColorAtPosition(canvasX, canvasY, renderer);

    if (colorData) {
        displayPickedColor(colorData, Math.floor(x), Math.floor(y));
        return true; // Event handled
    }

    return false;
}

// Copy color value to clipboard
export function copyColorValueToClipboard(format) {
    if (!lastPickedColor) {
        alert('No color picked yet');
        return;
    }

    let textToCopy = '';

    switch (format) {
        case 'rgb':
            textToCopy = `${lastPickedColor.rgb.r}, ${lastPickedColor.rgb.g}, ${lastPickedColor.rgb.b}`;
            break;
        case 'hex':
            textToCopy = lastPickedColor.hex.toUpperCase();
            break;
        case 'float':
            const r = lastPickedColor.float.r.toFixed(4);
            const g = lastPickedColor.float.g.toFixed(4);
            const b = lastPickedColor.float.b.toFixed(4);
            textToCopy = `${r}, ${g}, ${b}`;
            break;
        case 'linear':
            const rl = lastPickedColor.linear.r.toFixed(4);
            const gl = lastPickedColor.linear.g.toFixed(4);
            const bl = lastPickedColor.linear.b.toFixed(4);
            textToCopy = `${rl}, ${gl}, ${bl}`;
            break;
        default:
            console.error('Unknown format:', format);
            return;
    }

    navigator.clipboard.writeText(textToCopy).then(() => {
        console.log(`Copied ${format}:`, textToCopy);

        // Show feedback
        const btn = event.target;
        const originalText = btn.textContent;
        btn.textContent = '✓ Copied!';
        setTimeout(() => {
            btn.textContent = originalText;
        }, 1500);
    }).catch(err => {
        console.error('Failed to copy:', err);
        alert('Failed to copy to clipboard');
    });
}

// Get last picked color data
export function getLastPickedColor() {
    return lastPickedColor;
}

// Reset color picker state
export function resetColorPicker() {
    colorPickerActive = false;
    lastPickedColor = null;

    const panel = document.getElementById('color-picker-panel');
    const button = document.getElementById('color-picker-btn');
    const body = document.body;

    if (panel) panel.classList.remove('active');
    if (button) button.classList.remove('active');
    if (body) body.classList.remove('color-picker-mode');
}

// Export color data as JSON
export function exportColorData() {
    if (!lastPickedColor) {
        alert('No color picked yet');
        return;
    }

    const exportData = {
        timestamp: new Date().toISOString(),
        color: {
            rgb_8bit: lastPickedColor.rgb,
            rgb_float: lastPickedColor.float,
            rgb_linear: lastPickedColor.linear,
            hex: lastPickedColor.hex
        },
        position: lastPickedColor.position
    };

    const jsonString = JSON.stringify(exportData, null, 2);
    const blob = new Blob([jsonString], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `picked_color_${Date.now()}.json`;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);

    console.log('Color data exported');
}

// Make functions globally accessible
if (typeof window !== 'undefined') {
    window.toggleColorPicker = toggleColorPickerMode;
    window.copyColorValue = copyColorValueToClipboard;
    window.exportColorData = exportColorData;
}
