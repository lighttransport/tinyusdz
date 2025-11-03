# Animation System Test Guide

## Overview
The animation system has been updated to support USD keyframe animations from `cube-animation.usda`. This file contains animated transforms (translation, rotation, and scale) with 10 keyframes over 90 seconds.

## Features Added

### 1. Enhanced animation-info.js
- Added `--keyframes` flag to dump all keyframe data (times and values)
- Improved formatting for different animation data types (translation, rotation, scale)
- Better support for track-based animations

### 2. Updated animation.js
- Changed default USD asset from `suzanne.usdc` to `cube-animation.usda`
- Added support for track-based animations (legacy format)
- Enhanced `convertUSDAnimationsToThreeJS` function to handle both:
  - Track-based animations (used by cube-animation.usda)
  - Channel/Sampler-based animations (modern format)

### 3. Keyframe Data Structure
The `cube-animation.usda` file contains:
- **Translation track**: 10 keyframes with 3D position values
- **Rotation track**: 10 keyframes with quaternion values (x,y,z,w)
- **Scale track**: 10 keyframes with 3D scale values

## Testing Instructions

### Test 1: Animation Info Extraction
```bash
# Check basic animation info
sh animation-info.sh cube-animation.usda

# Check detailed animation info with keyframe dumps
sh animation-info.sh cube-animation.usda --detailed --keyframes
```

Expected output:
- 1 animation clip named "AnimatedCube_xform"
- Duration: 90 seconds
- 3 tracks: translation, rotation, scale
- Each track contains 10 keyframes

### Test 2: Node.js Animation Loading Test
```bash
# Run the test script
vite-node test-animation.js
```

This verifies that the USD animation data is correctly loaded and parsed.

### Test 3: Browser Animation Playback
```bash
# Start the development server
npm run dev
# or
vite
```

Then open your browser to:
- http://localhost:5173/animation.html

You should see:
1. A cube loaded from `cube-animation.usda`
2. Animation controls in the GUI (top right)
3. USD Animations folder showing 1 animation available
4. The ability to play the USD animation or synthetic animations

### Test 4: Verify Animation Playback

In the browser:
1. Look for the "USD Animations" folder in the GUI
2. Click "Play USD Animation" to play the loaded animation
3. The cube should:
   - Translate following a path
   - Rotate with smooth interpolation
   - Scale up and down

### Test 5: File Upload Test

1. Click "Load USD File" button
2. Select any other USD file with animations
3. The system should detect and list available animations
4. You can switch between animations using the GUI

## Animation Data Format

The system now supports two animation formats:

### Track-based (Legacy - used by cube-animation.usda)
```javascript
{
  tracks: [
    {
      path: 'translation',
      times: [0, 10, 20, ...],
      values: [x1,y1,z1, x2,y2,z2, ...],
      interpolation: 'LINEAR'
    }
  ]
}
```

### Channel/Sampler-based (Modern)
```javascript
{
  channels: [...],
  samplers: [...]
}
```

## Key Implementation Details

1. **Automatic Format Detection**: The system automatically detects whether an animation uses tracks or channels/samplers

2. **Quaternion Conversion**: Rotation values from USD (originally Euler angles in the .usda file) are converted to quaternions internally by TinyUSDZ

3. **Three.js Integration**: USD animations are converted to Three.js AnimationClips with proper KeyframeTracks

## Troubleshooting

If animations don't play:
1. Check browser console for errors
2. Verify the USD file loaded successfully
3. Ensure WebAssembly is enabled in your browser
4. Check that the animation duration is > 0
5. Verify keyframe data is present in the tracks

## Files Modified

- `animation-info.js` - Enhanced with keyframe dumping
- `animation.js` - Updated to use cube-animation.usda and support track-based animations
- `animation.html` - Updated default file display
- `test-animation.js` - New test script for verification
- `cube-animation.usda` - Already present in assets/

## Next Steps

The animation system is now ready for:
- Loading USD files with embedded animations
- Playing back node transform animations in Three.js
- Extracting and visualizing keyframe data
- Supporting both legacy and modern USD animation formats