const samples = [
  {
    name: 'Clean Xform',
    filename: 'clean.usda',
    text: `#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
)

def Xform "World"
{
}
`
  },
  {
    name: 'Mesh topology issues',
    filename: 'mesh-bad-topology.usda',
    text: `#usda 1.0

def Mesh "badMesh"
{
    point3f[] points = [(0, 0, 0), (1, 0, 0)]
    int[] faceVertexCounts = [3, 0]
    int[] faceVertexIndices = [0, 1, 9, 0]
}
`
  },
  {
    name: 'UsdLux light issues',
    filename: 'lux-light-issues.usda',
    text: `#usda 1.0

def SphereLight "sphere"
{
    float inputs:intensity = -1
    float inputs:radius = 0
    float inputs:shaping:cone:softness = 2
}

def DomeLight_1 "dome"
{
    token inputs:texture:format = "cubeMap"
    token poleAxis = "X"
}
`
  },
  {
    name: 'UsdPhysics issues',
    filename: 'physics-issues.usda',
    text: `#usda 1.0

def PhysicsScene "scene"
{
    vector3f physics:gravityDirection = (0, 0, 0)
    float physics:gravityMagnitude = -1
}

def Mesh "body" (
    prepend apiSchemas = ["PhysicsRigidBodyAPI", "PhysicsCollisionAPI", "PhysicsMeshCollisionAPI"]
)
{
    float physics:mass = -1
    token physics:approximation = "triangleSoup"
    token physics:simulationOwner = "notARelationship"
}

def PhysicsRevoluteJoint "hinge"
{
    token physics:axis = "W"
    token physics:body0 = "notARelationship"
    float physics:lowerLimit = 10
    float physics:upperLimit = 0
}
`
  },
  {
    name: 'UsdPreviewSurface warnings',
    filename: 'preview-surface-warning.usda',
    text: `#usda 1.0

def Material "mat"
{
    def Shader "surface"
    {
        uniform token info:id = "UsdPreviewSurface"
        float inputs:diffuseColor = 0.5
        float inputs:rooughness = 0.5
        token outputs:surface
    }
}
`
  },
  {
    name: 'Shader/material issues',
    filename: 'shader-material-issues.usda',
    text: `#usda 1.0

def Material "mat"
{
    token outputs:surface = "notAConnection"
    token inputs:stPrimvarName = "st"

    def Mesh "notShader"
    {
        token outputs:surface
    }

    token outputs:volume.connect = </mat/notShader.outputs:surface>

    def Shader "tex"
    {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @@
        token inputs:wrapS = "tileForever"
        token inputs:sourceColorSpace = "ACEScg"
        float3 outputs:rgb
    }

    def Shader "reader"
    {
        uniform token info:id = "UsdPrimvarReader_float2"
        string inputs:varname.connect = </mat.inputs:stPrimvarName>
        token outputs:result
    }
}
`
  },
  {
    name: 'Skel, MaterialX, composition',
    filename: 'advanced-usd-issues.usda',
    text: `#usda 1.0

def Material "mat" (
    references = @./look.mtlx@
)
{
    string config:mtlx:version = "2.0"
    string config:mtlx:sourceUri = "look.usda"

    def Shader "mtlxShader"
    {
        uniform token info:id = "ND_open_pbr_surface_surfaceshader"
    }
}

def SkelRoot "Rig"
{
    def Skeleton "Skel"
    {
        uniform token[] joints = ["Root/Spine", "Root"]
        uniform matrix4d[] bindTransforms = [
            ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1))
        ]
    }

    def Mesh "Mesh"
    {
        rel skel:skeleton = </Rig/Skel>
        int[] primvars:skel:jointIndices = [0, -1, 1] (
            interpolation = "constant"
            elementSize = 2
        )
        float[] primvars:skel:jointWeights = [1, 0] (
            interpolation = "vertex"
            elementSize = 1
        )
    }
}

def Xform "Composed" (
    references = @./ref.usda@</Ref> (scale = 0)
    payload = @./payload.usda@</Payload> (scale = 0)
    inherits = </Composed>
    variantSets = ["bad-name"]
)
{
}
`
  },
  {
    name: 'Animation, BlendShape, clips',
    filename: 'animation-clips-issues.usda',
    text: `#usda 1.0

over "Animated" (
    clips = {
        dictionary bad = {
            asset[] assetPaths = [@clip_0.usdc@]
            double2[] active = [(0, 0), (1, 2)]
            string primPath = "NotAbsolute"
            double2[] times = [(10, 10), (5, 5)]
        }
        dictionary templ = {
            asset templateAssetPath = @clips/anim.###.usd@
            double templateStartTime = 10
            double templateEndTime = 1
            double templateStride = 0
        }
    }
)
{
}

def SkelRoot "Rig"
{
    def Skeleton "Skel"
    {
        uniform token[] joints = ["Root", "Root/Spine"]
    }

    def SkelAnimation "Anim"
    {
        uniform token[] joints = ["Root", "Root/Spine"]
        quatf[] rotations.timeSamples = {
            1: [(1, 0, 0, 0), (1, 0, 0, 0)]
            2: [(1, 0, 0, 0)]
        }
        uniform token[] blendShapes = ["Smile", "Smile"]
        float[] blendShapeWeights.timeSamples = {
            1: [0, 1]
            2: [0]
        }
    }

    def BlendShape "Smile"
    {
        uniform vector3f[] offsets = [(0, 0, 0), (1, 0, 0)]
        uniform vector3f[] normalOffsets = [(0, 0, 1)]
        uniform int[] pointIndices = [0, -1]
        uniform vector3f[] inbetweens:low = [(0, 0, 0)]
    }

    def Mesh "Mesh"
    {
        point3f[] points = [(0, 0, 0)]
        int[] faceVertexCounts = [1]
        int[] faceVertexIndices = [0]
        rel skel:skeleton = </Rig/Skel>
        int[] primvars:skel:jointIndices = [0, 3] (
            interpolation = "vertex"
            elementSize = 2
        )
        float[] primvars:skel:jointWeights = [0.25, 0.25] (
            interpolation = "vertex"
            elementSize = 2
        )
    }
}
`
  },
  {
    name: 'Metadata and API schemas',
    filename: 'metadata-issues.usda',
    text: `#usda 1.0
(
    defaultPrim = "1Bad"
    kilogramsPerUnit = 0
    colorConfiguration = @@
    colorManagementSystem = ""
    owner = ""
)

def Xform "Root" (
    kind = ""
    instanceable = true
    assetInfo = {
        asset identifier = @@
        string name = ""
        asset[] payloadAssetDependencies = [@@]
    }
    prepend apiSchemas = [
        "CollectionAPI",
        "MaterialBindingAPI:bad",
        "CollectionAPI:look",
        "CollectionAPI:look",
        "UnknownAPI:bad:name"
    ]
)
{
    uniform token side = "middle" (
        allowedTokens = ["left", "right", "left"]
        interpolation = "bogus"
        elementSize = 0
        connectability = "sometimes"
        renderType = ""
        outputName = ""
    )
    varying rel material:binding = </Root>
}
`
  },
  {
    name: 'UsdGeom schema issues',
    filename: 'usdgeom-issues.usda',
    text: `#usda 1.0
def Camera "cam"
{
    float2 clippingRange = (10, 1)
    float focalLength = 0
    float horizontalAperture = -1
    float verticalAperture = 0
    float fStop = -1
    token projection = "fisheye"
    token stereoRole = "center"
    double shutter:open = 1
    double shutter:close = 0
}

def PointInstancer "pi"
{
    point3f[] positions = [(0, 0, 0), (1, 0, 0)]
    int[] protoIndices = [0, -1]
    quath[] orientations = [(1, 0, 0, 0)]
}

def BasisCurves "curves"
{
    uniform token type = "cubic"
    uniform token basis = "bogus"
    uniform token wrap = "loop"
    point3f[] points = [(0, 0, 0), (1, 0, 0), (2, 0, 0)]
    int[] curveVertexCounts = [2, 2]
    float[] widths = [0.1, 0.2, 0.3, 0.4]
}
`
  }
];

const state = {
  nativeModule: null,
  currentBytes: null,
  currentName: '',
  lastResult: null
};

const statusEl = document.getElementById('status');
const dropzone = document.getElementById('dropzone');
const fileInput = document.getElementById('fileInput');
const chooseFileBtn = document.getElementById('chooseFile');
const validateBtn = document.getElementById('validate');
const fileInfoEl = document.getElementById('fileInfo');
const sampleSelect = document.getElementById('sampleSelect');
const loadSampleBtn = document.getElementById('loadSample');
const groupCore = document.getElementById('groupCore');
const groupGeom = document.getElementById('groupGeom');
const groupShade = document.getElementById('groupShade');
const groupLux = document.getElementById('groupLux');
const groupPhysics = document.getElementById('groupPhysics');
const groupCrate = document.getElementById('groupCrate');
const summaryStatus = document.getElementById('summaryStatus');
const summaryErrors = document.getElementById('summaryErrors');
const summaryWarnings = document.getElementById('summaryWarnings');
const summaryGroups = document.getElementById('summaryGroups');
const issueRows = document.getElementById('issueRows');
const reportEl = document.getElementById('report');
const copyReportBtn = document.getElementById('copyReport');
const downloadReportBtn = document.getElementById('downloadReport');

function setStatus(text) {
  statusEl.textContent = text;
}

function selectedGroups() {
  const groups = [];
  if (groupCore.checked) groups.push('core');
  if (groupGeom.checked) groups.push('geom');
  if (groupShade.checked) groups.push('shade');
  if (groupLux.checked) groups.push('lux');
  if (groupPhysics.checked) groups.push('physics');
  if (groupCrate.checked) groups.push('crate');
  return groups.length ? groups : ['core'];
}

function updateGroupsSummary() {
  summaryGroups.textContent = selectedGroups().join(', ');
}

function setCurrentInput(bytes, filename) {
  state.currentBytes = bytes;
  state.currentName = filename;
  validateBtn.disabled = !state.nativeModule;
  fileInfoEl.textContent = `${filename} - ${bytes.byteLength.toLocaleString()} bytes`;
}

function formatResult(result) {
  return JSON.stringify(result, null, 2);
}

function renderResult(result) {
  state.lastResult = result;
  const parseOk = result.parse_ok !== false;
  const ok = parseOk && result.ok === true;
  const warningCount = Number(result.warning_count || 0);
  summaryStatus.textContent = parseOk
    ? (ok ? (warningCount > 0 ? 'Passed with warnings' : 'Passed') : 'Failed')
    : 'Parse failed';
  summaryStatus.className = `value ${ok ? (warningCount > 0 ? 'warning' : 'ok') : 'error'}`;
  summaryErrors.textContent = String(result.error_count || 0);
  summaryWarnings.textContent = String(warningCount);
  summaryGroups.textContent = Array.isArray(result.checked_groups)
    ? result.checked_groups.join(', ')
    : selectedGroups().join(', ');

  const issues = Array.isArray(result.issues) ? result.issues : [];
  issueRows.innerHTML = '';
  if (!parseOk) {
    const row = document.createElement('tr');
    row.innerHTML = `<td><span class="severity error">error</span></td><td>parse</td><td>${escapeHTML(state.currentName)}</td><td>${escapeHTML(result.error || 'Failed to parse input')}</td>`;
    issueRows.appendChild(row);
  } else if (issues.length === 0) {
    const row = document.createElement('tr');
    row.innerHTML = '<td colspan="4">No issues found.</td>';
    issueRows.appendChild(row);
  } else {
    for (const issue of issues) {
      const severity = issue.severity === 'error' ? 'error' : 'warning';
      const row = document.createElement('tr');
      row.innerHTML = `
        <td><span class="severity ${severity}">${severity}</span></td>
        <td>${escapeHTML(issue.rule_id || '')}</td>
        <td>${escapeHTML(issue.location || '')}</td>
        <td>${escapeHTML(issue.message || '')}</td>
      `;
      issueRows.appendChild(row);
    }
  }

  reportEl.textContent = formatResult(result);
  copyReportBtn.disabled = false;
  downloadReportBtn.disabled = false;
}

function escapeHTML(text) {
  return String(text)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

async function initWasm() {
  try {
    const factory = (await import('./src/tinyusdz/tinyusdz.js')).default;
    state.nativeModule = await factory();
    setStatus('Ready');
    validateBtn.disabled = !state.currentBytes;
  } catch (error) {
    setStatus(`WASM load failed: ${error.message}`);
    throw error;
  }
}

async function loadFile(file) {
  if (!file) return;
  const bytes = new Uint8Array(await file.arrayBuffer());
  setCurrentInput(bytes, file.name);
  setStatus('File loaded');
}

function loadSample(index) {
  const sample = samples[index] || samples[0];
  const bytes = new TextEncoder().encode(sample.text);
  setCurrentInput(bytes, sample.filename);
  setStatus('Sample loaded');
}

function validateCurrentInput() {
  if (!state.nativeModule || !state.currentBytes) return;
  const native = new state.nativeModule.TinyUSDZLoaderNative();
  try {
    setStatus('Validating...');
    const options = JSON.stringify({ groups: selectedGroups() });
    const raw = native.validateFromBinary(state.currentBytes, state.currentName, options);
    const result = JSON.parse(raw);
    renderResult(result);
    setStatus('Validation complete');
  } catch (error) {
    renderResult({
      parse_ok: false,
      ok: false,
      error: error.message,
      issues: [],
      error_count: 1,
      warning_count: 0,
      checked_groups: selectedGroups()
    });
    setStatus('Validation failed');
  } finally {
    native.delete();
  }
}

function downloadReport() {
  if (!state.lastResult) return;
  const blob = new Blob([formatResult(state.lastResult)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  const base = state.currentName ? state.currentName.replace(/\.[^.]+$/, '') : 'validation';
  link.href = url;
  link.download = `${base}.validation.json`;
  link.click();
  URL.revokeObjectURL(url);
}

for (let i = 0; i < samples.length; i++) {
  const option = document.createElement('option');
  option.value = String(i);
  option.textContent = samples[i].name;
  sampleSelect.appendChild(option);
}

chooseFileBtn.addEventListener('click', () => fileInput.click());
fileInput.addEventListener('change', () => loadFile(fileInput.files[0]));
validateBtn.addEventListener('click', validateCurrentInput);
loadSampleBtn.addEventListener('click', () => loadSample(Number(sampleSelect.value)));
copyReportBtn.addEventListener('click', async () => {
  await navigator.clipboard.writeText(reportEl.textContent);
  setStatus('Report copied');
});
downloadReportBtn.addEventListener('click', downloadReport);

for (const checkbox of [groupCore, groupGeom, groupShade, groupCrate]) {
  checkbox.addEventListener('change', updateGroupsSummary);
}

dropzone.addEventListener('dragover', (event) => {
  event.preventDefault();
  dropzone.classList.add('active');
});

dropzone.addEventListener('dragleave', () => {
  dropzone.classList.remove('active');
});

dropzone.addEventListener('drop', (event) => {
  event.preventDefault();
  dropzone.classList.remove('active');
  loadFile(event.dataTransfer.files[0]);
});

updateGroupsSummary();
loadSample(0);
initWasm();
