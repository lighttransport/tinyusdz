# imgui_sdl

ImGuiSDL is a lightweight SDL2 based renderer for [Dear ImGui](https://github.com/ocornut/imgui). Dear ImGui is designed to be easily rendered using modern 3D renderers like OpenGL or DirectX, but while SDL2's renderer does use hardware acceleration (the beforementioned APIs) behind the scenes, it does not provide an interface to pass generic vertex data to OpenGL. ImGuiSDL implements the rendering using a combination of a software based triangle rasterizer and a simple textured/filled rectangle drawer from SDL2. To improve the performance, the slower triangle blits are cached into render textures.

ImGuiSDL consists of two files that you can simply add to your project to use ImGuiSDL:

* imgui_sdl.h
* imgui_sdl.cpp

## Usage

There is a full usage example provided in example.cpp, and the public API is exposed and documented in imgui_sdl.h. 

Here's how you initialize ImGuiSDL:
```cpp
// Create your SDL renderer or use an existing one.
SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
// Initialize ImGuiSDL by calling Initialize with your SDL_Renderer, and with window size. This will also take care of setting up the ImGui viewport.
ImGuiSDL::Initialize(renderer, 800, 600);
```

Then to render ImGui, call `ImGuiSDL::Render` after calling `ImGui::Render`. You probably want to do this after all other rendering that happens in your project:

```cpp
ImGuiSDL::Render(ImGui::GetDrawData());
```

To cleanup at exit, you can call `ImGuiSDL::Deinitialize`, but that doesn't do anything critical, so if you don't care about cleaning up memory at application exit, you don't need to call this.

## Render Result

![Render Result](https://i.imgur.com/UzUsUO2.png)

As you can see the results are not perfect, but this display is definitely good enough to be used as a debug UI which is the main use case for Dear ImGui anyways. The rendering is also done in a little bit simpler style than what Dear ImGui uses by default, mainly to increase the amount of rendering that can be done using rectangles.

## Requirements

The implementation doesn't rely on any non-standard SDL functionality, imgui_sdl.cpp simply includes SDL.h and imgui.h. You can easily change these two includes to point to the correct locations if you use some sort of other include file scheme.

## Notes

Do note that this is just a renderer for SDL2. For input handling, you shoud use the [great SDL2 implementation](https://github.com/ocornut/imgui/blob/master/examples/imgui_impl_sdl.cpp) provided in the Dear ImGui repository, or you could of course roll your own event provider.

## Links

* [SDL2](https://www.libsdl.org/)
* [Dear Imgui](https://github.com/ocornut/imgui)