# For ARKit

## File format

Seems USDA(ASCII) is not supported(yet?).
USDC only. 

## Mesh

attribute with `texCoord2f[]` type: Can be used as explicit texture coordinate
attribute with `float2[]` type: Specify texture UV attribute from TexCoordReader(UsdPrimvarReader) with `inputs:varname` attribute.

## Material/Shader

It looks USDZ tool(e.g. ARKit) only supports UsdPreviewSurface

https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html 

## Texture

UDIM is supported.

### File format

.png, .jpg

