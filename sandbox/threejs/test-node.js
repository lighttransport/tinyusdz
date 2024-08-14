var fs = require('fs');
var Module = require('./build_emcc/tinyusdzjs.js')

Module.onRuntimeInitialized = async function(){

  var data = new Uint8Array(fs.readFileSync("../../models/suzanne.usdc"))

  var instance = new Module.TinyUSDZLoader(data);
  console.log(instance.ok())
  console.log("# of meshes:", instance.numMeshes())
  console.log("mesh", instance.getMesh(0))

}

