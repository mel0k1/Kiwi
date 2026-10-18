#include "icons.hpp"

#include <cairo.h>

#include <cmath>
#include <map>
#include <tuple>
#include <utility>

namespace kiwi {

namespace {

constexpr const char* kInk = "#2C3E50";
constexpr const char* kMuted = "#85929E";

struct Rgb {
    double r;
    double g;
    double b;
};

Rgb parse_hex(const char* hex) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    Rgb out{0, 0, 0};
    if (hex[0] != '#') return out;
    out.r = (nibble(hex[1]) * 16 + nibble(hex[2])) / 255.0;
    out.g = (nibble(hex[3]) * 16 + nibble(hex[4])) / 255.0;
    out.b = (nibble(hex[5]) * 16 + nibble(hex[6])) / 255.0;
    return out;
}

using Draw = void (*)(cairo_t*, double);

void stroke_path(cairo_t* cr, double size) {
    cairo_set_line_width(cr, size >= 20 ? 1.8 : 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
}

void draw_back(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.62, s * 0.20);
    cairo_line_to(cr, s * 0.28, s * 0.50);
    cairo_line_to(cr, s * 0.62, s * 0.80);
    stroke_path(cr, s);
}

void draw_forward(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.38, s * 0.20);
    cairo_line_to(cr, s * 0.72, s * 0.50);
    cairo_line_to(cr, s * 0.38, s * 0.80);
    stroke_path(cr, s);
}

void draw_reload(cairo_t* cr, double s) {
    cairo_arc(cr, s * 0.50, s * 0.52, s * 0.28, -M_PI * 0.25, M_PI * 1.15);
    stroke_path(cr, s);
    // arrowhead at the end of the arc (top right)
    cairo_move_to(cr, s * 0.82, s * 0.30);
    cairo_line_to(cr, s * 0.78, s * 0.52);
    cairo_line_to(cr, s * 0.58, s * 0.46);
    stroke_path(cr, s);
}

void draw_plus(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.50, s * 0.18);
    cairo_line_to(cr, s * 0.50, s * 0.82);
    cairo_move_to(cr, s * 0.18, s * 0.50);
    cairo_line_to(cr, s * 0.82, s * 0.50);
    stroke_path(cr, s);
}

void draw_close(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.22, s * 0.22);
    cairo_line_to(cr, s * 0.78, s * 0.78);
    cairo_move_to(cr, s * 0.78, s * 0.22);
    cairo_line_to(cr, s * 0.22, s * 0.78);
    stroke_path(cr, s);
}

void draw_find(cairo_t* cr, double s) {
    cairo_arc(cr, s * 0.42, s * 0.42, s * 0.24, 0, 2 * M_PI);
    stroke_path(cr, s);
    cairo_move_to(cr, s * 0.60, s * 0.60);
    cairo_line_to(cr, s * 0.82, s * 0.82);
    stroke_path(cr, s);
}

void draw_chevron_up(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.25, s * 0.60);
    cairo_line_to(cr, s * 0.50, s * 0.36);
    cairo_line_to(cr, s * 0.75, s * 0.60);
    stroke_path(cr, s);
}

void draw_chevron_down(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.25, s * 0.40);
    cairo_line_to(cr, s * 0.50, s * 0.64);
    cairo_line_to(cr, s * 0.75, s * 0.40);
    stroke_path(cr, s);
}

void draw_download(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.50, s * 0.16);
    cairo_line_to(cr, s * 0.50, s * 0.60);
    cairo_move_to(cr, s * 0.30, s * 0.42);
    cairo_line_to(cr, s * 0.50, s * 0.62);
    cairo_line_to(cr, s * 0.70, s * 0.42);
    stroke_path(cr, s);
    cairo_move_to(cr, s * 0.18, s * 0.80);
    cairo_line_to(cr, s * 0.82, s * 0.80);
    stroke_path(cr, s);
}

void draw_gear(cairo_t* cr, double s) {
    cairo_arc(cr, s * 0.50, s * 0.50, s * 0.17, 0, 2 * M_PI);
    stroke_path(cr, s);
    for (int i = 0; i < 8; ++i) {
        double angle = M_PI / 4.0 * i;
        double x1 = s * 0.50 + std::cos(angle) * s * 0.24;
        double y1 = s * 0.50 + std::sin(angle) * s * 0.24;
        double x2 = s * 0.50 + std::cos(angle) * s * 0.34;
        double y2 = s * 0.50 + std::sin(angle) * s * 0.34;
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
    }
    stroke_path(cr, s);
}

void star_points(cairo_t* cr, double s, double radius) {
    static const double kAngles[10] = {
        -M_PI / 2, -M_PI / 2 + M_PI / 5, -M_PI / 2 + 2 * M_PI / 5,
        -M_PI / 2 + 3 * M_PI / 5, -M_PI / 2 + 4 * M_PI / 5,
        -M_PI / 2 + 5 * M_PI / 5, -M_PI / 2 + 6 * M_PI / 5,
        -M_PI / 2 + 7 * M_PI / 5, -M_PI / 2 + 8 * M_PI / 5,
        -M_PI / 2 + 9 * M_PI / 5};
    static const double kRatio[10] = {1.0, 0.42, 1.0, 0.42, 1.0,
                                      0.42, 1.0, 0.42, 1.0, 0.42};
    for (int i = 0; i < 10; ++i) {
        double x = s * 0.5 + std::cos(kAngles[i]) * s * radius * kRatio[i];
        double y = s * 0.5 + std::sin(kAngles[i]) * s * radius * kRatio[i];
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_close_path(cr);
}

void draw_star(cairo_t* cr, double s) {
    star_points(cr, s, 0.38);
    stroke_path(cr, s);
}

void draw_star_filled(cairo_t* cr, double s) {
    star_points(cr, s, 0.38);
    cairo_fill(cr);
}

void draw_stop(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.24, s * 0.24);
    cairo_line_to(cr, s * 0.76, s * 0.76);
    cairo_move_to(cr, s * 0.76, s * 0.24);
    cairo_line_to(cr, s * 0.24, s * 0.76);
    stroke_path(cr, s);
}

void draw_shield(cairo_t* cr, double s) {
    cairo_move_to(cr, s * 0.50, s * 0.14);
    cairo_curve_to(cr, s * 0.68, s * 0.24, s * 0.78, s * 0.26, s * 0.82, s * 0.26);
    cairo_line_to(cr, s * 0.82, s * 0.48);
    cairo_curve_to(cr, s * 0.82, s * 0.68, s * 0.66, s * 0.82, s * 0.50, s * 0.88);
    cairo_curve_to(cr, s * 0.34, s * 0.82, s * 0.18, s * 0.68, s * 0.18, s * 0.48);
    cairo_line_to(cr, s * 0.18, s * 0.26);
    cairo_curve_to(cr, s * 0.22, s * 0.26, s * 0.32, s * 0.24, s * 0.50, s * 0.14);
    stroke_path(cr, s);
}

const Draw kDraws[] = {
    draw_back, draw_forward, draw_reload, draw_plus, draw_close, draw_find,
    draw_chevron_up, draw_chevron_down, draw_download, draw_gear, draw_star,
    draw_star_filled, draw_stop, draw_shield};

struct PixbufKey {
    Icon icon;
    int size;
    bool muted;
    bool operator<(const PixbufKey& other) const {
        return std::tie(icon, size, muted) < std::tie(other.icon, other.size, other.muted);
    }
};

GdkPixbuf* render_icon(Icon icon, int size, bool muted) {
    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surface);
    Rgb tint = parse_hex(muted ? kMuted : kInk);
    cairo_set_source_rgb(cr, tint.r, tint.g, tint.b);
    size_t index = static_cast<size_t>(icon);
    if (index < sizeof(kDraws) / sizeof(kDraws[0]) && kDraws[index]) {
        kDraws[index](cr, static_cast<double>(size));
    }
    cairo_destroy(cr);
    GdkPixbuf* pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, size, size);
    cairo_surface_destroy(surface);
    return pixbuf;
}

std::map<PixbufKey, GdkPixbuf*>& pixbuf_cache() {
    static std::map<PixbufKey, GdkPixbuf*> cache;
    return cache;
}

void on_button_sensitive(GtkWidget* button, GParamSpec*, gpointer user_data) {
    GtkWidget* image = GTK_WIDGET(user_data);
    Icon icon = static_cast<Icon>(
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "kiwi-icon")));
    int size = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "kiwi-size"));
    bool sensitive = gtk_widget_get_sensitive(button) != FALSE;
    gtk_image_set_from_pixbuf(GTK_IMAGE(image), icon_pixbuf(icon, size, !sensitive));
}

}  // namespace

GdkPixbuf* icon_pixbuf(Icon icon, int size, bool muted) {
    PixbufKey key{icon, size, muted};
    std::map<PixbufKey, GdkPixbuf*>& cache = pixbuf_cache();
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    GdkPixbuf* pixbuf = render_icon(icon, size, muted);
    cache[key] = pixbuf;
    return pixbuf;
}

GtkWidget* icon_image(Icon icon, int size, bool muted) {
    return gtk_image_new_from_pixbuf(icon_pixbuf(icon, size, muted));
}

GtkWidget* icon_button(Icon icon, const char* tooltip, int size) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(button, FALSE);
    if (tooltip && *tooltip) gtk_widget_set_tooltip_text(button, tooltip);
    GtkWidget* image = gtk_image_new_from_pixbuf(icon_pixbuf(icon, size, false));
    gtk_container_add(GTK_CONTAINER(button), image);
    gtk_widget_show(image);
    g_object_set_data(G_OBJECT(button), "kiwi-icon", GINT_TO_POINTER(static_cast<int>(icon)));
    g_object_set_data(G_OBJECT(button), "kiwi-size", GINT_TO_POINTER(size));
    g_signal_connect(button, "notify::sensitive", G_CALLBACK(on_button_sensitive), image);
    return button;
}

void icon_button_sync(GtkWidget* button, GtkWidget* image) {
    if (!button || !image) return;
    on_button_sensitive(button, nullptr, image);
}

}  // namespace kiwi
