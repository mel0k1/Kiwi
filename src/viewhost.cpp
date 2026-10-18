#include "viewhost.hpp"

namespace kiwi {

GtkWidget* view_page_widget(mustela::View* view, GtkWindow*) {
    return static_cast<GtkWidget*>(view->native_widget());
}

void install_shortcuts(GtkWindow*, ShortcutHandler) {}

}
