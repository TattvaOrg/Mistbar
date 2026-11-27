/* Mistbar - MIT License */

#include "bar/bar_app.h"
#include "common/ipc.h"
#include "common/logger.h"
#include "common/xdg.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/file.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t g_stop_requested = 0;

void handle_signal(int) {
    g_stop_requested = 1;
}

int acquire_single_instance_lock() {
    const std::string dir = mistbar::xdg::runtime_dir();
    mistbar::xdg::ensure_dir(dir);
    const std::string lock_path = dir + "/bar.lock";
    const int fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    if (::flock(fd, LOCK_EX | LOCK_NB) < 0) {
        const int err = errno;
        ::close(fd);
        errno = err;
        return -2;
    }
    return fd;
}
} // namespace

int main(int argc, char** argv) {
    mistbar::log::init();

    // Parse CLI flags before touching Wayland or spawning a bar surface.
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cout << "Usage: mistbar-daemon [--dump-png <path>] [--version] [--help]\n";
            return 0;
        }
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--version") == 0) {
            std::cout << "mistbar-daemon " << MISTBAR_VERSION << "\n";
            return 0;
        }
        if (std::strcmp(arg, "--dump-png") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --dump-png requires an output path.\n";
                return 1;
            }
            mistbar::wl::Display dummy_dpy;
            mistbar::bar::BarApp preview_app(dummy_dpy);
            cairo_surface_t* canvas = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1280, 500);
            cairo_t* cr = cairo_create(canvas);

            mistbar::glass::GlassBackdrop bg;
            auto cfg = mistbar::config::load_config();
            auto pal = mistbar::themes::resolve_palette(cfg);
            bg.init(cfg);
            bg.load_wallpaper(pal.wallpaper_path, 1280, 500, 0);
            if (bg.raw_wallpaper()) {
                cairo_set_source_surface(cr, bg.raw_wallpaper(), 0.0, 0.0);
                cairo_paint(cr);
            }

            cairo_save(cr);
            cairo_translate(cr, 0.0, 0.0);
            preview_app.render_bar_preview(cr, 1280, cfg.bar_height);
            cairo_restore(cr);

            cairo_save(cr);
            cairo_translate(cr, 964.0, 26.0);
            preview_app.render_control_center_preview(cr, 300, 456);
            cairo_restore(cr);

            cairo_destroy(cr);
            cairo_surface_write_to_png(canvas, argv[i + 1]);
            cairo_surface_destroy(canvas);
            return 0;
        }
        std::cerr << "Unknown argument '" << arg << "'. Use --help for usage.\n";
        return 1;
    }

    const int lock_fd = acquire_single_instance_lock();
    if (lock_fd == -2) {
        std::cerr << "mistbar-daemon is already running.\n";
        return 0;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    mistbar::wl::Display display;
    if (!display.connect()) {
        mistbar::log::error("bar", "Could not connect to Wayland display");
        if (lock_fd >= 0) ::close(lock_fd);
        return 1;
    }

    mistbar::bar::BarApp app(display);
    if (!app.init()) {
        mistbar::log::error("bar", "Could not initialize Wayland bar surface");
        if (lock_fd >= 0) ::close(lock_fd);
        return 1;
    }

    bool should_quit = false;
    mistbar::ipc::Server ipc_srv;
    ipc_srv.start(mistbar::ipc::default_socket_path(), [&](const std::string& req) {
        return app.handle_ipc(req, should_quit);
    });

    mistbar::log::info("bar", "mistbar-daemon started");

    auto last_1s = std::chrono::steady_clock::now();

    while (!should_quit && !g_stop_requested) {
        const auto now_pre = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_pre - last_1s).count() >= 1000) {
            last_1s = now_pre;
            app.refresh_clock_only();
        }

        const bool animating = app.tick();

        if (!display.prepare_and_flush()) {
            mistbar::log::warn("bar", "Wayland prepare_and_flush failed; exiting");
            break;
        }

        const int timeout_ms = animating ? 16 : 500;
        pollfd pfds[3]{
            {display.fd(), POLLIN, 0},
            {ipc_srv.fd(), POLLIN, 0},
            {app.wake_fd(), POLLIN, 0}
        };
        nfds_t nfds = 1;
        if (ipc_srv.fd() >= 0) pfds[nfds++] = {ipc_srv.fd(), POLLIN, 0};
        if (app.wake_fd() >= 0) pfds[nfds++] = {app.wake_fd(), POLLIN, 0};

        const int pr = ::poll(pfds, nfds, timeout_ms);
        if (pr > 0 && (pfds[0].revents & POLLIN)) {
            if (!display.dispatch_events()) {
                mistbar::log::warn("bar", "Wayland dispatch_events failed; exiting");
                break;
            }
        } else {
            wl_display_cancel_read(display.handle());
            if (pr > 0 && (pfds[0].revents & (POLLERR | POLLHUP))) {
                mistbar::log::warn("bar", "Wayland socket closed; exiting");
                break;
            }
        }

        for (nfds_t i = 1; i < nfds; ++i) {
            if (pr > 0 && (pfds[i].revents & POLLIN)) {
                if (pfds[i].fd == ipc_srv.fd()) {
                    ipc_srv.poll_once();
                } else if (pfds[i].fd == app.wake_fd()) {
                    app.apply_worker_updates();
                }
            }
        }
    }

    ipc_srv.stop();
    if (lock_fd >= 0) ::close(lock_fd);
    mistbar::log::info("bar", "mistbar-daemon exiting");
    return 0;
}
