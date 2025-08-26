// require nodejs v24.0 or later(to load wasm)
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { TinyUSDZComposer } from 'tinyusdz/TinyUSDZComposer.js';

import fs from 'node:fs';

function reportMemUsage() {
 const used = process.memoryUsage()
 const messages = []
 for (let key in used) {
    console.log(`${key}: ${Math.round(used[key] / 1024 / 1024 * 100) / 100} MB`)
  }
}

function loadFile(filename) {
  try {
    const data = fs.readFileSync(filename);
    const base64 = data.toString('base64');
    const mimeType = 'application/octet-stream';
    return `data:${mimeType};base64,${base64}`;
    //console.log(data);
  } catch (err) {
    console.error(err);
  }
  return null;
}

console.log(process.versions.node);

function checkMemory64Support() {
    try {
        // Try creating a 64-bit memory
        const memory = new WebAssembly.Memory({
            initial: 1,
            maximum: 65536,
            index: 'i64'  // This specifies 64-bit indexing
        });
        return true;
    } catch (e) {
        return false;
    }
}

console.log("memory64:", checkMemory64Support());

const usd_filename = "../../models/suzanne-subd-lv6.usdc";

async function initScene() {

  const loader = new TinyUSDZLoader();
  const memory64 = checkMemory64Support();
  await loader.init({useMemory64: true});
  loader.setMaxMemoryLimitMB(150);

  const usd = await loader.loadAsync(loadFile(usd_filename));

  const usdRootNode = usd.getDefaultRootNode();
  //console.log(usdRootNode);

  reportMemUsage();

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
