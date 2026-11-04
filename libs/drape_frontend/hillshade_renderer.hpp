#pragma once

#include "drape/mesh_object.hpp"
#include "drape/pointers.hpp"
#include "drape/static_texture.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace dp
{
class GraphicsContext;
}  // namespace dp

namespace gpu
{
class ProgramManager;
}  // namespace gpu

class FileReader;
class ScreenBase;

namespace df
{
class FrameValues;

// Draws pre-shaded terrain tiles over area fills, and under lines, from an optional offline
// tile bundle in the writable directory. See tools/python/hillshade_bundle.py for the format.
class HillshadeRenderer
{
public:
  explicit HillshadeRenderer(ref_ptr<dp::GraphicsContext> context);
  ~HillshadeRenderer();

  void SetEnabled(bool enabled);

  void Render(ref_ptr<dp::GraphicsContext> context, ref_ptr<gpu::ProgramManager> mng, ScreenBase const & screen,
              FrameValues const & frameValues);

private:
  struct TileData
  {
    uint64_t m_offset = 0;
    uint32_t m_size = 0;
  };

  struct CachedTile
  {
    drape_ptr<dp::StaticTexture> m_texture;
    uint64_t m_lastUsedFrame = 0;
  };

  void LoadIndex();
  ref_ptr<dp::StaticTexture> GetTexture(ref_ptr<dp::GraphicsContext> context, uint64_t key, TileData const & tile);
  void EvictTiles();

  dp::MeshObject m_quad;
  std::unique_ptr<FileReader> m_reader;
  std::unordered_map<uint64_t, TileData> m_index;
  std::unordered_map<uint64_t, CachedTile> m_tiles;
  uint64_t m_frame = 0;
  bool m_enabled = false;
  int m_minZoom = 0;
  int m_maxZoom = 0;
};
}  // namespace df
