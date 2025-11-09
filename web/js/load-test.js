// require nodejs v24.0 or later(to load wasm)
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { TinyUSDZComposer } from 'tinyusdz/TinyUSDZComposer.js';

async function initScene() {

  const loader = new TinyUSDZLoader();
  await loader.init();

}

console.log(WebAssembly);
/*
Object [WebAssembly] {
  compile: [Function: compile],
  validate: [Function: validate],
  instantiate: [Function: instantiate]
}
*/


initScene();
