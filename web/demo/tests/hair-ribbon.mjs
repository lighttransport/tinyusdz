import assert from 'node:assert/strict';
import { buildHairRibbonGeometry, generateFurballCurves, generateGrassCurves } from '../src/hair-fur-rendering.js';

const curves = { tessellatedPoints: new Float32Array([0,0,0, 0,1,0, 0,1,0]),
  tessellatedVertexCounts: new Uint32Array([3]), tessellatedWidths: new Float32Array([.1,.08,.01]) };
const geometry = buildHairRibbonGeometry(curves);
assert.equal(geometry.getAttribute('position').count, 6);
assert.equal(geometry.index.count, 12);
assert.deepEqual(geometry.userData, { strandCount: 1, segmentCount: 2 });
for (const value of geometry.getAttribute('hairTangent').array) assert.ok(Number.isFinite(value));
assert.equal(generateFurballCurves(10, 3, 1).tessellatedPoints.length, 120);
assert.equal(generateGrassCurves(10, 2, 1).tessellatedVertexCounts.length, 10);
console.log('hair ribbon tests passed');
