# Tinyusdz WASM/JS

JS/WASM version of TinyUSDZ.

See `<tinyusdz>/.github/workflows/wasmPublish.yml` to assemble files for npm packaging.

## Quick usage

```js
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js'

async function loadScenes() {

  const loader = new TinyUSDZLoader();

  // it is recommended to call init() before loadAsync()
  // (wait loading/compiling wasm module in the early stage))
  await loader.init();

  const suzanne_filename = "./assets/suzanne-pbr.usda";

  const usd_scene = await loader.loadAsync(suzanne_filename);

  const usdRootNode = usd_scene.getDefaultRootNode();

  const defaultMtl = TinyUSDZLoaderUtils.createDefaultMaterial();

  const options = {
    overrideMaterial: false // override USD material with defaultMtl(default 'false')
  }

  const threeNode = TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_scene, options);

  //
  // Add threeNode to threejs scene.
  //
} 
```

