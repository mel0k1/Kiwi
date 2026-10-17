#pragma once

// Kiwi — Material Design 3 theming (pastel peach tonal palette, fixed per spec).
//
// Palette (strict, no invented colors):
//   Surface          #FFF5F0   window / panels background
//   Surface Variant  #FADBD8   inactive tabs, sidebars, table headers
//   Primary          #E67E22   active elements, accents
//   Primary (soft)   #E59866   hover accent, selection
//   On-Primary       #FFFFFF   text on primary
//   Outline          #EDBB99   1px borders and separators
//   Text Primary     #2C3E50
//   Text Secondary   #85929E
//   Press            #F5CBA7
//
// Geometry: radius 8 on buttons/entries, radius 4 on tab top corners,
// high density (paddings 4-8px). Solid fills only: no gradients, no
// shadows, no blur anywhere.

#include <gtk/gtk.h>

#include <glib/gstdio.h>

#include <string>

#ifndef KIWI_ASSET_DIR
#define KIWI_ASSET_DIR "/usr/local/share/kiwi"
#endif

namespace kiwi {

inline const char* theme_css() {
    return R"CSS(
window, dialog, messagedialog {
  background-color: #FFF5F0;
  color: #2C3E50;
}

label { color: #2C3E50; }
label.kiwi-secondary { color: #85929E; }
label.kiwi-dot-idle { color: #85929E; }
label.kiwi-dot-active { color: #E67E22; }

button {
  background-color: #FFF5F0;
  background-image: none;
  color: #2C3E50;
  border: 1px solid #EDBB99;
  border-radius: 8px;
  padding: 3px 8px;
  box-shadow: none;
  text-shadow: none;
}
button:hover { background-color: #FADBD8; }
button:active, button:checked { background-color: #F5CBA7; }
button:disabled { color: #85929E; border-color: #F5CBA7; background-color: #FFF5F0; }
button.flat, button.kiwi-flat {
  border-color: transparent;
  background-color: transparent;
  padding: 2px 6px;
}
button.flat:hover, button.kiwi-flat:hover { background-color: #FADBD8; }
button.flat:active, button.kiwi-flat:active { background-color: #F5CBA7; }
button.kiwi-accent {
  background-color: #E67E22;
  border-color: #E67E22;
  color: #FFFFFF;
}
button.kiwi-accent:hover { background-color: #E59866; border-color: #E59866; }
button.kiwi-accent:active { background-color: #F5CBA7; }

entry, spinbutton {
  background-color: #FFF5F0;
  background-image: none;
  color: #2C3E50;
  border: 1px solid #EDBB99;
  border-radius: 8px;
  padding: 3px 8px;
  box-shadow: none;
}
entry:focus, spinbutton:focus { border-color: #E59866; }
entry:disabled { color: #85929E; }
entry selection, spinbutton selection { background-color: #E59866; color: #FFFFFF; }
entry progress { background-color: #E59866; border-radius: 4px; }

combobox button.combo {
  background-color: #FFF5F0;
  background-image: none;
  border: 1px solid #EDBB99;
  border-radius: 8px;
  padding: 2px 6px;
  box-shadow: none;
}
combobox button.combo:hover { background-color: #FADBD8; }

notebook > header {
  background-color: #FADBD8;
  border: none;
  box-shadow: none;
}
notebook > header > tabs > tab {
  background-color: transparent;
  background-image: none;
  border-radius: 4px 4px 0 0;
  padding: 3px 4px;
  margin: 2px 1px 0 1px;
  color: #85929E;
  box-shadow: none;
}
notebook > header > tabs > tab:hover { background-color: #F5CBA7; }
notebook > header > tabs > tab:checked {
  background-color: #FFF5F0;
  color: #2C3E50;
}
notebook > header > tabs > tab button {
  background-color: transparent;
  background-image: none;
  border: none;
  border-radius: 4px;
  padding: 0 2px;
  min-width: 16px;
  min-height: 16px;
  box-shadow: none;
}
notebook > header > tabs > tab button:hover { background-color: #F5CBA7; }

treeview {
  background-color: #FFF5F0;
  color: #2C3E50;
}
treeview:selected, treeview:selected:focus {
  background-color: #E59866;
  color: #FFFFFF;
}
treeview header button {
  background-color: #FADBD8;
  background-image: none;
  color: #2C3E50;
  border: none;
  border-radius: 0;
  padding: 2px 6px;
  box-shadow: none;
}

.kiwi-status {
  color: #85929E;
  font-size: 90%;
}

.kiwi-findbar {
  background-color: #FFF5F0;
  border-bottom: 1px solid #EDBB99;
}

menu.kiwi-menu {
  background-color: #FFF5F0;
  border: 1px solid #EDBB99;
  border-radius: 8px;
  padding: 2px 0;
}
menu.kiwi-menu menuitem {
  background-color: transparent;
  border-radius: 4px;
  padding: 3px 8px;
  margin: 0 2px;
  color: #2C3E50;
  min-height: 18px;
}
menu.kiwi-menu menuitem:hover { background-color: #FADBD8; }
menu.kiwi-menu menuitem:disabled { color: #85929E; }
menu.kiwi-menu separator { margin: 2px 8px; background-color: #EDBB99; min-height: 1px; }

scrollbar {
  background-color: #FFF5F0;
}
scrollbar slider {
  background-color: #EDBB99;
  border-radius: 6px;
  min-width: 6px;
  min-height: 24px;
}
scrollbar slider:hover { background-color: #E59866; }

menu, .menu, popover.background {
  background-color: #FFF5F0;
  color: #2C3E50;
  border: 1px solid #EDBB99;
}
menuitem:hover { background-color: #FADBD8; }

checkbutton, radiobutton { color: #2C3E50; }
checkbutton check, radiobutton radio {
  border: 1px solid #EDBB99;
  background-color: #FFF5F0;
  color: #2C3E50;
}
checkbutton:checked check, radiobutton:checked radio {
  background-color: #E67E22;
  border-color: #E67E22;
  color: #FFFFFF;
}

separator { background-color: #EDBB99; min-height: 1px; min-width: 1px; }
)CSS";
}

inline void apply_theme() {
    static GtkCssProvider* provider = nullptr;
    if (provider) return;
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, theme_css(), -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

inline std::string kiwi_exe_dir() {
    gchar* link = g_file_read_link("/proc/self/exe", nullptr);
    if (!link) return std::string();
    gchar* dir = g_path_get_dirname(link);
    std::string out = dir ? dir : "";
    g_free(dir);
    g_free(link);
    return out;
}

inline void apply_app_icon() {
    const std::string dir = kiwi_exe_dir();
    const std::string candidates[] = {
        std::string(KIWI_ASSET_DIR) + "/logo.png",
        dir + "/../assets/logo.png",
        dir + "/assets/logo.png",
        std::string("assets/logo.png"),
    };
    for (const std::string& path : candidates) {
        if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
            gtk_window_set_default_icon_from_file(path.c_str(), nullptr);
            return;
        }
    }
}

}  // namespace kiwi
