/* Mistbar - MIT License */

#pragma once

#include <cairo.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;
struct zwlr_screencopy_manager_v1;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct zxdg_decoration_manager_v1;
struct zxdg_toplevel_decoration_v1;

namespace mistbar::wl {

class Display;

struct OutputInfo {
    wl_output* output = nullptr;
    std::uint32_t global_name = 0;
    std::string name;
    int width = 1280;
    int height = 800;
    int scale = 1;
};

struct ToplevelInfo {
    zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    std::string title;
    std::string app_id;
    bool active = false;
    bool maximized = false;
    bool minimized = false;
    bool fullscreen = false;
};

struct CapturedFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    bool y_inverted = false;
    std::vector<std::uint8_t> pixels;
};

struct PointerEvent {
    enum class Type { Enter, Leave, Motion, ButtonPress, ButtonRelease, Axis };
    Type type = Type::Motion;
    double x = 0.0;
    double y = 0.0;
    std::uint32_t button = 0;
    std::uint32_t serial = 0;
    double axis_dx = 0.0;
    double axis_dy = 0.0;
};

struct KeyEvent {
    bool pressed = true;
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    std::uint32_t keycode = 0;
    std::uint32_t modifiers = 0;
    std::string utf8;
};

class ShmBufferPool {
public:
    ShmBufferPool() = default;
    ~ShmBufferPool();

    ShmBufferPool(const ShmBufferPool&) = delete;
    ShmBufferPool& operator=(const ShmBufferPool&) = delete;

    cairo_t* begin_frame(wl_shm* shm, int width, int height);
    void end_frame(wl_surface* surface, int buffer_scale = 1);
    void destroy();

private:
    struct Slot {
        wl_buffer* buffer = nullptr;
        void* data = nullptr;
        std::size_t size = 0;
        int width = 0;
        int height = 0;
        int stride = 0;
        bool busy = false;
        cairo_surface_t* cairo_surface = nullptr;
        cairo_t* cr = nullptr;
    };

    Slot slots_[3];
    int active_slot_ = -1;

    static void on_buffer_release(void* data, wl_buffer* buffer);
    static const wl_buffer_listener buffer_listener_;
};

class Surface {
public:
    using DrawFn    = std::function<void(cairo_t* cr, int width, int height)>;
    using PointerFn = std::function<void(const PointerEvent& ev)>;
    using KeyFn     = std::function<void(const KeyEvent& ev)>;
    using CloseFn   = std::function<void()>;

    explicit Surface(Display& display);
    virtual ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    void set_draw_callback(DrawFn fn) { on_draw_ = std::move(fn); }
    void set_pointer_callback(PointerFn fn) { on_pointer_ = std::move(fn); }
    void set_key_callback(KeyFn fn) { on_key_ = std::move(fn); }
    void set_close_callback(CloseFn fn) { on_close_ = std::move(fn); }

    void request_redraw();
    void render_now();

    wl_surface* wl_handle() const noexcept { return wl_surface_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool is_configured() const noexcept { return configured_; }
    bool is_visible() const noexcept { return visible_; }
    void set_visible(bool v) noexcept { visible_ = v; }

    void dispatch_pointer(const PointerEvent& ev);
    void dispatch_key(const KeyEvent& ev);

protected:
    Display& display_;
    wl_surface* wl_surface_ = nullptr;
    ShmBufferPool shm_pool_;
    int width_ = 0;
    int height_ = 0;
    int scale_ = 1;
    bool configured_ = false;
    bool visible_ = true;
    bool redraw_pending_ = false;
    bool frame_pending_ = false;

    DrawFn on_draw_;
    PointerFn on_pointer_;
    KeyFn on_key_;
    CloseFn on_close_;

    void schedule_frame_callback();
    static void on_frame_done(void* data, wl_callback* cb, std::uint32_t time);
    static void on_surface_enter(void* data, wl_surface* surf, wl_output* output);
    static void on_surface_leave(void* data, wl_surface* surf, wl_output* output);
    static const wl_callback_listener frame_listener_;
    static const wl_surface_listener surface_listener_;
};

class LayerSurface : public Surface {
public:
    enum Anchor : std::uint32_t {
        Top    = 1U << 0,
        Bottom = 1U << 1,
        Left   = 1U << 2,
        Right  = 1U << 3
    };
    enum class Layer { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
    enum class KeyboardMode { None = 0, Exclusive = 1, OnDemand = 2 };

    explicit LayerSurface(Display& display);
    ~LayerSurface() override;

    bool create(const std::string& ns,
                Layer layer,
                std::uint32_t anchors,
                int width,
                int height,
                int exclusive_zone = 0,
                KeyboardMode kb_mode = KeyboardMode::None,
                wl_output* output = nullptr);

    void set_margin(int top, int right, int bottom, int left);
    void set_exclusive_zone(int zone);
    void set_size(int width, int height);
    void set_keyboard_mode(KeyboardMode kb_mode);

    void hide();
    void show();

private:
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    std::string namespace_;
    Layer layer_ = Layer::Top;
    std::uint32_t anchors_ = 0;
    int req_width_ = 0;
    int req_height_ = 0;
    int exclusive_zone_ = 0;
    KeyboardMode kb_mode_ = KeyboardMode::None;
    wl_output* output_ = nullptr;

    static void on_configure(void* data, zwlr_layer_surface_v1* surf,
                             std::uint32_t serial, std::uint32_t w, std::uint32_t h);
    static void on_closed(void* data, zwlr_layer_surface_v1* surf);
    friend struct WaylandListeners;
};

class ToplevelSurface : public Surface {
public:
    explicit ToplevelSurface(Display& display);
    ~ToplevelSurface() override;

    bool create(const std::string& title, const std::string& app_id, int width, int height);

    void set_title(const std::string& title);
    void set_app_id(const std::string& app_id);

    void start_move(std::uint32_t serial);
    void minimize();
    void toggle_maximize();
    bool is_activated() const noexcept { return activated_; }
    bool is_maximized() const noexcept { return maximized_; }

private:
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* xdg_toplevel_ = nullptr;
    zxdg_toplevel_decoration_v1* decoration_ = nullptr;
    bool activated_ = true;
    bool maximized_ = false;

    static void on_xdg_surface_configure(void* data, xdg_surface* s, std::uint32_t serial);
    static void on_toplevel_configure(void* data, xdg_toplevel* tl,
                                      std::int32_t w, std::int32_t h, wl_array* states);
    static void on_toplevel_close(void* data, xdg_toplevel* tl);
    friend struct WaylandListeners;
};

class Display {
public:
    using ToplevelChangeFn = std::function<void()>;

    Display() = default;
    ~Display();

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    bool connect(const std::string& display_name = "");
    void disconnect();

    int fd() const noexcept;
    bool prepare_and_flush();
    bool dispatch_events();
    bool roundtrip();

    wl_display* handle() const noexcept { return display_; }
    wl_compositor* compositor() const noexcept { return compositor_; }
    wl_shm* shm() const noexcept { return shm_; }
    wl_seat* seat() const noexcept { return seat_; }
    xdg_wm_base* xdg_wm() const noexcept { return xdg_wm_base_; }
    zwlr_layer_shell_v1* layer_shell() const noexcept { return layer_shell_; }
    zxdg_decoration_manager_v1* decoration_manager() const noexcept { return decoration_mgr_; }

    bool has_layer_shell() const noexcept { return layer_shell_ != nullptr; }
    bool has_screencopy() const noexcept { return screencopy_mgr_ != nullptr; }
    bool has_foreign_toplevel() const noexcept { return toplevel_mgr_ != nullptr; }

    const std::vector<OutputInfo>& outputs() const noexcept { return outputs_; }
    OutputInfo primary_output() const;
    int output_scale(wl_output* output) const noexcept;

    const std::vector<ToplevelInfo>& toplevels() const noexcept { return toplevels_; }
    void set_toplevel_change_callback(ToplevelChangeFn fn) { on_toplevel_change_ = std::move(fn); }
    bool activate_toplevel(zwlr_foreign_toplevel_handle_v1* handle);
    bool close_toplevel(zwlr_foreign_toplevel_handle_v1* handle);

    bool capture_output(wl_output* output, CapturedFrame& out_frame, bool overlay_cursor = false);

    void register_surface(wl_surface* wl_s, Surface* surf);
    void unregister_surface(wl_surface* wl_s);
    std::uint32_t last_pointer_serial() const noexcept { return last_pointer_serial_; }

private:
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    xdg_wm_base* xdg_wm_base_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    zwlr_foreign_toplevel_manager_v1* toplevel_mgr_ = nullptr;
    zwlr_screencopy_manager_v1* screencopy_mgr_ = nullptr;
    zxdg_decoration_manager_v1* decoration_mgr_ = nullptr;

    xkb_context* xkb_ctx_ = nullptr;
    xkb_keymap* xkb_keymap_ = nullptr;
    xkb_state* xkb_state_ = nullptr;

    std::vector<OutputInfo> outputs_;
    std::vector<ToplevelInfo> toplevels_;
    std::vector<std::pair<wl_surface*, Surface*>> surfaces_;

    Surface* pointer_focus_ = nullptr;
    Surface* keyboard_focus_ = nullptr;
    double pointer_x_ = 0.0;
    double pointer_y_ = 0.0;
    std::uint32_t last_pointer_serial_ = 0;
    std::uint32_t key_mods_ = 0;
    ToplevelChangeFn on_toplevel_change_;

    Surface* find_surface(wl_surface* wl_s) const;
    void bind_seat_devices(std::uint32_t caps);
    void notify_toplevel_changed();

    static void on_global(void* data, wl_registry* reg, std::uint32_t name,
                          const char* iface, std::uint32_t version);
    static void on_global_remove(void* data, wl_registry* reg, std::uint32_t name);
    static void on_xdg_ping(void* data, xdg_wm_base* wm, std::uint32_t serial);

    static void on_output_geometry(void* data, wl_output* out, std::int32_t x, std::int32_t y,
                                   std::int32_t pw, std::int32_t ph, std::int32_t subpixel,
                                   const char* make, const char* model, std::int32_t transform);
    static void on_output_mode(void* data, wl_output* out, std::uint32_t flags,
                               std::int32_t w, std::int32_t h, std::int32_t refresh);
    static void on_output_done(void* data, wl_output* out);
    static void on_output_scale(void* data, wl_output* out, std::int32_t factor);
    static void on_output_name(void* data, wl_output* out, const char* name);
    static void on_output_description(void* data, wl_output* out, const char* desc);

    static void on_seat_capabilities(void* data, wl_seat* seat, std::uint32_t caps);
    static void on_seat_name(void* data, wl_seat* seat, const char* name);

    static void on_pointer_enter(void* data, wl_pointer* ptr, std::uint32_t serial,
                                 wl_surface* surf, wl_fixed_t sx, wl_fixed_t sy);
    static void on_pointer_leave(void* data, wl_pointer* ptr, std::uint32_t serial, wl_surface* surf);
    static void on_pointer_motion(void* data, wl_pointer* ptr, std::uint32_t time,
                                  wl_fixed_t sx, wl_fixed_t sy);
    static void on_pointer_button(void* data, wl_pointer* ptr, std::uint32_t serial,
                                  std::uint32_t time, std::uint32_t button, std::uint32_t state);
    static void on_pointer_axis(void* data, wl_pointer* ptr, std::uint32_t time,
                                std::uint32_t axis, wl_fixed_t value);
    static void on_pointer_frame(void* data, wl_pointer* ptr);
    static void on_pointer_axis_source(void* data, wl_pointer* ptr, std::uint32_t src);
    static void on_pointer_axis_stop(void* data, wl_pointer* ptr, std::uint32_t time, std::uint32_t axis);
    static void on_pointer_axis_discrete(void* data, wl_pointer* ptr, std::uint32_t axis, std::int32_t discrete);

    static void on_keyboard_keymap(void* data, wl_keyboard* kb, std::uint32_t format,
                                   std::int32_t fd, std::uint32_t size);
    static void on_keyboard_enter(void* data, wl_keyboard* kb, std::uint32_t serial,
                                  wl_surface* surf, wl_array* keys);
    static void on_keyboard_leave(void* data, wl_keyboard* kb, std::uint32_t serial, wl_surface* surf);
    static void on_keyboard_key(void* data, wl_keyboard* kb, std::uint32_t serial,
                                std::uint32_t time, std::uint32_t key, std::uint32_t state);
    static void on_keyboard_modifiers(void* data, wl_keyboard* kb, std::uint32_t serial,
                                      std::uint32_t depressed, std::uint32_t latched,
                                      std::uint32_t locked, std::uint32_t group);
    static void on_keyboard_repeat_info(void* data, wl_keyboard* kb, std::int32_t rate, std::int32_t delay);

    static void on_toplevel_created(void* data, zwlr_foreign_toplevel_manager_v1* mgr,
                                    zwlr_foreign_toplevel_handle_v1* handle);
    static void on_toplevel_finished(void* data, zwlr_foreign_toplevel_manager_v1* mgr);
    static void on_tl_title(void* data, zwlr_foreign_toplevel_handle_v1* h, const char* title);
    static void on_tl_app_id(void* data, zwlr_foreign_toplevel_handle_v1* h, const char* app_id);
    static void on_tl_output_enter(void* data, zwlr_foreign_toplevel_handle_v1* h, wl_output* out);
    static void on_tl_output_leave(void* data, zwlr_foreign_toplevel_handle_v1* h, wl_output* out);
    static void on_tl_state(void* data, zwlr_foreign_toplevel_handle_v1* h, wl_array* state);
    static void on_tl_done(void* data, zwlr_foreign_toplevel_handle_v1* h);
    static void on_tl_closed(void* data, zwlr_foreign_toplevel_handle_v1* h);
    static void on_tl_parent(void* data, zwlr_foreign_toplevel_handle_v1* h,
                             zwlr_foreign_toplevel_handle_v1* parent);

    static const wl_registry_listener registry_listener_;
    static const wl_output_listener output_listener_;
    static const wl_seat_listener seat_listener_;
    static const wl_pointer_listener pointer_listener_;
    static const wl_keyboard_listener keyboard_listener_;
    friend struct WaylandListeners;
};

} // namespace mistbar::wl
