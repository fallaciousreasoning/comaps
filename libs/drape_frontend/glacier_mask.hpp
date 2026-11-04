#pragma once

#include "geometry/point2d.hpp"
#include "geometry/rect2d.hpp"
#include "geometry/spline.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace df
{
// Grid of the glaciers in a tile, used to colour isolines differently on ice.
class GlacierMask
{
public:
  using Runs = std::vector<std::pair<m2::SharedSpline, bool /* onGlacier */>>;

  explicit GlacierMask(m2::RectD const & tileRect);

  void AddTriangle(m2::PointD const & p1, m2::PointD const & p2, m2::PointD const & p3);

  // Splits a line into consecutive runs that are all on, or all off, a glacier.
  Runs Split(m2::SharedSpline const & spline) const;

private:
  bool IsInside(m2::PointD const & point) const;

  m2::RectD m_tileRect;
  double m_cellSize;
  std::vector<uint8_t> m_cells;
};
}  // namespace df
