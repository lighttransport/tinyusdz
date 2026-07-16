export const SIMPLE_TRIANGLE_USDA = `#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Mesh "Prim"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }
}
`;

export const TEXTURED_TWO_MATERIAL_USDA = `#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Mesh "Prim_A" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </World/Mat_A>
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        texCoord2f[] primvars:st = [(0, 0), (1, 0), (0, 1)] (
            interpolation = "vertex"
        )
    }

    def Mesh "Prim_B" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </World/Mat_B>
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(1, 0, 0), (2, 0, 0), (1, 1, 0)]
        texCoord2f[] primvars:st = [(0, 0), (1, 0), (0, 1)] (
            interpolation = "vertex"
        )
    }

    def Material "Mat_A"
    {
        token outputs:surface.connect = </World/Mat_A/Shader.outputs:surface>
        def Shader "Shader"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor.connect = </World/Mat_A/Texture.outputs:rgb>
            float inputs:opacity.connect = </World/Mat_A/Texture.outputs:r>
            float inputs:roughness = 0.5
            token outputs:surface
        }
        def Shader "Texture"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @Texture.png@
            token inputs:sourceColorSpace = "sRGB"
            float3 outputs:rgb
            float outputs:r
        }
    }

    def Material "Mat_B"
    {
        token outputs:surface.connect = </World/Mat_B/Shader.outputs:surface>
        def Shader "Shader"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor.connect = </World/Mat_B/Texture.outputs:rgb>
            float inputs:opacity.connect = </World/Mat_B/Texture.outputs:r>
            float inputs:roughness = 0.5
            token outputs:surface
        }
        def Shader "Texture"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @Texture.png@
            token inputs:sourceColorSpace = "sRGB"
            float3 outputs:rgb
            float outputs:r
        }
    }
}
`;

export const INVALID_FACE_INDEX_USDA = `#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Mesh "Prim_Invalid"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 9]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }
}
`;
