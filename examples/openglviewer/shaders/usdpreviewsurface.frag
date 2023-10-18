// Simple UsdPreviewSurface fragment shader.

// TODO
// - [ ] UV transform


in vec2 interpolated_uv0;
uniform sampler2D base_color_texture;
uniform vec4 base_color_factor;

out vec4 output_color;

void main()
{
  vec4 base_color_result = texture(base_color_texture, interpolated_uv0) + base_color_factor;
  output_color = base_color_result;
}


