import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const root = new URL('../public/assets/metahuman-fixture/', import.meta.url);
const hero = await readFile(new URL('MetaHuman_Hero.usda', root), 'utf8');
const body = await readFile(new URL('Fixture_Body.usda', root), 'utf8');
const deformers = await readFile(new URL('MetaHuman_Deformers.usda', root), 'utf8');
const physics = await readFile(new URL('MetaHuman_Physics.usda', root), 'utf8');
assert.match(hero, /references = @Fixture_Body\.usda@/);
assert.match(hero, /def BasisCurves "HairStrands"/);
assert.match(hero, /subsurface_weight/);
assert.match(body, /def Skeleton "Skeleton"/);
assert.match(body, /def BlendShape "Smile"/);
assert.match(body, /blendShapeWeights\.timeSamples/);
assert.match(deformers, /MetaHuman DNA \/ RigLogic/);
assert.match(physics, /Unreal PhysicsAsset/);
console.log('metahuman fixture tests passed');
