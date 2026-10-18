#pragma once

#include <gtk/gtk.h>

#include <functional>

#include "mustela/view.hpp"

namespace kiwi {

using ShortcutHandler = std::function<bool(guint keyval, GdkModifierType mods)>;

GtkWidget* view_page_widget(mustela::View* view, GtkWindow* parent);

void install_shortcuts(GtkWindow* window, ShortcutHandler handler);

}
