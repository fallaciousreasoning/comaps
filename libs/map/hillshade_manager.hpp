#pragma once

#include "drape_frontend/drape_engine_safe_ptr.hpp"

#include "drape/pointers.hpp"

#include <functional>
#include <memory>

namespace df
{
class DrapeEngine;
}  // namespace df

// Keeps the offline hillshade bundle on disk, downloading it the first time the layer is enabled.
class HillshadeManager final
{
public:
  enum class HillshadeState
  {
    Disabled,
    Downloading,
    Enabled,
    DownloadFailed
  };

  using HillshadeStateChangedFn = std::function<void(HillshadeState)>;

  HillshadeManager();

  HillshadeState GetState() const { return m_state; }
  void SetStateListener(HillshadeStateChangedFn const & onStateChangedFn);

  void SetDrapeEngine(ref_ptr<df::DrapeEngine> engine);

  void SetEnabled(bool enabled);
  bool IsEnabled() const;

private:
  void ChangeState(HillshadeState newState);
  void StartDownload();
  void OnDownloadFinished(bool success);

  df::DrapeEngineSafePtr m_drapeEngine;
  HillshadeStateChangedFn m_onStateChangedFn;
  HillshadeState m_state = HillshadeState::Disabled;
  bool m_isDownloadRunning = false;
  // Lets a finished download notice that the manager has gone.
  std::shared_ptr<bool> m_lifetime = std::make_shared<bool>(true);
};
