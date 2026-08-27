const shader = /* wgsl */`
struct Settings { inv: mat4x4<f32>, cam: vec4<f32>, size: vec2<u32>, sampleStart: u32, sampleCount: u32, bounces: u32, root: u32, seed: u32, pad: u32 }
struct Hit { t:f32, u:f32, v:f32, prim:u32 }
@group(0) @binding(0) var<storage,read> nodes: array<u32>;
@group(0) @binding(1) var<storage,read> blocks: array<u32>;
@group(0) @binding(2) var<storage,read> normals: array<f32>;
@group(0) @binding(3) var<storage,read> colors: array<f32>;
@group(0) @binding(4) var<storage,read> params: array<f32>;
@group(0) @binding(5) var<storage,read> matIds: array<i32>;
@group(0) @binding(6) var<storage,read> materials: array<f32>;
@group(0) @binding(7) var<storage,read_write> output: array<vec4<f32>>;
@group(0) @binding(8) var<uniform> cfg: Settings;
fn fu(v:u32)->f32{return bitcast<f32>(v)}
fn hash(v0:u32)->u32{var v=v0;v=v^(v>>16u);v=v*0x7feb352du;v=v^(v>>15u);v=v*0x846ca68bu;return v^(v>>16u)}
fn rnd(state:ptr<function,u32>)->f32{*state=hash(*state+0x9e3779b9u);return f32(*state>>8u)/16777216.0}
fn cosine(n:vec3<f32>,state:ptr<function,u32>)->vec3<f32>{let r=sqrt(rnd(state));let a=6.2831853*rnd(state);let t=normalize(select(cross(vec3f(1,0,0),n),cross(vec3f(0,1,0),n),abs(n.y)<0.9));let b=cross(n,t);return normalize(t*(r*cos(a))+b*(r*sin(a))+n*sqrt(max(0.0,1.0-r*r)))}
fn intersect(org:vec3<f32>,dir:vec3<f32>)->Hit{
 var best=Hit(1e30,0,0,0xffffffffu);let invd=vec3f(select(1e18,1.0/dir.x,abs(dir.x)>1e-18),select(1e18,1.0/dir.y,abs(dir.y)>1e-18),select(1e18,1.0/dir.z,abs(dir.z)>1e-18));
 var refs:array<u32,96>;var near:array<f32,96>;var sp=1u;refs[0]=cfg.root;near[0]=0.0001;
 loop{if(sp==0u){break}sp--;let ref=refs[sp];if(near[sp]>=best.t){continue}
  if((ref&0x80000000u)!=0u){let first=(ref&0x7fffffffu)>>4u;let count=ref&15u;
   for(var block=0u;block<count;block++){let bb=(first+block)*80u;for(var lane=0u;lane<8u;lane++){let prim=blocks[bb+72u+lane];if(prim==0xffffffffu){continue}
    let v0=vec3f(fu(blocks[bb+lane]),fu(blocks[bb+8u+lane]),fu(blocks[bb+16u+lane]));let e1=vec3f(fu(blocks[bb+24u+lane]),fu(blocks[bb+32u+lane]),fu(blocks[bb+40u+lane]));let e2=vec3f(fu(blocks[bb+48u+lane]),fu(blocks[bb+56u+lane]),fu(blocks[bb+64u+lane]));
    let p=cross(dir,e2);let det=dot(e1,p);if(abs(det)<1e-12){continue}let id=1.0/det;let tv=org-v0;let u=dot(tv,p)*id;if(u<0||u>1){continue}let q=cross(tv,e1);let v=dot(dir,q)*id;if(v<0||u+v>1){continue}let t=dot(e2,q)*id;if(t>0.0001&&t<best.t){best=Hit(t,u,v,prim)}
   }}continue}
  let nb=(ref&0x7fffffffu)*64u;let childCount=nodes[nb+56u];for(var lane=0u;lane<childCount;lane++){let lo=vec3f(fu(nodes[nb+lane]),fu(nodes[nb+8u+lane]),fu(nodes[nb+16u+lane]));let hi=vec3f(fu(nodes[nb+24u+lane]),fu(nodes[nb+32u+lane]),fu(nodes[nb+40u+lane]));let a=(lo-org)*invd;let b=(hi-org)*invd;let tn=max(max(max(min(a.x,b.x),min(a.y,b.y)),min(a.z,b.z)),0.0001);let tf=min(min(min(max(a.x,b.x),max(a.y,b.y)),max(a.z,b.z)),best.t);if(tn<=tf&&sp<96u){refs[sp]=nodes[nb+48u+lane];near[sp]=tn;sp++}}
 }return best
}
fn attr3(a:ptr<storage,array<f32>,read>,prim:u32,u:f32,v:f32)->vec3<f32>{let o=prim*9u;let w=1.0-u-v;return vec3f((*a)[o],(*a)[o+1u],(*a)[o+2u])*w+vec3f((*a)[o+3u],(*a)[o+4u],(*a)[o+5u])*u+vec3f((*a)[o+6u],(*a)[o+7u],(*a)[o+8u])*v}
fn attr4(prim:u32,u:f32,v:f32)->vec4<f32>{let o=prim*12u;let w=1.0-u-v;return vec4f(params[o],params[o+1u],params[o+2u],params[o+3u])*w+vec4f(params[o+4u],params[o+5u],params[o+6u],params[o+7u])*u+vec4f(params[o+8u],params[o+9u],params[o+10u],params[o+11u])*v}
@compute @workgroup_size(8,8) fn main(@builtin(global_invocation_id) id:vec3<u32>){if(id.x>=cfg.size.x||id.y>=cfg.size.y){return}let pixel=id.y*cfg.size.x+id.x;var sum=vec3f(0);for(var sample=0u;sample<cfg.sampleCount;sample++){var state=hash(pixel^(cfg.sampleStart+sample+1u)*9781u^cfg.seed);let ndc=vec2f(2.0*(f32(id.x)+rnd(&state))/f32(cfg.size.x)-1.0,1.0-2.0*(f32(id.y)+rnd(&state))/f32(cfg.size.y));let far=cfg.inv*vec4f(ndc,1,1);var org=cfg.cam.xyz;var dir=normalize(far.xyz/far.w-org);var through=vec3f(1);var rad=vec3f(0);
 for(var bounce=0u;bounce<cfg.bounces;bounce++){let h=intersect(org,dir);if(h.prim==0xffffffffu){let sky=0.08+0.18*max(0.0,dir.y);rad+=through*vec3f(sky*0.85,sky*0.92,sky);break}var n=normalize(attr3(&normals,h.prim,h.u,h.v));if(dot(n,dir)>0){n=-n}let tex=attr3(&colors,h.prim,h.u,h.v);let mp=attr4(h.prim,h.u,h.v);let mid=u32(max(0,matIds[h.prim]));let mo=mid*10u;let base=tex*vec3f(materials[mo],materials[mo+1u],materials[mo+2u]);rad+=through*vec3f(materials[mo+5u],materials[mo+6u],materials[mo+7u]);let hp=org+dir*h.t;let light=normalize(vec3f(-0.45,0.8,0.35));rad+=through*base*max(0.0,dot(n,light))*(0.7+0.3*mp.w);let pick=rnd(&state);if(pick<mp.z){org=hp+dir*0.001;through*=base}else if(pick<mp.z+mp.x){dir=normalize(reflect(dir,n)+cosine(n,&state)*mp.y*mp.y);org=hp+n*0.001;through*=base}else{dir=cosine(n,&state);org=hp+n*0.001;through*=mix(base,vec3f(1,0.55,0.42),mp.w*0.25)}if(bounce>=2u){let p=clamp(max(through.x,max(through.y,through.z)),0.1,0.95);if(rnd(&state)>p){break}through/=p}}
 sum+=rad}output[pixel]=vec4f(sum/f32(cfg.sampleCount),1)}
`;

export class OpenChessWebGPUPathTracer {
  async init(scene) {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw new Error('No WebGPU adapter');
    this.device = await adapter.requestDevice();
    this.scene = scene;
    const storage = (data, usage = GPUBufferUsage.STORAGE) => {
      const size = Math.max(4, (data.byteLength + 3) & ~3);
      const b = this.device.createBuffer({ size, usage: usage | GPUBufferUsage.COPY_DST });
      this.device.queue.writeBuffer(b, 0, data); return b;
    };
    this.buffers = [storage(scene.nodes), storage(scene.blocks), storage(scene.normals), storage(scene.colors),
      storage(scene.vertexParams), storage(scene.materialIds), storage(scene.materials)];
    this.pipeline = this.device.createComputePipeline({ layout: 'auto', compute: { module: this.device.createShaderModule({ code: shader }), entryPoint: 'main' } });
  }
  async trace({ inv, camera, width, height, sampleStart, sampleCount, bounces }) {
    const outputSize = width * height * 16;
    const output = this.device.createBuffer({ size: outputSize, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
    const read = this.device.createBuffer({ size: outputSize, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const uniform = this.device.createBuffer({ size: 112, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    const raw = new ArrayBuffer(112), f = new Float32Array(raw), u = new Uint32Array(raw);
    f.set(inv, 0); f.set([...camera, 1], 16); u[20] = width; u[21] = height; u[22] = sampleStart; u[23] = sampleCount; u[24] = bounces; u[25] = this.scene.root; u[26] = 1337;
    this.device.queue.writeBuffer(uniform, 0, raw);
    const entries = this.buffers.map((buffer, binding) => ({ binding, resource: { buffer } }));
    entries.push({ binding: 7, resource: { buffer: output } }, { binding: 8, resource: { buffer: uniform } });
    const bind = this.device.createBindGroup({ layout: this.pipeline.getBindGroupLayout(0), entries });
    const encoder = this.device.createCommandEncoder(), pass = encoder.beginComputePass();
    pass.setPipeline(this.pipeline); pass.setBindGroup(0, bind); pass.dispatchWorkgroups(Math.ceil(width / 8), Math.ceil(height / 8)); pass.end();
    encoder.copyBufferToBuffer(output, 0, read, 0, outputSize); this.device.queue.submit([encoder.finish()]);
    await read.mapAsync(GPUMapMode.READ); const result = new Float32Array(read.getMappedRange().slice(0)); read.unmap();
    output.destroy(); read.destroy(); uniform.destroy(); return result;
  }
  destroy() { for (const buffer of this.buffers || []) buffer.destroy(); this.buffers = []; }
}
