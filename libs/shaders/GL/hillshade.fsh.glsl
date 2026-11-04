layout (location = 0) in vec2 v_colorTexCoords;

layout (location = 0) out vec4 v_FragColor;

layout (binding = 0) uniform UBO
{
  mat4 u_modelView;
  mat4 u_projection;
  mat4 u_pivotTransform;
  vec2 u_contrastGamma;
  float u_opacity;
  float u_zScale;
  float u_interpolation;
  float u_isOutlinePass;
};

layout (binding = 1) uniform sampler2D u_colorTex;

const float kTileSize = 256.0;

// Cubic B-spline filtering from 4 bilinear taps, as tiles are often heavily magnified.
float sampleBicubic(vec2 texCoords)
{
  vec2 position = texCoords * kTileSize - 0.5;
  vec2 f = fract(position);
  vec2 texel = position - f;
  vec2 f2 = f * f;
  vec2 f3 = f2 * f;
  vec2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
  vec2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
  vec2 w3 = f3 / 6.0;
  vec2 w2 = 1.0 - w0 - w1 - w3;
  vec2 g0 = w0 + w1;
  vec2 g1 = w2 + w3;
  vec2 p0 = (texel + w1 / g0 - 0.5) / kTileSize;
  vec2 p1 = (texel + w3 / g1 + 1.5) / kTileSize;
  return g0.y * (g0.x * texture(u_colorTex, p0).r + g1.x * texture(u_colorTex, vec2(p1.x, p0.y)).r) +
         g1.y * (g0.x * texture(u_colorTex, vec2(p0.x, p1.y)).r + g1.x * texture(u_colorTex, p1).r);
}

void main()
{
  // Tiles store signed shading with 128 meaning flat ground.
  float shade = sampleBicubic(v_colorTexCoords) * (255.0 / 127.0) - (128.0 / 127.0);
  v_FragColor = vec4(vec3(step(0.0, shade)), abs(shade) * u_opacity);
}
