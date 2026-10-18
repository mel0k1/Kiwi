#pragma once

// Kiwi — cairo-drawn toolbar icons (Material 3, pastel peach palette).
//
// Windows ships GTK without an icon theme, so icon-name lookups render as
// broken-image fallbacks ("icons from the past"). Every glyph Kiwi needs is
// drawn here with plain cairo paths and handed to GTK as a GdkPixbuf — no
// theme, no assets, no network. Two tints: ink (#2C3E50) for enabled controls
// and muted (#85929E) for disabled ones.

#include <gtk/gtk.h>

namespace kiwi {

enum class Icon {
    Back,
    Forward,
    Reload,
    Plus,
    Close,
    Find,
    ChevronUp,
    ChevronDown,
    Download,
    Gear,
    Star,
    StarFilled,
    Stop,
    Shield
};

// Returns a cached 16x16 (or requested size) pixbuf. Muted tint for disabled
// controls. Do not free — pixbufs live for the process lifetime.
GdkPixbuf* icon_pixbuf(Icon icon, int size = 16, bool muted = false);

// GtkImage preloaded with the given icon.
GtkWidget* icon_image(Icon icon, int size = 16, bool muted = false);

// Flat toolbar button with a cairo icon that swaps to the muted tint when the
// button becomes insensitive.
GtkWidget* icon_button(Icon icon, const char* tooltip, int size = 16);

// Swaps the pixbuf of an icon_button's GtkImage according to sensitivity.
void icon_button_sync(GtkWidget* button, GtkWidget* image);

}
