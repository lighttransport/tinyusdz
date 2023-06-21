const fs = require('fs')
const lz4js = require('lz4js')

buffer = new ArrayBuffer(12);
let dataView = new DataView(buffer);

dataView.setUint8(1,255)
console.log(dataView.getUint8(1))

const buf = fs.readFileSync("test.usdc")
const dv = new DataView(buf.buffer);
console.log(dv);
