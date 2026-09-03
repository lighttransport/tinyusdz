#version 450
layout(location = 0) out vec2 vNdc;
void main() {
  const vec2 p[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0),
                            vec2(-1.0, 3.0));
  vNdc = p[gl_VertexIndex];
  gl_Position = vec4(vNdc, 1.0, 1.0);
}
