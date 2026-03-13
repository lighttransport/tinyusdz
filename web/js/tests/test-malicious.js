import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';

const loader = new TinyUSDZLoader();
await loader.init();
const data = fs.readFileSync('../../models/test-malicious-digits.usda');
loader.parse(data, 'test',
    () => console.log('UNEXPECTED: Malicious file loaded'),
    (e) => console.log('GOOD: Malicious file rejected')
);