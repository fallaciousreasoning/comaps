#include "drape_frontend/glacier_mask.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace df
{
namespace
{
// A tile is a few hundred pixels wide, so a cell is about a pixel.
int constexpr kGridSize = 512;
}  // namespace

GlacierMask::GlacierMask(m2::RectD const & tileRect)
  : m_tileRect(tileRect)
  , m_cellSize(tileRect.SizeX() / kGridSize)
  , m_cells(kGridSize * kGridSize, 0)
{}

void GlacierMask::AddTriangle(m2::PointD const & p1, m2::PointD const & p2, m2::PointD const & p3)
{
  auto const toGrid = [this](m2::PointD const & p)
  { return m2::PointD((p.x - m_tileRect.minX()) / m_cellSize, (p.y - m_tileRect.minY()) / m_cellSize); };
  m2::PointD const points[] = {toGrid(p1), toGrid(p2), toGrid(p3)};

  auto const [minY, maxY] = std::minmax({points[0].y, points[1].y, points[2].y});
  int const firstRow = std::max(0, static_cast<int>(std::floor(minY)));
  int const lastRow = std::min(kGridSize - 1, static_cast<int>(std::ceil(maxY)));

  for (int row = firstRow; row <= lastRow; ++row)
  {
    double const y = row + 0.5;
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    for (size_t i = 0; i < 3; ++i)
    {
      auto const & a = points[i];
      auto const & b = points[(i + 1) % 3];
      if ((a.y <= y) == (b.y <= y))
        continue;
      double const x = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
      minX = std::min(minX, x);
      maxX = std::max(maxX, x);
    }

    if (minX > maxX)
      continue;

    int const firstColumn = std::max(0, static_cast<int>(std::floor(minX)));
    int const lastColumn = std::min(kGridSize - 1, static_cast<int>(std::floor(maxX)));
    if (firstColumn <= lastColumn)
      std::fill_n(m_cells.begin() + row * kGridSize + firstColumn, lastColumn - firstColumn + 1, uint8_t{1});
  }
}

bool GlacierMask::IsInside(m2::PointD const & point) const
{
  int const column = static_cast<int>((point.x - m_tileRect.minX()) / m_cellSize);
  int const row = static_cast<int>((point.y - m_tileRect.minY()) / m_cellSize);
  if (column < 0 || row < 0 || column >= kGridSize || row >= kGridSize)
    return false;
  return m_cells[row * kGridSize + column] != 0;
}

GlacierMask::Runs GlacierMask::Split(m2::SharedSpline const & spline) const
{
  Runs runs;
  auto const & path = spline->GetPath();
  if (path.size() < 2)
    return runs;

  std::vector<m2::PointD> run = {path.front()};
  bool onGlacier = IsInside(path.front());

  auto const addPoint = [&](m2::PointD const & point)
  {
    bool const inside = IsInside(point);
    run.push_back(point);
    if (inside == onGlacier)
      return;

    runs.emplace_back(m2::SharedSpline(std::move(run)), onGlacier);
    run = {point};
    onGlacier = inside;
  };

  for (size_t i = 1; i < path.size(); ++i)
  {
    auto const & from = path[i - 1];
    auto const & to = path[i];
    int const steps = std::max(1, static_cast<int>(std::ceil(from.Length(to) / m_cellSize)));
    for (int step = 1; step <= steps; ++step)
      addPoint(from + (to - from) * (static_cast<double>(step) / steps));
  }

  if (run.size() > 1)
    runs.emplace_back(m2::SharedSpline(std::move(run)), onGlacier);
  return runs;
}
}  // namespace df
