/* Mistbar - MIT License */

#include "common/wayland_client.h"
#include "common/logger.h"

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#define namespace wl_namespace
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace mistbar::wl {

namespace {

int create_shm_fd(std::size_t size) {
    int fd = ::memfd_create("mistbar-shm", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (::ftruncate(fd, static_cast<off_t>(size)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

struct ScreencopyState {
    wl_shm* shm = nullptr;
    std::uint32_t format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::uint32_t flags = 0;
    bool buffer_ready = false;
    bool done = false;
    bool failed = false;
    wl_buffer* buffer = nullptr;
    void* mapped = nullptr;
    std::size_t size = 0;
};

void sc_buffer(void* data, struct zwlr_screencopy_frame_v1* frame,
               std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t stride) {
    auto* st = static_cast<ScreencopyState*>(data);
    if (st->buffer_ready || st->failed) return;
    st->format = format;
    st->width = width;
    st->height = height;
    st->stride = stride;
    st->size = static_cast<std::size_t>(stride) * height;

    int fd = create_shm_fd(st->size);
    if (fd < 0) {
        st->failed = true;
        st->done = true;
        return;
    }
    st->mapped = ::mmap(nullptr, st->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (st->mapped == MAP_FAILED) {
        st->mapped = nullptr;
        ::close(fd);
        st->failed = true;
        st->done = true;
        return;
    }
    wl_shm_pool* pool = wl_shm_create_pool(st->shm, fd, static_cast<std::int32_t>(st->size));
    st->buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<std::int32_t>(width),
                                           static_cast<std::int32_t>(height),
                                           static_cast<std::int32_t>(stride), format);
    wl_shm_pool_destroy(pool);
    ::close(fd);

    zwlr_screencopy_frame_v1_copy(frame, st->buffer);
    st->buffer_ready = true;
}

void sc_flags(void* data, struct zwlr_screencopy_frame_v1*, std::uint32_t flags) {
    static_cast<ScreencopyState*>(data)->flags = flags;
}

void sc_ready(void* data, struct zwlr_screencopy_frame_v1*, std::uint32_t, std::uint32_t, std::uint32_t) {
    static_cast<ScreencopyState*>(data)->done = true;
}

void sc_failed(void* data, struct zwlr_screencopy_frame_v1*) {
    auto* st = static_cast<ScreencopyState*>(data);
    st->failed = true;
    st->done = true;
}

void sc_damage(void*, struct zwlr_screencopy_frame_v1*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
void sc_linux_dmabuf(void*, struct zwlr_screencopy_frame_v1*, std::uint32_t, std::uint32_t, std::uint32_t) {}
void sc_buffer_done(void*, struct zwlr_screencopy_frame_v1*) {}

const zwlr_screencopy_frame_v1_listener kScreencopyListener = {
    sc_buffer, sc_flags, sc_ready, sc_failed, sc_damage, sc_linux_dmabuf, sc_buffer_done
};

} // namespace

struct WaylandListeners {
    static const zwlr_layer_surface_v1_listener layer_surface;
    static const xdg_surface_listener xdg_surface;
    static const xdg_toplevel_listener xdg_toplevel;
    static const xdg_wm_base_listener xdg_wm_base;
    static const zwlr_foreign_toplevel_manager_v1_listener toplevel_mgr;
    static const zwlr_foreign_toplevel_handle_v1_listener toplevel_handle;
};

const zwlr_layer_surface_v1_listener WaylandListeners::layer_surface = {
    LayerSurface::on_configure,
    LayerSurface::on_closed
};

const xdg_surface_listener WaylandListeners::xdg_surface = {
    ToplevelSurface::on_xdg_surface_configure
};

const xdg_toplevel_listener WaylandListeners::xdg_toplevel = {
    ToplevelSurface::on_toplevel_configure,
    ToplevelSurface::on_toplevel_close,
    nullptr,
    nullptr
};

const xdg_wm_base_listener WaylandListeners::xdg_wm_base = {
    Display::on_xdg_ping
};

const zwlr_foreign_toplevel_manager_v1_listener WaylandListeners::toplevel_mgr = {
    Display::on_toplevel_created,
    Display::on_toplevel_finished
};

const zwlr_foreign_toplevel_handle_v1_listener WaylandListeners::toplevel_handle = {
    Display::on_tl_title,
    Display::on_tl_app_id,
    Display::on_tl_output_enter,
    Display::on_tl_output_leave,
    Display::on_tl_state,
    Display::on_tl_done,
    Display::on_tl_closed,
    Display::on_tl_parent
};

const wl_buffer_listener ShmBufferPool::buffer_listener_ = {
    ShmBufferPool::on_buffer_release
};

void ShmBufferPool::on_buffer_release(void* data, wl_buffer*) {
    static_cast<Slot*>(data)->busy = false;
}

ShmBufferPool::~ShmBufferPool() {
    destroy();
}

void ShmBufferPool::destroy() {
    for (auto& slot : slots_) {
        if (slot.cr) {
            cairo_destroy(slot.cr);
            slot.cr = nullptr;
        }
        if (slot.cairo_surface) {
            cairo_surface_destroy(slot.cairo_surface);
            slot.cairo_surface = nullptr;
        }
        if (slot.buffer) {
            wl_buffer_destroy(slot.buffer);
            slot.buffer = nullptr;
        }
        if (slot.data && slot.size > 0) {
            ::munmap(slot.data, slot.size);
            slot.data = nullptr;
        }
        slot.size = 0;
        slot.width = 0;
        slot.height = 0;
        slot.busy = false;
    }
    active_slot_ = -1;
}

cairo_t* ShmBufferPool::begin_frame(wl_shm* shm, int width, int height) {
    if (!shm || width <= 0 || height <= 0) {
        return nullptr;
    }

    int chosen = -1;
    for (int i = 0; i < 3; ++i) {
        if (!slots_[i].busy) {
            chosen = i;
            break;
        }
    }
    if (chosen < 0) {
        chosen = 0;
    }

    Slot& slot = slots_[chosen];
    if (slot.width != width || slot.height != height || !slot.buffer) {
        if (slot.cr) { cairo_destroy(slot.cr); slot.cr = nullptr; }
        if (slot.cairo_surface) { cairo_surface_destroy(slot.cairo_surface); slot.cairo_surface = nullptr; }
        if (slot.buffer) { wl_buffer_destroy(slot.buffer); slot.buffer = nullptr; }
        if (slot.data && slot.size > 0) { ::munmap(slot.data, slot.size); slot.data = nullptr; }

        const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
        const std::size_t size = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
        int fd = create_shm_fd(size);
        if (fd < 0) return nullptr;

        void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            ::close(fd);
            return nullptr;
        }

        wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<std::int32_t>(size));
        wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        ::close(fd);

        wl_buffer_add_listener(buf, &buffer_listener_, &slot);

        slot.buffer = buf;
        slot.data = ptr;
        slot.size = size;
        slot.width = width;
        slot.height = height;
        slot.stride = stride;
        slot.cairo_surface = cairo_image_surface_create_for_data(
            static_cast<unsigned char*>(ptr), CAIRO_FORMAT_ARGB32, width, height, stride);
        slot.cr = cairo_create(slot.cairo_surface);
    }

    active_slot_ = chosen;
    return slot.cr;
}

void ShmBufferPool::end_frame(wl_surface* surface, int buffer_scale) {
    if (active_slot_ < 0 || !surface) {
        return;
    }
    Slot& slot = slots_[active_slot_];
    cairo_surface_flush(slot.cairo_surface);
    wl_surface_set_buffer_scale(surface, buffer_scale);
    wl_surface_attach(surface, slot.buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, slot.width, slot.height);
    wl_surface_commit(surface);
    slot.busy = true;
    active_slot_ = -1;
}

const wl_callback_listener Surface::frame_listener_ = {
    Surface::on_frame_done
};

const wl_surface_listener Surface::surface_listener_ = {
    Surface::on_surface_enter,
    Surface::on_surface_leave,
    nullptr,
    nullptr
};

Surface::Surface(Display& display) : display_(display) {
    if (display_.compositor()) {
        wl_surface_ = wl_compositor_create_surface(display_.compositor());
        if (wl_surface_) {
            wl_surface_add_listener(wl_surface_, &surface_listener_, this);
            display_.register_surface(wl_surface_, this);
        }
    }
    scale_ = display_.primary_output().scale;
}

Surface::~Surface() {
    cancel_frame_callback();
    shm_pool_.destroy();
    if (wl_surface_) {
        display_.unregister_surface(wl_surface_);
        wl_surface_destroy(wl_surface_);
        wl_surface_ = nullptr;
    }
}

void Surface::cancel_frame_callback() {
    if (frame_cb_) {
        wl_callback_destroy(frame_cb_);
        frame_cb_ = nullptr;
    }
    frame_pending_ = false;
}

void Surface::request_redraw() {
    if (!visible_ || !configured_ || awaiting_configure_) {
        redraw_pending_ = true;
        return;
    }
    if (!frame_pending_) {
        render_now();
    } else {
        redraw_pending_ = true;
    }
}

void Surface::render_now() {
    if (!wl_surface_ || !configured_ || awaiting_configure_ || !visible_ || width_ <= 0 || height_ <= 0) {
        redraw_pending_ = true;
        return;
    }
    if (on_egl_draw_) {
        wl_surface_set_buffer_scale(wl_surface_, scale_);
        if (on_egl_draw_(wl_surface_, width_, height_, scale_)) {
            redraw_pending_ = false;
            return;
        }
        // Fallback to SHM below if EGL callback declined
    }
    const int px_w = width_ * scale_;
    const int px_h = height_ * scale_;
    cairo_t* cr = shm_pool_.begin_frame(display_.shm(), px_w, px_h);
    if (!cr) {
        return;
    }
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (scale_ > 1) {
        cairo_scale(cr, static_cast<double>(scale_), static_cast<double>(scale_));
    }
    if (on_draw_) {
        on_draw_(cr, width_, height_);
    }
    cairo_restore(cr);

    schedule_frame_callback();
    shm_pool_.end_frame(wl_surface_, scale_);
    redraw_pending_ = false;
}

void Surface::schedule_frame_callback() {
    if (!wl_surface_ || frame_pending_) return;
    wl_callback* cb = wl_surface_frame(wl_surface_);
    if (cb) {
        frame_cb_ = cb;
        frame_pending_ = true;
        wl_callback_add_listener(cb, &frame_listener_, this);
    }
}

void Surface::on_frame_done(void* data, wl_callback* cb, std::uint32_t) {
    auto* self = static_cast<Surface*>(data);
    if (self->frame_cb_ == cb) {
        self->frame_cb_ = nullptr;
    }
    wl_callback_destroy(cb);
    self->frame_pending_ = false;
    if (self->redraw_pending_ && self->visible_ && self->configured_ && !self->awaiting_configure_) {
        self->render_now();
    }
}

void Surface::on_surface_enter(void* data, wl_surface*, wl_output* output) {
    auto* self = static_cast<Surface*>(data);
    const int new_scale = self->display_.output_scale(output);
    if (new_scale != self->scale_) {
        self->scale_ = new_scale;
        if (self->on_resize_ && self->configured_ && self->width_ > 0 && self->height_ > 0) {
            self->on_resize_(self->wl_surface_, self->width_, self->height_, self->scale_);
        }
        self->request_redraw();
    }
}

void Surface::on_surface_leave(void*, wl_surface*, wl_output*) {}

void Surface::dispatch_pointer(const PointerEvent& ev) {
    if (on_pointer_) on_pointer_(ev);
}

void Surface::dispatch_key(const KeyEvent& ev) {
    if (on_key_) on_key_(ev);
}

void Surface::set_input_region(int x, int y, int w, int h) {
    if (!wl_surface_ || !display_.compositor()) return;
    wl_region* reg = wl_compositor_create_region(display_.compositor());
    if (!reg) return;
    if (w > 0 && h > 0) {
        wl_region_add(reg, x, y, w, h);
    }
    wl_surface_set_input_region(wl_surface_, reg);
    wl_region_destroy(reg);
}

void Surface::clear_input_region() {
    if (!wl_surface_) return;
    wl_surface_set_input_region(wl_surface_, nullptr);
}

LayerSurface::LayerSurface(Display& display) : Surface(display) {}

LayerSurface::~LayerSurface() {
    if (layer_surface_) {
        zwlr_layer_surface_v1_destroy(layer_surface_);
        layer_surface_ = nullptr;
    }
}

bool LayerSurface::create(const std::string& ns,
                          Layer layer,
                          std::uint32_t anchors,
                          int width,
                          int height,
                          int exclusive_zone,
                          KeyboardMode kb_mode,
                          wl_output* output,
                          int margin_top,
                          int margin_right,
                          int margin_bottom,
                          int margin_left) {
    if (!display_.layer_shell() || !wl_surface_) {
        log::error("wayland", "Cannot create layer surface: wlr-layer-shell unavailable");
        return false;
    }
    if (layer_surface_) {
        zwlr_layer_surface_v1_destroy(layer_surface_);
        layer_surface_ = nullptr;
    }
    namespace_ = ns;
    layer_ = layer;
    anchors_ = anchors;
    req_width_ = width;
    req_height_ = height;
    exclusive_zone_ = exclusive_zone;
    kb_mode_ = kb_mode;
    output_ = output;
    configured_ = false;
    awaiting_configure_ = true;

    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        display_.layer_shell(), wl_surface_, output_,
        static_cast<std::uint32_t>(layer_), namespace_.c_str());
    if (!layer_surface_) {
        return false;
    }

    zwlr_layer_surface_v1_add_listener(layer_surface_, &WaylandListeners::layer_surface, this);
    zwlr_layer_surface_v1_set_size(layer_surface_, static_cast<std::uint32_t>(req_width_),
                                   static_cast<std::uint32_t>(req_height_));
    zwlr_layer_surface_v1_set_anchor(layer_surface_, anchors_);
    zwlr_layer_surface_v1_set_margin(layer_surface_, margin_top, margin_right, margin_bottom, margin_left);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, exclusive_zone_);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_, static_cast<std::uint32_t>(kb_mode_));

    wl_surface_commit(wl_surface_);
    return true;
}

void LayerSurface::set_geometry(int width, int height, int top, int right, int bottom, int left, int exclusive_zone) {
    const bool size_changed = (width != req_width_ || height != req_height_);
    req_width_ = width;
    req_height_ = height;
    exclusive_zone_ = exclusive_zone;
    if (!layer_surface_) return;
    if (size_changed) {
        awaiting_configure_ = true;
    }
    zwlr_layer_surface_v1_set_size(layer_surface_, static_cast<std::uint32_t>(width),
                                   static_cast<std::uint32_t>(height));
    zwlr_layer_surface_v1_set_margin(layer_surface_, top, right, bottom, left);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, exclusive_zone);
    wl_surface_commit(wl_surface_);
}

void LayerSurface::set_margin(int top, int right, int bottom, int left) {
    if (!layer_surface_) return;
    zwlr_layer_surface_v1_set_margin(layer_surface_, top, right, bottom, left);
    wl_surface_commit(wl_surface_);
}

void LayerSurface::set_exclusive_zone(int zone) {
    exclusive_zone_ = zone;
    if (!layer_surface_) return;
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, zone);
    wl_surface_commit(wl_surface_);
}

void LayerSurface::set_size(int width, int height) {
    const bool size_changed = (width != req_width_ || height != req_height_);
    req_width_ = width;
    req_height_ = height;
    if (!layer_surface_) return;
    if (size_changed) {
        awaiting_configure_ = true;
    }
    zwlr_layer_surface_v1_set_size(layer_surface_, static_cast<std::uint32_t>(width),
                                   static_cast<std::uint32_t>(height));
    wl_surface_commit(wl_surface_);
}

void LayerSurface::set_keyboard_mode(KeyboardMode kb_mode) {
    kb_mode_ = kb_mode;
    if (!layer_surface_) return;
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_, static_cast<std::uint32_t>(kb_mode));
    wl_surface_commit(wl_surface_);
}

void LayerSurface::hide() {
    visible_ = false;
    configured_ = false;
    awaiting_configure_ = false;
    redraw_pending_ = false;
    cancel_frame_callback();
    if (wl_surface_) {
        wl_surface_attach(wl_surface_, nullptr, 0, 0);
        wl_surface_commit(wl_surface_);
    }
}

void LayerSurface::show() {
    visible_ = true;
    if (configured_ && !awaiting_configure_) {
        render_now();
    } else {
        redraw_pending_ = true;
        if (wl_surface_ && layer_surface_ && !awaiting_configure_) {
            awaiting_configure_ = true;
            wl_surface_commit(wl_surface_);
        }
    }
}

void LayerSurface::on_configure(void* data, zwlr_layer_surface_v1* surf,
                                std::uint32_t serial, std::uint32_t w, std::uint32_t h) {
    auto* self = static_cast<LayerSurface*>(data);
    zwlr_layer_surface_v1_ack_configure(surf, serial);
    self->width_ = (w > 0) ? static_cast<int>(w) : self->req_width_;
    self->height_ = (h > 0) ? static_cast<int>(h) : self->req_height_;
    self->configured_ = true;
    self->awaiting_configure_ = false;
    if (self->on_resize_ && self->width_ > 0 && self->height_ > 0) {
        self->on_resize_(self->wl_surface_, self->width_, self->height_, self->scale_);
    }
    if (self->visible_) {
        self->render_now();
    }
}

void LayerSurface::on_closed(void* data, zwlr_layer_surface_v1*) {
    auto* self = static_cast<LayerSurface*>(data);
    self->visible_ = false;
    self->configured_ = false;
    self->awaiting_configure_ = false;
    self->cancel_frame_callback();
    if (self->layer_surface_) {
        zwlr_layer_surface_v1_destroy(self->layer_surface_);
        self->layer_surface_ = nullptr;
    }
    if (self->on_close_) {
        self->on_close_();
    }
}

ToplevelSurface::ToplevelSurface(Display& display) : Surface(display) {}

ToplevelSurface::~ToplevelSurface() {
    if (decoration_) {
        zxdg_toplevel_decoration_v1_destroy(decoration_);
        decoration_ = nullptr;
    }
    if (xdg_toplevel_) {
        xdg_toplevel_destroy(xdg_toplevel_);
        xdg_toplevel_ = nullptr;
    }
    if (xdg_surface_) {
        xdg_surface_destroy(xdg_surface_);
        xdg_surface_ = nullptr;
    }
}

bool ToplevelSurface::create(const std::string& title, const std::string& app_id, int width, int height) {
    if (!display_.xdg_wm() || !wl_surface_) {
        log::error("wayland", "Cannot create toplevel: xdg_wm_base unavailable");
        return false;
    }
    width_ = width;
    height_ = height;
    xdg_surface_ = xdg_wm_base_get_xdg_surface(display_.xdg_wm(), wl_surface_);
    if (!xdg_surface_) return false;
    xdg_surface_add_listener(xdg_surface_, &WaylandListeners::xdg_surface, this);

    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    if (!xdg_toplevel_) return false;
    xdg_toplevel_add_listener(xdg_toplevel_, &WaylandListeners::xdg_toplevel, this);

    xdg_toplevel_set_title(xdg_toplevel_, title.c_str());
    xdg_toplevel_set_app_id(xdg_toplevel_, app_id.c_str());

    if (display_.decoration_manager()) {
        decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(
            display_.decoration_manager(), xdg_toplevel_);
        if (decoration_) {
            zxdg_toplevel_decoration_v1_set_mode(
                decoration_, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        }
    }

    wl_surface_commit(wl_surface_);
    return true;
}

void ToplevelSurface::set_title(const std::string& title) {
    if (xdg_toplevel_) xdg_toplevel_set_title(xdg_toplevel_, title.c_str());
}

void ToplevelSurface::set_app_id(const std::string& app_id) {
    if (xdg_toplevel_) xdg_toplevel_set_app_id(xdg_toplevel_, app_id.c_str());
}

void ToplevelSurface::start_move(std::uint32_t serial) {
    if (xdg_toplevel_ && display_.seat()) {
        xdg_toplevel_move(xdg_toplevel_, display_.seat(), serial);
    }
}

void ToplevelSurface::minimize() {
    if (xdg_toplevel_) xdg_toplevel_set_minimized(xdg_toplevel_);
}

void ToplevelSurface::toggle_maximize() {
    if (!xdg_toplevel_) return;
    if (maximized_) {
        xdg_toplevel_unset_maximized(xdg_toplevel_);
    } else {
        xdg_toplevel_set_maximized(xdg_toplevel_);
    }
}

void ToplevelSurface::on_xdg_surface_configure(void* data, xdg_surface* s, std::uint32_t serial) {
    auto* self = static_cast<ToplevelSurface*>(data);
    xdg_surface_ack_configure(s, serial);
    self->configured_ = true;
    self->render_now();
}

void ToplevelSurface::on_toplevel_configure(void* data, xdg_toplevel*,
                                            std::int32_t w, std::int32_t h, wl_array* states) {
    auto* self = static_cast<ToplevelSurface*>(data);
    if (w > 0) self->width_ = w;
    if (h > 0) self->height_ = h;

    bool act = false;
    bool max = false;
    if (states) {
        const auto* begin = static_cast<const std::uint32_t*>(states->data);
        const auto count = states->size / sizeof(std::uint32_t);
        for (std::size_t i = 0; i < count; ++i) {
            if (begin[i] == XDG_TOPLEVEL_STATE_ACTIVATED) act = true;
            if (begin[i] == XDG_TOPLEVEL_STATE_MAXIMIZED) max = true;
        }
    }
    self->activated_ = act;
    self->maximized_ = max;
}

void ToplevelSurface::on_toplevel_close(void* data, xdg_toplevel*) {
    auto* self = static_cast<ToplevelSurface*>(data);
    if (self->on_close_) self->on_close_();
}

const wl_registry_listener Display::registry_listener_ = {
    Display::on_global,
    Display::on_global_remove
};

const wl_output_listener Display::output_listener_ = {
    Display::on_output_geometry,
    Display::on_output_mode,
    Display::on_output_done,
    Display::on_output_scale,
    Display::on_output_name,
    Display::on_output_description
};

const wl_seat_listener Display::seat_listener_ = {
    Display::on_seat_capabilities,
    Display::on_seat_name
};

const wl_pointer_listener Display::pointer_listener_ = {
    Display::on_pointer_enter,
    Display::on_pointer_leave,
    Display::on_pointer_motion,
    Display::on_pointer_button,
    Display::on_pointer_axis,
    Display::on_pointer_frame,
    Display::on_pointer_axis_source,
    Display::on_pointer_axis_stop,
    Display::on_pointer_axis_discrete,
    nullptr,
    nullptr,
    nullptr
};

const wl_keyboard_listener Display::keyboard_listener_ = {
    Display::on_keyboard_keymap,
    Display::on_keyboard_enter,
    Display::on_keyboard_leave,
    Display::on_keyboard_key,
    Display::on_keyboard_modifiers,
    Display::on_keyboard_repeat_info
};

Display::~Display() {
    disconnect();
}

bool Display::connect(const std::string& display_name) {
    disconnect();
    display_ = wl_display_connect(display_name.empty() ? nullptr : display_name.c_str());
    if (!display_) {
        log::error("wayland", "Failed to connect to Wayland display");
        return false;
    }

    xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &registry_listener_, this);

    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);

    if (!compositor_ || !shm_) {
        log::error("wayland", "Missing required wl_compositor or wl_shm");
        disconnect();
        return false;
    }
    return true;
}

void Display::disconnect() {
    for (auto& tl : toplevels_) {
        if (tl.handle) zwlr_foreign_toplevel_handle_v1_destroy(tl.handle);
    }
    toplevels_.clear();

    for (auto& out : outputs_) {
        if (out.output) wl_output_destroy(out.output);
    }
    outputs_.clear();

    if (pointer_) { wl_pointer_destroy(pointer_); pointer_ = nullptr; }
    if (keyboard_) { wl_keyboard_destroy(keyboard_); keyboard_ = nullptr; }
    if (seat_) { wl_seat_destroy(seat_); seat_ = nullptr; }
    if (decoration_mgr_) { zxdg_decoration_manager_v1_destroy(decoration_mgr_); decoration_mgr_ = nullptr; }
    if (screencopy_mgr_) { zwlr_screencopy_manager_v1_destroy(screencopy_mgr_); screencopy_mgr_ = nullptr; }
    if (toplevel_mgr_) { zwlr_foreign_toplevel_manager_v1_destroy(toplevel_mgr_); toplevel_mgr_ = nullptr; }
    if (layer_shell_) { zwlr_layer_shell_v1_destroy(layer_shell_); layer_shell_ = nullptr; }
    if (xdg_wm_base_) { xdg_wm_base_destroy(xdg_wm_base_); xdg_wm_base_ = nullptr; }
    if (shm_) { wl_shm_destroy(shm_); shm_ = nullptr; }
    if (compositor_) { wl_compositor_destroy(compositor_); compositor_ = nullptr; }
    if (registry_) { wl_registry_destroy(registry_); registry_ = nullptr; }

    if (xkb_state_) { xkb_state_unref(xkb_state_); xkb_state_ = nullptr; }
    if (xkb_keymap_) { xkb_keymap_unref(xkb_keymap_); xkb_keymap_ = nullptr; }
    if (xkb_ctx_) { xkb_context_unref(xkb_ctx_); xkb_ctx_ = nullptr; }

    if (display_) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
}

int Display::fd() const noexcept {
    return display_ ? wl_display_get_fd(display_) : -1;
}

bool Display::prepare_and_flush() {
    if (!display_) return false;
    while (wl_display_prepare_read(display_) != 0) {
        if (wl_display_dispatch_pending(display_) < 0) return false;
    }
    if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(display_);
        return false;
    }
    return true;
}

bool Display::dispatch_events() {
    if (!display_) return false;
    if (wl_display_read_events(display_) < 0) return false;
    return wl_display_dispatch_pending(display_) >= 0;
}

bool Display::roundtrip() {
    return display_ && wl_display_roundtrip(display_) >= 0;
}

OutputInfo Display::primary_output() const {
    if (!outputs_.empty()) return outputs_.front();
    return OutputInfo{};
}

int Display::output_scale(wl_output* output) const noexcept {
    for (const auto& o : outputs_) {
        if (o.output == output) return std::max(1, o.scale);
    }
    return outputs_.empty() ? 1 : std::max(1, outputs_.front().scale);
}

bool Display::activate_toplevel(zwlr_foreign_toplevel_handle_v1* handle) {
    if (!handle || !seat_) return false;
    zwlr_foreign_toplevel_handle_v1_activate(handle, seat_);
    wl_display_flush(display_);
    return true;
}

bool Display::close_toplevel(zwlr_foreign_toplevel_handle_v1* handle) {
    if (!handle) return false;
    zwlr_foreign_toplevel_handle_v1_close(handle);
    wl_display_flush(display_);
    return true;
}

bool Display::capture_output(wl_output* output, CapturedFrame& out_frame, bool overlay_cursor) {
    if (!screencopy_mgr_ || !shm_) return false;
    wl_output* target = output;
    if (!target && !outputs_.empty()) {
        target = outputs_.front().output;
    }
    if (!target) return false;

    ScreencopyState st;
    st.shm = shm_;
    zwlr_screencopy_frame_v1* frame = zwlr_screencopy_manager_v1_capture_output(
        screencopy_mgr_, overlay_cursor ? 1 : 0, target);
    if (!frame) return false;

    zwlr_screencopy_frame_v1_add_listener(frame, &kScreencopyListener, &st);
    while (!st.done) {
        if (wl_display_dispatch(display_) < 0) {
            st.failed = true;
            break;
        }
    }

    const bool ok = !st.failed && st.mapped != nullptr && st.width > 0 && st.height > 0;
    if (ok) {
        out_frame.width = static_cast<int>(st.width);
        out_frame.height = static_cast<int>(st.height);
        out_frame.stride = static_cast<int>(st.stride);
        out_frame.y_inverted = (st.flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
        out_frame.pixels.resize(st.size);

        const auto* src = static_cast<const std::uint8_t*>(st.mapped);
        if (!out_frame.y_inverted) {
            std::memcpy(out_frame.pixels.data(), src, st.size);
        } else {
            for (std::uint32_t y = 0; y < st.height; ++y) {
                const auto* srow = src + static_cast<std::size_t>(st.height - 1 - y) * st.stride;
                auto* drow = out_frame.pixels.data() + static_cast<std::size_t>(y) * st.stride;
                std::memcpy(drow, srow, st.stride);
            }
            out_frame.y_inverted = false;
        }

        if (st.format == WL_SHM_FORMAT_XRGB8888) {
            for (std::size_t i = 3; i < out_frame.pixels.size(); i += 4) {
                out_frame.pixels[i] = 0xFF;
            }
        } else if (st.format == WL_SHM_FORMAT_XBGR8888 || st.format == WL_SHM_FORMAT_ABGR8888) {
            for (std::size_t i = 0; i + 3 < out_frame.pixels.size(); i += 4) {
                std::swap(out_frame.pixels[i], out_frame.pixels[i + 2]);
                if (st.format == WL_SHM_FORMAT_XBGR8888) out_frame.pixels[i + 3] = 0xFF;
            }
        }
    }

    if (st.buffer) wl_buffer_destroy(st.buffer);
    if (st.mapped && st.size > 0) ::munmap(st.mapped, st.size);
    zwlr_screencopy_frame_v1_destroy(frame);
    return ok;
}

void Display::register_surface(wl_surface* wl_s, Surface* surf) {
    surfaces_.emplace_back(wl_s, surf);
}

void Display::unregister_surface(wl_surface* wl_s) {
    if (pointer_focus_ && pointer_focus_->wl_handle() == wl_s) pointer_focus_ = nullptr;
    if (keyboard_focus_ && keyboard_focus_->wl_handle() == wl_s) keyboard_focus_ = nullptr;
    surfaces_.erase(
        std::remove_if(surfaces_.begin(), surfaces_.end(),
                       [wl_s](const auto& pair) { return pair.first == wl_s; }),
        surfaces_.end());
}

Surface* Display::find_surface(wl_surface* wl_s) const {
    for (const auto& [handle, ptr] : surfaces_) {
        if (handle == wl_s) return ptr;
    }
    return nullptr;
}

void Display::bind_seat_devices(std::uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer_) {
        pointer_ = wl_seat_get_pointer(seat_);
        wl_pointer_add_listener(pointer_, &pointer_listener_, this);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer_) {
        wl_pointer_destroy(pointer_);
        pointer_ = nullptr;
    }

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard_) {
        keyboard_ = wl_seat_get_keyboard(seat_);
        wl_keyboard_add_listener(keyboard_, &keyboard_listener_, this);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard_) {
        wl_keyboard_destroy(keyboard_);
        keyboard_ = nullptr;
    }
}

void Display::notify_toplevel_changed() {
    if (on_toplevel_change_) on_toplevel_change_();
}

void Display::on_global(void* data, wl_registry* reg, std::uint32_t name,
                        const char* iface, std::uint32_t version) {
    auto* self = static_cast<Display*>(data);
    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4U)));
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        self->shm_ = static_cast<wl_shm*>(
            wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (std::strcmp(iface, wl_seat_interface.name) == 0) {
        self->seat_ = static_cast<wl_seat*>(
            wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 7U)));
        wl_seat_add_listener(self->seat_, &seat_listener_, self);
    } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        self->xdg_wm_base_ = static_cast<xdg_wm_base*>(
            wl_registry_bind(reg, name, &xdg_wm_base_interface, std::min(version, 2U)));
        xdg_wm_base_add_listener(self->xdg_wm_base_, &WaylandListeners::xdg_wm_base, self);
    } else if (std::strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, std::min(version, 4U)));
    } else if (std::strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        self->toplevel_mgr_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(
            wl_registry_bind(reg, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min(version, 3U)));
        zwlr_foreign_toplevel_manager_v1_add_listener(self->toplevel_mgr_, &WaylandListeners::toplevel_mgr, self);
    } else if (std::strcmp(iface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        self->screencopy_mgr_ = static_cast<zwlr_screencopy_manager_v1*>(
            wl_registry_bind(reg, name, &zwlr_screencopy_manager_v1_interface, std::min(version, 3U)));
    } else if (std::strcmp(iface, zxdg_decoration_manager_v1_interface.name) == 0) {
        self->decoration_mgr_ = static_cast<zxdg_decoration_manager_v1*>(
            wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1));
    } else if (std::strcmp(iface, wl_output_interface.name) == 0) {
        OutputInfo info;
        info.global_name = name;
        info.output = static_cast<wl_output*>(
            wl_registry_bind(reg, name, &wl_output_interface, std::min(version, 4U)));
        self->outputs_.push_back(info);
        wl_output_add_listener(info.output, &output_listener_, self);
    }
}

void Display::on_global_remove(void* data, wl_registry*, std::uint32_t name) {
    auto* self = static_cast<Display*>(data);
    self->outputs_.erase(
        std::remove_if(self->outputs_.begin(), self->outputs_.end(),
                       [name](OutputInfo& o) {
                           if (o.global_name == name) {
                               if (o.output) wl_output_destroy(o.output);
                               return true;
                           }
                           return false;
                       }),
        self->outputs_.end());
}

void Display::on_xdg_ping(void*, xdg_wm_base* wm, std::uint32_t serial) {
    xdg_wm_base_pong(wm, serial);
}

void Display::on_output_geometry(void*, wl_output*, std::int32_t, std::int32_t,
                                 std::int32_t, std::int32_t, std::int32_t,
                                 const char*, const char*, std::int32_t) {}

void Display::on_output_mode(void* data, wl_output* out, std::uint32_t flags,
                             std::int32_t w, std::int32_t h, std::int32_t) {
    if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;
    auto* self = static_cast<Display*>(data);
    for (auto& info : self->outputs_) {
        if (info.output == out) {
            info.width = w;
            info.height = h;
            break;
        }
    }
}

void Display::on_output_done(void*, wl_output*) {}

void Display::on_output_scale(void* data, wl_output* out, std::int32_t factor) {
    auto* self = static_cast<Display*>(data);
    for (auto& info : self->outputs_) {
        if (info.output == out) {
            info.scale = std::max(1, factor);
            break;
        }
    }
}

void Display::on_output_name(void* data, wl_output* out, const char* name) {
    auto* self = static_cast<Display*>(data);
    for (auto& info : self->outputs_) {
        if (info.output == out && name) {
            info.name = name;
            break;
        }
    }
}

void Display::on_output_description(void*, wl_output*, const char*) {}

void Display::on_seat_capabilities(void* data, wl_seat*, std::uint32_t caps) {
    static_cast<Display*>(data)->bind_seat_devices(caps);
}

void Display::on_seat_name(void*, wl_seat*, const char*) {}

void Display::on_pointer_enter(void* data, wl_pointer*, std::uint32_t serial,
                               wl_surface* surf, wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<Display*>(data);
    self->last_pointer_serial_ = serial;
    self->pointer_focus_ = self->find_surface(surf);
    self->pointer_x_ = wl_fixed_to_double(sx);
    self->pointer_y_ = wl_fixed_to_double(sy);
    if (self->pointer_focus_) {
        PointerEvent ev{PointerEvent::Type::Enter, self->pointer_x_, self->pointer_y_, 0, serial, 0.0, 0.0};
        self->pointer_focus_->dispatch_pointer(ev);
    }
}

void Display::on_pointer_leave(void* data, wl_pointer*, std::uint32_t serial, wl_surface* surf) {
    auto* self = static_cast<Display*>(data);
    Surface* target = self->find_surface(surf);
    if (target) {
        PointerEvent ev{PointerEvent::Type::Leave, self->pointer_x_, self->pointer_y_, 0, serial, 0.0, 0.0};
        target->dispatch_pointer(ev);
    }
    if (self->pointer_focus_ == target) self->pointer_focus_ = nullptr;
}

void Display::on_pointer_motion(void* data, wl_pointer*, std::uint32_t,
                                wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<Display*>(data);
    self->pointer_x_ = wl_fixed_to_double(sx);
    self->pointer_y_ = wl_fixed_to_double(sy);
    if (self->pointer_focus_) {
        PointerEvent ev{PointerEvent::Type::Motion, self->pointer_x_, self->pointer_y_, 0, 0, 0.0, 0.0};
        self->pointer_focus_->dispatch_pointer(ev);
    }
}

void Display::on_pointer_button(void* data, wl_pointer*, std::uint32_t serial,
                                std::uint32_t, std::uint32_t button, std::uint32_t state) {
    auto* self = static_cast<Display*>(data);
    self->last_pointer_serial_ = serial;
    if (self->pointer_focus_) {
        const auto type = (state == WL_POINTER_BUTTON_STATE_PRESSED)
                              ? PointerEvent::Type::ButtonPress
                              : PointerEvent::Type::ButtonRelease;
        PointerEvent ev{type, self->pointer_x_, self->pointer_y_, button, serial, 0.0, 0.0};
        self->pointer_focus_->dispatch_pointer(ev);
    }
}

void Display::on_pointer_axis(void* data, wl_pointer*, std::uint32_t,
                              std::uint32_t axis, wl_fixed_t value) {
    auto* self = static_cast<Display*>(data);
    if (self->pointer_focus_) {
        PointerEvent ev{PointerEvent::Type::Axis, self->pointer_x_, self->pointer_y_, 0, 0, 0.0, 0.0};
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) ev.axis_dy = wl_fixed_to_double(value);
        else ev.axis_dx = wl_fixed_to_double(value);
        self->pointer_focus_->dispatch_pointer(ev);
    }
}

void Display::on_pointer_frame(void*, wl_pointer*) {}
void Display::on_pointer_axis_source(void*, wl_pointer*, std::uint32_t) {}
void Display::on_pointer_axis_stop(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
void Display::on_pointer_axis_discrete(void*, wl_pointer*, std::uint32_t, std::int32_t) {}

void Display::on_keyboard_keymap(void* data, wl_keyboard*, std::uint32_t format,
                                 std::int32_t fd, std::uint32_t size) {
    auto* self = static_cast<Display*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        ::close(fd);
        return;
    }
    void* shm_str = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (shm_str == MAP_FAILED) {
        ::close(fd);
        return;
    }
    xkb_keymap* map = xkb_keymap_new_from_string(
        self->xkb_ctx_, static_cast<const char*>(shm_str),
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    ::munmap(shm_str, size);
    ::close(fd);
    if (!map) return;

    if (self->xkb_state_) xkb_state_unref(self->xkb_state_);
    if (self->xkb_keymap_) xkb_keymap_unref(self->xkb_keymap_);
    self->xkb_keymap_ = map;
    self->xkb_state_ = xkb_state_new(map);
}

void Display::on_keyboard_enter(void* data, wl_keyboard*, std::uint32_t,
                                wl_surface* surf, wl_array*) {
    auto* self = static_cast<Display*>(data);
    self->keyboard_focus_ = self->find_surface(surf);
}

void Display::on_keyboard_leave(void* data, wl_keyboard*, std::uint32_t, wl_surface* surf) {
    auto* self = static_cast<Display*>(data);
    if (self->keyboard_focus_ == self->find_surface(surf)) {
        self->keyboard_focus_ = nullptr;
    }
}

void Display::on_keyboard_key(void* data, wl_keyboard*, std::uint32_t,
                              std::uint32_t, std::uint32_t key, std::uint32_t state) {
    auto* self = static_cast<Display*>(data);
    if (!self->keyboard_focus_ || !self->xkb_state_) return;

    const std::uint32_t xkb_code = key + 8;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(self->xkb_state_, xkb_code);
    KeyEvent ev;
    ev.pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
    ev.keycode = key;
    ev.keysym = sym;
    ev.modifiers = self->key_mods_;

    if (ev.pressed) {
        char buf[64]{};
        const int len = xkb_state_key_get_utf8(self->xkb_state_, xkb_code, buf, sizeof(buf));
        if (len > 0) ev.utf8.assign(buf, static_cast<std::size_t>(len));
    }
    self->keyboard_focus_->dispatch_key(ev);
}

void Display::on_keyboard_modifiers(void* data, wl_keyboard*, std::uint32_t,
                                    std::uint32_t depressed, std::uint32_t latched,
                                    std::uint32_t locked, std::uint32_t group) {
    auto* self = static_cast<Display*>(data);
    if (!self->xkb_state_) return;
    xkb_state_update_mask(self->xkb_state_, depressed, latched, locked, 0, 0, group);
    std::uint32_t m = 0;
    if (xkb_state_mod_name_is_active(self->xkb_state_, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) m |= 1U;
    if (xkb_state_mod_name_is_active(self->xkb_state_, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)  m |= 2U;
    if (xkb_state_mod_name_is_active(self->xkb_state_, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)   m |= 4U;
    if (xkb_state_mod_name_is_active(self->xkb_state_, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)  m |= 8U;
    self->key_mods_ = m;
}

void Display::on_keyboard_repeat_info(void*, wl_keyboard*, std::int32_t, std::int32_t) {}

void Display::on_toplevel_created(void* data, zwlr_foreign_toplevel_manager_v1*,
                                  zwlr_foreign_toplevel_handle_v1* handle) {
    auto* self = static_cast<Display*>(data);
    ToplevelInfo tl;
    tl.handle = handle;
    self->toplevels_.push_back(tl);
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &WaylandListeners::toplevel_handle, self);
}

void Display::on_toplevel_finished(void*, zwlr_foreign_toplevel_manager_v1*) {}

void Display::on_tl_title(void* data, zwlr_foreign_toplevel_handle_v1* h, const char* title) {
    auto* self = static_cast<Display*>(data);
    for (auto& tl : self->toplevels_) {
        if (tl.handle == h) {
            tl.title = title ? title : "";
            break;
        }
    }
}

void Display::on_tl_app_id(void* data, zwlr_foreign_toplevel_handle_v1* h, const char* app_id) {
    auto* self = static_cast<Display*>(data);
    for (auto& tl : self->toplevels_) {
        if (tl.handle == h) {
            tl.app_id = app_id ? app_id : "";
            break;
        }
    }
}

void Display::on_tl_output_enter(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}
void Display::on_tl_output_leave(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}

void Display::on_tl_state(void* data, zwlr_foreign_toplevel_handle_v1* h, wl_array* state) {
    auto* self = static_cast<Display*>(data);
    for (auto& tl : self->toplevels_) {
        if (tl.handle != h) continue;
        tl.active = false;
        tl.maximized = false;
        tl.minimized = false;
        tl.fullscreen = false;
        if (state) {
            const auto* begin = static_cast<const std::uint32_t*>(state->data);
            const auto count = state->size / sizeof(std::uint32_t);
            for (std::size_t i = 0; i < count; ++i) {
                switch (begin[i]) {
                    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:  tl.active = true; break;
                    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:  tl.maximized = true; break;
                    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:  tl.minimized = true; break;
                    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN: tl.fullscreen = true; break;
                    default: break;
                }
            }
        }
        break;
    }
}

void Display::on_tl_done(void* data, zwlr_foreign_toplevel_handle_v1*) {
    static_cast<Display*>(data)->notify_toplevel_changed();
}

void Display::on_tl_closed(void* data, zwlr_foreign_toplevel_handle_v1* h) {
    auto* self = static_cast<Display*>(data);
    self->toplevels_.erase(
        std::remove_if(self->toplevels_.begin(), self->toplevels_.end(),
                       [h](const ToplevelInfo& tl) {
                           if (tl.handle == h) {
                               zwlr_foreign_toplevel_handle_v1_destroy(h);
                               return true;
                           }
                           return false;
                       }),
        self->toplevels_.end());
    self->notify_toplevel_changed();
}

void Display::on_tl_parent(void*, zwlr_foreign_toplevel_handle_v1*,
                           zwlr_foreign_toplevel_handle_v1*) {}

} // namespace mistbar::wl
