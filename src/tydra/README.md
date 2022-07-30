# What is Tydra?

TinyUSDZ does not support Hydra interface at the moment.
We think Hydra(multi-purpose sceneDelegate/renderDelegate interface) is too much for TinyUSDZ usecases(AR, lightweight 3D viewer/runtime, etc).

Instead, we'd like to propose Tydra, a three-headed monster(Please imagine Gidorah: https://en.wikipedia.org/wiki/King_Ghidora), which directly converts(`publishes`) USD scene graph(GPrim hierarchy) to a renderer-friendly data structure or `published` data format(imagine glTF)

Currently Tydra is considering the following three usecases:

- GL/Vulkan renderer
- Ray tracing renderer
  See `../../examples/sdlviewer/` for SW raytracing example.
- Runtime publishment, DCC conversion(e.g. to glTF)


EoL.

