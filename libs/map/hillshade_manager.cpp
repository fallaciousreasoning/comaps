#include "map/hillshade_manager.hpp"

#include "drape_frontend/drape_engine.hpp"

#include "platform/http_client.hpp"
#include "platform/platform.hpp"

#include "coding/internal/file_data.hpp"

#include "base/logging.hpp"

#include <string>
#include <utility>

namespace
{
std::string_view constexpr kBundleUrl = "https://data.topos.nz/hillshade-12.tilebundle";
std::string_view constexpr kBundleFileName = "hillshade.tilebundle";
std::string_view constexpr kTempFileExtension = ".tmp";
double constexpr kDownloadTimeoutSec = 300.0;

std::string GetBundlePath()
{
  return GetPlatform().WritableDir() + std::string(kBundleFileName);
}
}  // namespace

HillshadeManager::HillshadeManager() = default;

void HillshadeManager::SetStateListener(HillshadeStateChangedFn const & onStateChangedFn)
{
  m_onStateChangedFn = onStateChangedFn;
}

void HillshadeManager::SetDrapeEngine(ref_ptr<df::DrapeEngine> engine)
{
  m_drapeEngine.Set(engine);
  m_drapeEngine.SafeCall(&df::DrapeEngine::EnableHillshade, m_state == HillshadeState::Enabled);
}

bool HillshadeManager::IsEnabled() const
{
  return m_state == HillshadeState::Enabled || m_state == HillshadeState::Downloading;
}

void HillshadeManager::SetEnabled(bool enabled)
{
  if (!enabled)
  {
    ChangeState(HillshadeState::Disabled);
    m_drapeEngine.SafeCall(&df::DrapeEngine::EnableHillshade, false);
    return;
  }

  if (Platform::IsFileExistsByFullPath(GetBundlePath()))
  {
    ChangeState(HillshadeState::Enabled);
    m_drapeEngine.SafeCall(&df::DrapeEngine::EnableHillshade, true);
    return;
  }

  ChangeState(HillshadeState::Downloading);
  StartDownload();
}

void HillshadeManager::ChangeState(HillshadeState newState)
{
  if (m_state == newState)
    return;
  m_state = newState;
  if (m_onStateChangedFn != nullptr)
    m_onStateChangedFn(newState);
}

void HillshadeManager::StartDownload()
{
  if (m_isDownloadRunning)
    return;
  m_isDownloadRunning = true;

  GetPlatform().RunTask(Platform::Thread::Network, [this, lifetime = std::weak_ptr<bool>(m_lifetime)]()
  {
    std::string const path = GetBundlePath();
    std::string const tempPath = path + std::string(kTempFileExtension);

    platform::HttpClient request{std::string(kBundleUrl)};
    request.SetReceivedFile(tempPath);
    request.SetTimeout(kDownloadTimeoutSec);

    bool success = request.RunHttpRequest() && request.ErrorCode() == 200;
    if (success)
      success = base::RenameFileX(tempPath, path);
    else
      LOG(LWARNING, ("Hillshade download failed with code", request.ErrorCode()));

    if (!success)
      base::DeleteFileX(tempPath);

    GetPlatform().RunTask(Platform::Thread::Gui, [this, lifetime, success]()
    {
      if (!lifetime.expired())
        OnDownloadFinished(success);
    });
  });
}

void HillshadeManager::OnDownloadFinished(bool success)
{
  m_isDownloadRunning = false;

  // The layer may have been switched off in the meantime.
  if (m_state != HillshadeState::Downloading)
    return;

  ChangeState(success ? HillshadeState::Enabled : HillshadeState::DownloadFailed);
  if (success)
    m_drapeEngine.SafeCall(&df::DrapeEngine::EnableHillshade, true);
}
