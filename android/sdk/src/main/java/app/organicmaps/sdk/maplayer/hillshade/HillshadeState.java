package app.organicmaps.sdk.maplayer.hillshade;

// Keep in sync with HillshadeManager::HillshadeState in libs/map/hillshade_manager.hpp.
public enum HillshadeState
{
  DISABLED,
  DOWNLOADING,
  ENABLED,
  DOWNLOAD_FAILED
}
