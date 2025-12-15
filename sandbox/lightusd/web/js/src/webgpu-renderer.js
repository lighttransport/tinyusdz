// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - WebGPU Renderer (Pure JavaScript, no three.js)
// Renders USD scenes converted to RenderScene format

/**
 * Simple WebGPU renderer for LightUSD RenderScene
 * Supports UsdPreviewSurface PBR materials
 */
export class WebGPURenderer {
    constructor(canvas) {
        this.canvas = canvas;
        this.device = null;
        this.context = null;
        this.format = null;
        this.pipeline = null;
        this.depthTexture = null;

        // Camera
        this.camera = {
            position: [0, 0, 5],
            target: [0, 0, 0],
            up: [0, 1, 0],
            fov: 45,
            near: 0.1,
            far: 1000
        };

        // Uniform buffers
        this.uniformBuffer = null;
        this.materialBuffer = null;
        this.uniformBindGroup = null;

        // Scene data
        this.meshBuffers = [];
        this.materials = [];

        // Light
        this.lightDir = [0.5, 1.0, 0.8];
    }

    /**
     * Initialize WebGPU
     */
    async init() {
        if (!navigator.gpu) {
            throw new Error('WebGPU is not supported in this browser');
        }

        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
            throw new Error('Failed to get WebGPU adapter');
        }

        this.device = await adapter.requestDevice();

        this.context = this.canvas.getContext('webgpu');
        this.format = navigator.gpu.getPreferredCanvasFormat();

        this.context.configure({
            device: this.device,
            format: this.format,
            alphaMode: 'premultiplied'
        });

        this._createPipeline();
        this._createUniformBuffer();
        this._createDepthTexture();

        return this;
    }

    /**
     * Create render pipeline with PBR material support
     */
    _createPipeline() {
        const shaderCode = `
            // Scene uniforms
            struct Uniforms {
                viewProj: mat4x4<f32>,
                model: mat4x4<f32>,
                lightDir: vec3<f32>,
                _pad: f32,
                cameraPos: vec3<f32>,
                _pad2: f32,
            }

            // PBR Material properties (UsdPreviewSurface)
            struct Material {
                baseColor: vec4<f32>,      // RGB + alpha
                emissive: vec3<f32>,
                metallic: f32,
                roughness: f32,
                normalScale: f32,
                occlusionStrength: f32,
                alphaCutoff: f32,
            }

            @group(0) @binding(0) var<uniform> uniforms: Uniforms;
            @group(0) @binding(1) var<uniform> material: Material;

            struct VertexInput {
                @location(0) position: vec3<f32>,
                @location(1) normal: vec3<f32>,
            }

            struct VertexOutput {
                @builtin(position) position: vec4<f32>,
                @location(0) worldPos: vec3<f32>,
                @location(1) normal: vec3<f32>,
            }

            @vertex
            fn vs_main(input: VertexInput) -> VertexOutput {
                var output: VertexOutput;
                let worldPos = (uniforms.model * vec4<f32>(input.position, 1.0)).xyz;
                output.position = uniforms.viewProj * vec4<f32>(worldPos, 1.0);
                output.worldPos = worldPos;
                // Transform normal (assuming uniform scale)
                output.normal = normalize((uniforms.model * vec4<f32>(input.normal, 0.0)).xyz);
                return output;
            }

            // Simplified PBR (GGX) functions
            const PI: f32 = 3.14159265359;

            fn fresnelSchlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
                return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
            }

            fn distributionGGX(N: vec3<f32>, H: vec3<f32>, roughness: f32) -> f32 {
                let a = roughness * roughness;
                let a2 = a * a;
                let NdotH = max(dot(N, H), 0.0);
                let NdotH2 = NdotH * NdotH;
                let num = a2;
                var denom = (NdotH2 * (a2 - 1.0) + 1.0);
                denom = PI * denom * denom;
                return num / denom;
            }

            fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
                let r = roughness + 1.0;
                let k = (r * r) / 8.0;
                let num = NdotV;
                let denom = NdotV * (1.0 - k) + k;
                return num / denom;
            }

            fn geometrySmith(N: vec3<f32>, V: vec3<f32>, L: vec3<f32>, roughness: f32) -> f32 {
                let NdotV = max(dot(N, V), 0.0);
                let NdotL = max(dot(N, L), 0.0);
                let ggx2 = geometrySchlickGGX(NdotV, roughness);
                let ggx1 = geometrySchlickGGX(NdotL, roughness);
                return ggx1 * ggx2;
            }

            @fragment
            fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
                let N = normalize(input.normal);
                let L = normalize(uniforms.lightDir);
                let V = normalize(uniforms.cameraPos - input.worldPos);
                let H = normalize(L + V);

                // Material properties
                let albedo = material.baseColor.rgb;
                let metallic = material.metallic;
                let roughness = max(material.roughness, 0.04); // Prevent division issues
                let alpha = material.baseColor.a;

                // Fresnel reflectance at normal incidence
                let F0 = mix(vec3<f32>(0.04), albedo, metallic);

                // Cook-Torrance BRDF
                let NDF = distributionGGX(N, H, roughness);
                let G = geometrySmith(N, V, L, roughness);
                let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

                let kS = F;
                let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);

                let NdotL = max(dot(N, L), 0.0);

                let numerator = NDF * G * F;
                let denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
                let specular = numerator / denominator;

                // Light contribution (single directional light)
                let lightColor = vec3<f32>(1.0, 0.98, 0.95);
                let lightIntensity = 2.5;
                let Lo = (kD * albedo / PI + specular) * lightColor * lightIntensity * NdotL;

                // Ambient (image-based lighting approximation)
                let ambientStrength = 0.15;
                let ambient = albedo * ambientStrength * (1.0 - metallic * 0.5);

                // Emissive
                let emissive = material.emissive;

                // Final color
                var color = ambient + Lo + emissive;

                // Tone mapping (ACES approximation)
                color = color / (color + vec3<f32>(1.0));

                // Gamma correction
                color = pow(color, vec3<f32>(1.0 / 2.2));

                return vec4<f32>(color, alpha);
            }
        `;

        const shaderModule = this.device.createShaderModule({ code: shaderCode });

        const uniformBindGroupLayout = this.device.createBindGroupLayout({
            entries: [
                {
                    binding: 0,
                    visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
                    buffer: { type: 'uniform' }
                },
                {
                    binding: 1,
                    visibility: GPUShaderStage.FRAGMENT,
                    buffer: { type: 'uniform' }
                }
            ]
        });

        const pipelineLayout = this.device.createPipelineLayout({
            bindGroupLayouts: [uniformBindGroupLayout]
        });

        this.pipeline = this.device.createRenderPipeline({
            layout: pipelineLayout,
            vertex: {
                module: shaderModule,
                entryPoint: 'vs_main',
                buffers: [
                    {
                        arrayStride: 12, // 3 floats * 4 bytes
                        attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }]
                    },
                    {
                        arrayStride: 12,
                        attributes: [{ shaderLocation: 1, offset: 0, format: 'float32x3' }]
                    }
                ]
            },
            fragment: {
                module: shaderModule,
                entryPoint: 'fs_main',
                targets: [{
                    format: this.format,
                    blend: {
                        color: {
                            srcFactor: 'src-alpha',
                            dstFactor: 'one-minus-src-alpha',
                            operation: 'add'
                        },
                        alpha: {
                            srcFactor: 'one',
                            dstFactor: 'one-minus-src-alpha',
                            operation: 'add'
                        }
                    }
                }]
            },
            primitive: {
                topology: 'triangle-list',
                cullMode: 'back',
                frontFace: 'ccw'
            },
            depthStencil: {
                format: 'depth24plus',
                depthWriteEnabled: true,
                depthCompare: 'less'
            }
        });

        this.uniformBindGroupLayout = uniformBindGroupLayout;
    }

    /**
     * Create uniform buffers (scene and material)
     */
    _createUniformBuffer() {
        // Scene uniforms: viewProj (64) + model (64) + lightDir (12) + pad (4) + cameraPos (12) + pad (4) = 160 bytes
        this.uniformBuffer = this.device.createBuffer({
            size: 160,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        // Material uniforms: baseColor (16) + emissive (12) + metallic (4) + roughness (4) + normalScale (4) + occlusionStrength (4) + alphaCutoff (4) = 48 bytes
        this.materialBuffer = this.device.createBuffer({
            size: 48,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        this.uniformBindGroup = this.device.createBindGroup({
            layout: this.uniformBindGroupLayout,
            entries: [
                {
                    binding: 0,
                    resource: { buffer: this.uniformBuffer }
                },
                {
                    binding: 1,
                    resource: { buffer: this.materialBuffer }
                }
            ]
        });
    }

    /**
     * Create depth texture
     */
    _createDepthTexture() {
        this.depthTexture = this.device.createTexture({
            size: [this.canvas.width, this.canvas.height],
            format: 'depth24plus',
            usage: GPUTextureUsage.RENDER_ATTACHMENT
        });
    }

    /**
     * Load RenderScene into GPU buffers
     */
    loadScene(renderScene) {
        // Clear existing buffers
        for (const mb of this.meshBuffers) {
            mb.positionBuffer.destroy();
            mb.normalBuffer.destroy();
            mb.indexBuffer.destroy();
        }
        this.meshBuffers = [];
        this.materials = [];

        // Load materials first
        for (let i = 0; i < renderScene.materialCount(); i++) {
            const mat = renderScene.material(i);
            const baseColor = mat.baseColor();
            const emissive = mat.emissive();
            this.materials.push({
                name: mat.name(),
                path: mat.path(),
                baseColor: baseColor,
                metallic: mat.metallic(),
                roughness: mat.roughness(),
                emissive: emissive,
                normalScale: mat.normalScale(),
                occlusionStrength: mat.occlusionStrength(),
                alphaCutoff: mat.alphaCutoff(),
                doubleSided: mat.doubleSided()
            });
            mat.delete();
        }

        // Load each mesh
        for (let i = 0; i < renderScene.meshCount(); i++) {
            const mesh = renderScene.mesh(i);
            if (!mesh.isValid()) {
                mesh.delete();
                continue;
            }

            // Get vertex data (these return typed arrays)
            const positions = mesh.positions();
            const normals = mesh.normals();
            const indices = mesh.indices();

            if (!positions || positions.length === 0) {
                mesh.delete();
                continue;
            }

            // Create position buffer
            const positionBuffer = this.device.createBuffer({
                size: positions.byteLength,
                usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
                mappedAtCreation: true
            });
            new Float32Array(positionBuffer.getMappedRange()).set(positions);
            positionBuffer.unmap();

            // Create normal buffer (use positions as fallback)
            const normalData = normals && normals.length > 0 ? normals : positions;
            const normalBuffer = this.device.createBuffer({
                size: normalData.byteLength,
                usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
                mappedAtCreation: true
            });
            new Float32Array(normalBuffer.getMappedRange()).set(normalData);
            normalBuffer.unmap();

            // Create index buffer
            const indexBuffer = this.device.createBuffer({
                size: indices.byteLength,
                usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
                mappedAtCreation: true
            });
            new Uint32Array(indexBuffer.getMappedRange()).set(indices);
            indexBuffer.unmap();

            // Get transform matrix
            const transform = mesh.transform();

            // Get material index from first submesh
            let materialIndex = -1;
            if (mesh.submeshCount() > 0) {
                const submesh = mesh.submesh(0);
                if (submesh) {
                    materialIndex = submesh.materialIndex;
                }
            }

            this.meshBuffers.push({
                name: mesh.name(),
                positionBuffer,
                normalBuffer,
                indexBuffer,
                indexCount: indices.length,
                transform: transform ? new Float32Array(transform) : null,
                doubleSided: mesh.doubleSided(),
                materialIndex: materialIndex
            });

            mesh.delete();
        }

        // Auto-fit camera to scene bounds
        const boundsMin = renderScene.boundsMin();
        const boundsMax = renderScene.boundsMax();
        this._fitCameraToBounds(boundsMin, boundsMax);
    }

    /**
     * Fit camera to view entire scene
     */
    _fitCameraToBounds(min, max) {
        const center = [
            (min[0] + max[0]) / 2,
            (min[1] + max[1]) / 2,
            (min[2] + max[2]) / 2
        ];

        const size = Math.max(
            max[0] - min[0],
            max[1] - min[1],
            max[2] - min[2]
        );

        const distance = size * 2;

        this.camera.target = center;
        this.camera.position = [
            center[0] + distance * 0.5,
            center[1] + distance * 0.5,
            center[2] + distance
        ];
    }

    /**
     * Set camera position
     */
    setCamera(position, target, up) {
        this.camera.position = position;
        this.camera.target = target;
        if (up) this.camera.up = up;
    }

    /**
     * Render frame
     */
    render() {
        // Handle resize
        if (this.canvas.width !== this.depthTexture.width ||
            this.canvas.height !== this.depthTexture.height) {
            this.depthTexture.destroy();
            this._createDepthTexture();
        }

        const commandEncoder = this.device.createCommandEncoder();

        const renderPass = commandEncoder.beginRenderPass({
            colorAttachments: [{
                view: this.context.getCurrentTexture().createView(),
                clearValue: { r: 0.15, g: 0.15, b: 0.18, a: 1.0 },
                loadOp: 'clear',
                storeOp: 'store'
            }],
            depthStencilAttachment: {
                view: this.depthTexture.createView(),
                depthClearValue: 1.0,
                depthLoadOp: 'clear',
                depthStoreOp: 'store'
            }
        });

        // Update uniforms
        const aspect = this.canvas.width / this.canvas.height;
        const viewMatrix = this._lookAt(this.camera.position, this.camera.target, this.camera.up);
        const projMatrix = this._perspective(this.camera.fov * Math.PI / 180, aspect, this.camera.near, this.camera.far);
        const viewProjMatrix = this._multiply(projMatrix, viewMatrix);

        renderPass.setPipeline(this.pipeline);
        renderPass.setBindGroup(0, this.uniformBindGroup);

        // Render each mesh
        for (const mb of this.meshBuffers) {
            // Update uniform buffer with model matrix
            const uniformData = new Float32Array(40); // 160 bytes / 4
            uniformData.set(viewProjMatrix, 0);

            // Model matrix (identity if no transform)
            if (mb.transform) {
                uniformData.set(mb.transform, 16);
            } else {
                uniformData.set([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1], 16);
            }

            // Light direction (normalized)
            const lightLen = Math.sqrt(
                this.lightDir[0]**2 + this.lightDir[1]**2 + this.lightDir[2]**2
            );
            uniformData[32] = this.lightDir[0] / lightLen;
            uniformData[33] = this.lightDir[1] / lightLen;
            uniformData[34] = this.lightDir[2] / lightLen;
            uniformData[35] = 0; // padding

            // Camera position
            uniformData[36] = this.camera.position[0];
            uniformData[37] = this.camera.position[1];
            uniformData[38] = this.camera.position[2];
            uniformData[39] = 0; // padding

            this.device.queue.writeBuffer(this.uniformBuffer, 0, uniformData);

            // Update material buffer
            // Material struct: baseColor (16) + emissive (12) + metallic (4) + roughness (4) + normalScale (4) + occlusionStrength (4) + alphaCutoff (4) = 48 bytes
            const materialData = new Float32Array(12);

            // Get material from mesh or use defaults
            const mat = mb.materialIndex >= 0 && mb.materialIndex < this.materials.length
                ? this.materials[mb.materialIndex]
                : null;

            if (mat) {
                // baseColor (vec4)
                materialData[0] = mat.baseColor[0];
                materialData[1] = mat.baseColor[1];
                materialData[2] = mat.baseColor[2];
                materialData[3] = mat.baseColor[3];
                // emissive (vec3)
                materialData[4] = mat.emissive[0];
                materialData[5] = mat.emissive[1];
                materialData[6] = mat.emissive[2];
                // metallic
                materialData[7] = mat.metallic;
                // roughness
                materialData[8] = mat.roughness;
                // normalScale
                materialData[9] = mat.normalScale;
                // occlusionStrength
                materialData[10] = mat.occlusionStrength;
                // alphaCutoff
                materialData[11] = mat.alphaCutoff;
            } else {
                // Default material (gray, non-metallic, slightly rough)
                materialData[0] = 0.7; // baseColor.r
                materialData[1] = 0.7; // baseColor.g
                materialData[2] = 0.7; // baseColor.b
                materialData[3] = 1.0; // baseColor.a
                materialData[4] = 0.0; // emissive.r
                materialData[5] = 0.0; // emissive.g
                materialData[6] = 0.0; // emissive.b
                materialData[7] = 0.0; // metallic
                materialData[8] = 0.5; // roughness
                materialData[9] = 1.0; // normalScale
                materialData[10] = 1.0; // occlusionStrength
                materialData[11] = 0.5; // alphaCutoff
            }

            this.device.queue.writeBuffer(this.materialBuffer, 0, materialData);

            renderPass.setVertexBuffer(0, mb.positionBuffer);
            renderPass.setVertexBuffer(1, mb.normalBuffer);
            renderPass.setIndexBuffer(mb.indexBuffer, 'uint32');
            renderPass.drawIndexed(mb.indexCount);
        }

        renderPass.end();
        this.device.queue.submit([commandEncoder.finish()]);
    }

    /**
     * Create look-at view matrix
     */
    _lookAt(eye, target, up) {
        const zAxis = this._normalize([
            eye[0] - target[0],
            eye[1] - target[1],
            eye[2] - target[2]
        ]);
        const xAxis = this._normalize(this._cross(up, zAxis));
        const yAxis = this._cross(zAxis, xAxis);

        return [
            xAxis[0], yAxis[0], zAxis[0], 0,
            xAxis[1], yAxis[1], zAxis[1], 0,
            xAxis[2], yAxis[2], zAxis[2], 0,
            -this._dot(xAxis, eye), -this._dot(yAxis, eye), -this._dot(zAxis, eye), 1
        ];
    }

    /**
     * Create perspective projection matrix
     */
    _perspective(fov, aspect, near, far) {
        const f = 1 / Math.tan(fov / 2);
        const rangeInv = 1 / (near - far);

        return [
            f / aspect, 0, 0, 0,
            0, f, 0, 0,
            0, 0, (far + near) * rangeInv, -1,
            0, 0, 2 * far * near * rangeInv, 0
        ];
    }

    /**
     * Multiply 4x4 matrices
     */
    _multiply(a, b) {
        const result = new Array(16);
        for (let row = 0; row < 4; row++) {
            for (let col = 0; col < 4; col++) {
                let sum = 0;
                for (let k = 0; k < 4; k++) {
                    sum += a[row * 4 + k] * b[k * 4 + col];
                }
                result[row * 4 + col] = sum;
            }
        }
        return result;
    }

    _normalize(v) {
        const len = Math.sqrt(v[0]**2 + v[1]**2 + v[2]**2);
        return [v[0]/len, v[1]/len, v[2]/len];
    }

    _cross(a, b) {
        return [
            a[1]*b[2] - a[2]*b[1],
            a[2]*b[0] - a[0]*b[2],
            a[0]*b[1] - a[1]*b[0]
        ];
    }

    _dot(a, b) {
        return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    }

    /**
     * Get loaded materials
     */
    getMaterials() {
        return this.materials;
    }

    /**
     * Get loaded mesh info
     */
    getMeshes() {
        return this.meshBuffers.map(mb => ({
            name: mb.name,
            indexCount: mb.indexCount,
            materialIndex: mb.materialIndex
        }));
    }

    /**
     * Clean up resources
     */
    destroy() {
        for (const mb of this.meshBuffers) {
            mb.positionBuffer.destroy();
            mb.normalBuffer.destroy();
            mb.indexBuffer.destroy();
        }
        this.meshBuffers = [];
        this.materials = [];

        if (this.uniformBuffer) {
            this.uniformBuffer.destroy();
        }
        if (this.materialBuffer) {
            this.materialBuffer.destroy();
        }
        if (this.depthTexture) {
            this.depthTexture.destroy();
        }
    }
}

/**
 * Simple orbit camera controller
 */
export class OrbitController {
    constructor(renderer, canvas) {
        this.renderer = renderer;
        this.canvas = canvas;

        this.theta = 0.5;  // Horizontal angle
        this.phi = 0.4;    // Vertical angle
        this.radius = 5;

        this.isDragging = false;
        this.lastX = 0;
        this.lastY = 0;

        this._setupEvents();
    }

    _setupEvents() {
        this.canvas.addEventListener('mousedown', (e) => {
            this.isDragging = true;
            this.lastX = e.clientX;
            this.lastY = e.clientY;
        });

        this.canvas.addEventListener('mousemove', (e) => {
            if (!this.isDragging) return;

            const dx = e.clientX - this.lastX;
            const dy = e.clientY - this.lastY;

            this.theta -= dx * 0.01;
            this.phi = Math.max(0.1, Math.min(Math.PI - 0.1, this.phi + dy * 0.01));

            this.lastX = e.clientX;
            this.lastY = e.clientY;

            this._updateCamera();
        });

        this.canvas.addEventListener('mouseup', () => {
            this.isDragging = false;
        });

        this.canvas.addEventListener('mouseleave', () => {
            this.isDragging = false;
        });

        this.canvas.addEventListener('wheel', (e) => {
            e.preventDefault();
            this.radius *= 1 + e.deltaY * 0.001;
            this.radius = Math.max(0.1, Math.min(1000, this.radius));
            this._updateCamera();
        });
    }

    setTarget(target) {
        this.renderer.camera.target = target;
        this._updateCamera();
    }

    setRadius(radius) {
        this.radius = radius;
        this._updateCamera();
    }

    _updateCamera() {
        const target = this.renderer.camera.target;
        const x = target[0] + this.radius * Math.sin(this.phi) * Math.cos(this.theta);
        const y = target[1] + this.radius * Math.cos(this.phi);
        const z = target[2] + this.radius * Math.sin(this.phi) * Math.sin(this.theta);

        this.renderer.camera.position = [x, y, z];
    }
}
