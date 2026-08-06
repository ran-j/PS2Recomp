#pragma once

#include <algorithm>

namespace PresentLayout
{
    // Plain geometry result, independent of any windowing/graphics type so it
    // can be unit-tested without a live presentation window.
    struct PresentRect
    {
        float x;
        float y;
        float width;
        float height;
    };

    // Fit the guest's intended display region (dispWidth x dispHeight -- the
    // full-screen picture the GS scans out, e.g. the full 640x448 NTSC display)
    // into the host window (screenWidth x screenHeight), preserving the display
    // region's aspect ratio and centering it. The region is scaled by the
    // largest factor that keeps it inside the window, so:
    //   * a window with the same aspect as the region is filled exactly;
    //   * a wider window pillarboxes (bars left/right);
    //   * a taller window letterboxes (bars top/bottom).
    // The caller draws the decoded source buffer into this rectangle, so a
    // buffer with fewer columns than the display (a narrow DISPLAY/DISPFB
    // buffer) is stretched across the full region and fills the window exactly
    // as a full-width buffer does, instead of being shrunk to its own column
    // count and pillarboxed. dispWidth/dispHeight must be positive; a degenerate
    // region falls back to filling the window.
    inline PresentRect computePresentDstRect(float dispWidth, float dispHeight,
                                             float screenWidth, float screenHeight)
    {
        if (dispWidth <= 0.0f || dispHeight <= 0.0f)
        {
            return PresentRect{0.0f, 0.0f, screenWidth, screenHeight};
        }
        const float scale = std::min(screenWidth / dispWidth, screenHeight / dispHeight);
        const float scaledWidth = dispWidth * scale;
        const float scaledHeight = dispHeight * scale;
        return PresentRect{(screenWidth - scaledWidth) * 0.5f,
                           (screenHeight - scaledHeight) * 0.5f,
                           scaledWidth, scaledHeight};
    }
}
