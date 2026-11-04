package app.organicmaps.sdk.maplayer.hillshade;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

public class HillshadeManager
{
  public interface Listener
  {
    void onStateChanged(@NonNull HillshadeState state);
  }

  @Nullable
  private Listener mListener;

  public static boolean isEnabled()
  {
    return nativeIsEnabled();
  }

  public static void setEnabled(boolean isEnabled)
  {
    if (isEnabled != isEnabled())
      nativeSetEnabled(isEnabled);
  }

  public void initialize()
  {
    nativeAddListener();
  }

  public void attach(@NonNull Listener listener)
  {
    mListener = listener;
  }

  public void detach()
  {
    mListener = null;
  }

  // Called from JNI.
  @Keep
  @SuppressWarnings("unused")
  private void onStateChanged(int state)
  {
    if (mListener != null)
      mListener.onStateChanged(HillshadeState.values()[state]);
  }

  private native void nativeAddListener();
  private static native void nativeSetEnabled(boolean enabled);
  private static native boolean nativeIsEnabled();
}
