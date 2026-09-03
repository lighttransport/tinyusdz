# SPDX-License-Identifier: Apache-2.0
import os
import pathlib

import pytest

# Repo test assets (available in the repo / sdist; may be absent when the
# wheel is tested standalone -- tests then fall back to inline documents).
_DEFAULT = pathlib.Path(__file__).resolve().parents[2] / "tests" / "usda"
ASSETS = pathlib.Path(os.environ.get("LIGHTUSD_TEST_ASSETS", _DEFAULT))


@pytest.fixture
def assets_dir():
    if not ASSETS.is_dir():
        pytest.skip("test assets not available")
    return ASSETS


SIMPLE_USDA = """#usda 1.0
(
  defaultPrim = "World"
  upAxis = "Y"
  metersPerUnit = 1
)
def Xform "World" (kind = "assembly") {
  double3 xformOp:translate = (1, 2, 3)
  uniform token[] xformOpOrder = ["xformOp:translate"]

  def Mesh "Quad" {
    point3f[] points = [(0,0,0),(1,0,0),(1,1,0),(0,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    texCoord2f[] primvars:st = [(0,0),(1,0),(1,1),(0,1)] (interpolation = "vertex")
    float radius = 2.5
    token purpose = "render"
    double3 xformOp:translate.timeSamples = { 0: (1, 0, 0), 24: (5, 0, 0) }
    rel material:binding = </World/Looks/Red>
  }
  def Scope "Looks" {
    def Material "Red" {
      token outputs:surface.connect = </World/Looks/Red/Shader.outputs:surface>
      def Shader "Shader" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.9, 0.1, 0.1)
        float inputs:roughness = 0.4
        token outputs:surface
      }
    }
  }
}
"""


@pytest.fixture
def simple_stage():
    import lightusd

    return lightusd.loads(SIMPLE_USDA)
