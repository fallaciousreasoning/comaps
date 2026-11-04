#include "drape_frontend/hillshade_renderer.hpp"

#include "drape_frontend/frame_values.hpp"
#include "drape_frontend/render_state_extension.hpp"

#include "shaders/program_manager.hpp"
#include "shaders/program_params.hpp"
#include "shaders/programs.hpp"

#include "drape/graphics_context.hpp"
#include "drape/hw_texture.hpp"
#include "drape/render_state.hpp"

#include "indexer/drawing_rule_def.hpp"
#include "indexer/map_style_reader.hpp"

#include "platform/platform.hpp"

#include "coding/file_reader.hpp"

#include "geometry/screenbase.hpp"

#include "base/logging.hpp"

#include "3party/stb_image/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace df
{
namespace
{
std::string_view constexpr kBundleFileName = "hillshade.tilebundle";

// Bundle record header: magic(4) + z(1) + x(4) + y(4) + size(4), all little-endian.
uint32_t constexpr kBundleMagic = 0x52595054;  // "TPYR"
size_t constexpr kRecordHeaderSize = 17;

int constexpr kTileSize = 256;
// Border of neighbouring shading in each tile, which keeps filtering seamless across tiles.
float constexpr kTileGutter = 2.0f / kTileSize;
size_t constexpr kMaxCachedTiles = 192;

// Shading softens as it's magnified past the source resolution, so it fades out over these zooms.
float constexpr kOpacity = 0.6f;
float constexpr kMinOpacity = 0.25f;
double constexpr kFadeStartZooms = 0.0;
double constexpr kFadeEndZooms = 4.0;

// Everything below this depth is an area fill, so lines keep their full colour.
float constexpr kHillshadeDepth = static_cast<float>(drule::kMinLayeredDepthFg);

uint64_t MakeKey(uint32_t z, uint32_t x, uint32_t y)
{
  return (static_cast<uint64_t>(z) << 56) | (static_cast<uint64_t>(x) << 28) | y;
}

uint32_t ReadU32(char const * data)
{
  uint32_t value;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

int ClampTileIndex(double index, int tilesCount)
{
  return std::clamp(static_cast<int>(std::floor(index)), 0, tilesCount - 1);
}
}  // namespace

HillshadeRenderer::HillshadeRenderer(ref_ptr<dp::GraphicsContext> context)
  : m_quad(context, dp::MeshObject::DrawPrimitive::TriangleStrip, "Hillshade")
{
  auto const z = kHillshadeDepth;
  auto const t0 = kTileGutter;
  auto const t1 = 1.0f - kTileGutter;
  std::vector<float> vertices = {-1.0f, 1.0f, z, t0, t0, 1.0f,  1.0f,  z, t1, t0,
                                 -1.0f, -1.0f, z, t0, t1, 1.0f, -1.0f, z, t1, t1};
  m_quad.SetBuffer(0 /* bufferInd */, std::move(vertices), sizeof(float) * 5 /* stride */);
  m_quad.SetAttribute("a_position", 0 /* bufferInd */, 0 /* offset */, 3 /* componentsCount */);
  m_quad.SetAttribute("a_colorTexCoords", 0 /* bufferInd */, sizeof(float) * 3 /* offset */, 2 /* componentsCount */);
}

HillshadeRenderer::~HillshadeRenderer() = default;

void HillshadeRenderer::SetEnabled(bool enabled)
{
  m_enabled = enabled;
  if (m_enabled && m_index.empty())
    LoadIndex();
}

void HillshadeRenderer::LoadIndex()
{
  std::string const path = GetPlatform().WritableDir() + std::string(kBundleFileName);
  if (!GetPlatform().IsFileExistsByFullPath(path))
    return;

  try
  {
    auto reader = std::make_unique<FileReader>(path);
    uint64_t const fileSize = reader->Size();
    uint64_t offset = 0;
    char header[kRecordHeaderSize];
    m_minZoom = std::numeric_limits<int>::max();
    while (offset + kRecordHeaderSize <= fileSize)
    {
      reader->Read(offset, header, kRecordHeaderSize);
      uint32_t const size = ReadU32(header + 13);
      if (ReadU32(header) != kBundleMagic || offset + kRecordHeaderSize + size > fileSize)
      {
        LOG(LWARNING, ("Hillshade bundle is corrupted at offset", offset));
        break;
      }

      auto const z = static_cast<uint8_t>(header[4]);
      m_index[MakeKey(z, ReadU32(header + 5), ReadU32(header + 9))] = {offset + kRecordHeaderSize, size};
      m_minZoom = std::min<int>(m_minZoom, z);
      m_maxZoom = std::max<int>(m_maxZoom, z);
      offset += kRecordHeaderSize + size;
    }
    m_reader = std::move(reader);
    LOG(LINFO, ("Hillshade bundle has", m_index.size(), "tiles, zooms", m_minZoom, "to", m_maxZoom));
  }
  catch (Reader::Exception const & e)
  {
    LOG(LWARNING, ("Can't read hillshade bundle", path, e.Msg()));
    m_index.clear();
  }
}

ref_ptr<dp::StaticTexture> HillshadeRenderer::GetTexture(ref_ptr<dp::GraphicsContext> context, uint64_t key,
                                                         TileData const & tile)
{
  auto const it = m_tiles.find(key);
  if (it != m_tiles.end())
  {
    it->second.m_lastUsedFrame = m_frame;
    return make_ref(it->second.m_texture);
  }

  std::vector<unsigned char> encoded(tile.m_size);
  try
  {
    m_reader->Read(tile.m_offset, encoded.data(), encoded.size());
  }
  catch (Reader::Exception const & e)
  {
    LOG(LWARNING, ("Can't read hillshade tile", e.Msg()));
    return nullptr;
  }

  int width, height, channels;
  unsigned char * pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height,
                                                 &channels, 1 /* desired_channels */);
  if (pixels == nullptr || width != kTileSize || height != kTileSize)
  {
    LOG(LWARNING, ("Can't decode hillshade tile"));
    stbi_image_free(pixels);
    return nullptr;
  }

  dp::Texture::Params params;
  params.m_allocator = dp::GetDefaultAllocator(context);
  params.m_format = dp::TextureFormat::Red;
  params.m_width = kTileSize;
  params.m_height = kTileSize;

  auto texture = make_unique_dp<dp::StaticTexture>();
  texture->Create(context, params, make_ref(pixels));
  stbi_image_free(pixels);

  auto & cached = m_tiles[key];
  cached.m_texture = std::move(texture);
  cached.m_lastUsedFrame = m_frame;
  return make_ref(cached.m_texture);
}

void HillshadeRenderer::EvictTiles()
{
  while (m_tiles.size() > kMaxCachedTiles)
  {
    auto const oldest = std::min_element(m_tiles.begin(), m_tiles.end(), [](auto const & lhs, auto const & rhs)
    { return lhs.second.m_lastUsedFrame < rhs.second.m_lastUsedFrame; });
    if (oldest->second.m_lastUsedFrame == m_frame)
      break;
    m_tiles.erase(oldest);
  }
}

void HillshadeRenderer::Render(ref_ptr<dp::GraphicsContext> context, ref_ptr<gpu::ProgramManager> mng,
                               ScreenBase const & screen, FrameValues const & frameValues)
{
  if (!m_enabled || m_index.empty() || MapStyleIsDark(GetStyleReader().GetCurrentStyle()))
    return;

  ++m_frame;

  // Pick the first zoom whose tiles aren't drawn smaller than their native size.
  double constexpr kWorldSize = 360.0;
  double const idealZoom = std::log2(kWorldSize / (kTileSize * screen.GetScale()));
  int const zoom = std::clamp(static_cast<int>(std::ceil(idealZoom)), m_minZoom, m_maxZoom);

  int const tilesCount = 1 << zoom;
  double const tileSize = kWorldSize / tilesCount;
  m2::RectD const rect = screen.ClipRect();
  int const minX = ClampTileIndex((rect.minX() + 180.0) / tileSize, tilesCount);
  int const maxX = ClampTileIndex((rect.maxX() + 180.0) / tileSize, tilesCount);
  int const minY = ClampTileIndex((180.0 - rect.maxY()) / tileSize, tilesCount);
  int const maxY = ClampTileIndex((180.0 - rect.minY()) / tileSize, tilesCount);

  auto state = CreateRenderState(gpu::Program::Hillshade, DepthLayer::GeometryLayer);
  state.SetDepthTestEnabled(true);
  state.SetBlending(dp::Blending(true));

  gpu::MapProgramParams params;
  frameValues.SetTo(params);
  auto const fade = std::clamp((idealZoom - m_maxZoom - kFadeStartZooms) / (kFadeEndZooms - kFadeStartZooms), 0.0, 1.0);
  params.m_opacity = kOpacity + (kMinOpacity - kOpacity) * static_cast<float>(fade);

  auto program = mng->GetProgram(gpu::Program::Hillshade);
  for (int y = minY; y <= maxY; ++y)
  {
    for (int x = minX; x <= maxX; ++x)
    {
      uint64_t const key = MakeKey(zoom, x, y);
      auto const tile = m_index.find(key);
      if (tile == m_index.end())
        continue;

      auto texture = GetTexture(context, key, tile->second);
      if (texture == nullptr)
        continue;

      m2::PointD const center((x + 0.5) * tileSize - 180.0, 180.0 - (y + 0.5) * tileSize);
      auto const modelView = screen.GetModelView(center, 2.0 / tileSize);
      params.m_modelView = glsl::make_mat4(modelView.m_data);

      state.SetTexture("u_colorTex", texture);
      m_quad.Render(context, program, state, mng->GetParamsSetter(), params);
    }
  }

  EvictTiles();
}
}  // namespace df
