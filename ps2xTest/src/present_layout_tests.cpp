#include "MiniTest.h"
#include "runtime/present_layout.h"

using PresentLayout::computePresentDstRect;
using PresentLayout::PresentRect;

void register_present_layout_tests()
{
    MiniTest::Case("PresentLayout", [](TestCase &tc)
    {
        // A window whose aspect matches the display region is filled exactly.
        // This is the native desktop case (640x448 window): the decoded buffer,
        // stretched into this rectangle via the caller's srcRect, covers the
        // whole window instead of being pillarboxed at its own column count.
        tc.Run("display region fills a window of the same aspect", [](TestCase &t)
        {
            const PresentRect r = computePresentDstRect(640.0f, 448.0f, 640.0f, 448.0f);
            t.Equals(r.x, 0.0f, "matching-aspect dst x must be 0");
            t.Equals(r.y, 0.0f, "matching-aspect dst y must be 0");
            t.Equals(r.width, 640.0f, "matching-aspect dst width must fill the window");
            t.Equals(r.height, 448.0f, "matching-aspect dst height must fill the window");
        });

        // A window WIDER than the display aspect pillarboxes (bars left/right)
        // rather than stretching. This is the Vita window's shape: 960x544 is
        // wider than the 640x448 region, so it pillarboxes exactly like this.
        // The bar widths at 960x544 are non-integer, so this pins the property
        // with clean (power-of-two-scale) arithmetic instead.
        tc.Run("wider window pillarboxes without stretching (Vita-shaped)", [](TestCase &t)
        {
            const PresentRect r = computePresentDstRect(640.0f, 448.0f, 1600.0f, 896.0f);
            t.Equals(r.x, 160.0f, "wide-window dst must be inset horizontally (pillarbox)");
            t.Equals(r.y, 0.0f, "wide-window dst y must be 0");
            t.Equals(r.width, 1280.0f, "wide-window dst width must keep the region aspect, not fill");
            t.Equals(r.height, 896.0f, "wide-window dst height must scale to the window height");
        });

        // A window TALLER than the display aspect letterboxes (bars top/bottom).
        tc.Run("taller window letterboxes top and bottom", [](TestCase &t)
        {
            const PresentRect r = computePresentDstRect(640.0f, 448.0f, 640.0f, 896.0f);
            t.Equals(r.x, 0.0f, "tall-window dst x must be 0");
            t.Equals(r.y, 224.0f, "tall-window dst must be inset vertically (letterbox)");
            t.Equals(r.width, 640.0f, "tall-window dst width must scale to the window width");
            t.Equals(r.height, 448.0f, "tall-window dst height must keep the region aspect, not fill");
        });

        // The preserved aspect is the DISPLAY REGION's, not the window's: a
        // narrow region pillarboxes. This is why the caller passes the full
        // display width (FB_WIDTH), not the decoded narrow column count.
        tc.Run("aspect follows the display region, not the window", [](TestCase &t)
        {
            const PresentRect r = computePresentDstRect(320.0f, 448.0f, 640.0f, 448.0f);
            t.Equals(r.x, 160.0f, "narrow-region dst must pillarbox (params are live)");
            t.Equals(r.y, 0.0f, "narrow-region dst y must be 0");
            t.Equals(r.width, 320.0f, "narrow-region dst width must follow the region, not the window");
            t.Equals(r.height, 448.0f, "narrow-region dst height must equal the window height");
        });

        // A degenerate display region -- a non-positive width or height -- cannot
        // be fit or centered (scaling would divide by zero), so the helper falls
        // back to filling the window. This case pins the WIDTH axis of the guard;
        // without it a wrong fallback (e.g. {0,0,0,0}) passes every aspect case
        // above, since none of them exercises a non-positive region. Its height
        // sibling below pins the other axis.
        tc.Run("degenerate display width fills the window", [](TestCase &t)
        {
            const PresentRect r = computePresentDstRect(0.0f, 448.0f, 640.0f, 448.0f);
            t.Equals(r.x, 0.0f, "degenerate-width dst x must be 0");
            t.Equals(r.y, 0.0f, "degenerate-width dst y must be 0");
            t.Equals(r.width, 640.0f, "degenerate-width dst width must fill the window");
            t.Equals(r.height, 448.0f, "degenerate-width dst height must fill the window");
        });

        // Symmetric to the degenerate-width case: a non-positive display HEIGHT
        // is equally unfittable (scaling would divide by zero on the height
        // axis), so the guard falls back to filling the window here too. Without
        // this case a guard weakened to test only width (`if (dispWidth <= 0.0f)`)
        // passes every case above while leaving a dispHeight==0 caller to produce
        // a divide-by-zero-derived rect; this case fails that mutation.
        tc.Run("degenerate display height fills the window", [](TestCase &t)
        {
            const PresentRect r = computePresentDstRect(640.0f, 0.0f, 640.0f, 448.0f);
            t.Equals(r.x, 0.0f, "degenerate-height dst x must be 0");
            t.Equals(r.y, 0.0f, "degenerate-height dst y must be 0");
            t.Equals(r.width, 640.0f, "degenerate-height dst width must fill the window");
            t.Equals(r.height, 448.0f, "degenerate-height dst height must fill the window");
        });
    });
}
