#include "app.h"
#include "local_fs.h"
#include "remote_fs.h"
#include "ssh_session.h"
#include "file_ops.h"
#include "fuzzy.h"
#include <libssh/sftp.h>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

using namespace ftxui;
namespace fs = std::filesystem;

App::App() {
    PaneState ps;
    ps.pane = std::make_unique<LocalPane>(fs::current_path().string());
    panes_.push_back(std::move(ps));
}

App::App(const std::string& user, const std::string& host, const std::string& password) {
    PaneState local;
    local.pane = std::make_unique<LocalPane>(fs::current_path().string());
    panes_.push_back(std::move(local));

    auto sess = std::make_shared<SshSession>(host, user);
    if (sess->connect_password(password)) {
        PaneState remote;
        remote.pane = std::make_unique<RemotePane>(sess, "/");
        panes_.push_back(std::move(remote));
        active_pane_ = 1;
    } else {
        last_error_ = "Failed to connect to " + user + "@" + host;
    }
}

int App::run() {
    auto screen = ScreenInteractive::Fullscreen();

    auto cur_state = [&]() -> PaneState& { return panes_[active_pane_]; };
    auto cur_pane  = [&]() -> Pane&      { return *panes_[active_pane_].pane; };

    auto queue_rows = [&]() -> int {
        if (op_queue_.empty()) return 0;
        int items = std::min((int)op_queue_.size(), 6);
        if ((int)op_queue_.size() > 6) items++;
        return items + 1;
    };

    auto pane_rows = [&]() { return ((int)panes_.size() > 3) ? 2 : 1; };

    auto visible_rows = [&]() {
        return (Terminal::Size().dimy - 5 - queue_rows()) / pane_rows();
    };

    auto update_scroll = [&](int cursor, int n) {
        int vis = visible_rows();
        cur_state().scroll_offset = std::clamp(cursor - vis / 2, 0, std::max(0, n - vis));
    };

    auto path_tag = [&](const std::string& path) -> int {
        for (const auto& op : op_queue_) {
            if (op.src == path) {
                switch (op.type) {
                    case QueuedOp::Type::Copy:   return 1;
                    case QueuedOp::Type::Move:   return 2;
                    case QueuedOp::Type::Delete: return 3;
                }
            }
            if (!op.dst.empty() && fs::path(op.dst).parent_path().string() == path)
                return 6;
        }
        if (clipboard_active_ && clipboard_.path == path)
            return clipboard_.op == ClipboardOp::Copy ? 4 : 5;
        return 0;
    };

    // Saved pane view state (used by async execute restore)
    struct SavedPane {
        std::string cursor_path;
        std::vector<std::string> expanded_paths;
        int scroll_offset;
    };

    auto save_pane_states = [&]() {
        std::vector<SavedPane> saved;
        for (auto& ps2 : panes_) {
            SavedPane s;
            s.scroll_offset = ps2.scroll_offset;
            const auto& ents = ps2.pane->entries();
            if (ps2.pane->cursor < (int)ents.size())
                s.cursor_path = ents[ps2.pane->cursor].full_path;
            for (const auto& e : ents)
                if (e.expanded) s.expanded_paths.push_back(e.full_path);
            saved.push_back(std::move(s));
        }
        return saved;
    };

    auto restore_pane_states = [&](const std::vector<SavedPane>& saved) {
        for (int pi = 0; pi < (int)panes_.size() && pi < (int)saved.size(); pi++) {
            auto& ps2 = panes_[pi];
            const SavedPane& s = saved[pi];
            Pane& p2 = *ps2.pane;
            p2.navigate(p2.current_path);
            for (int ei = 0; ei < (int)p2.entries().size(); ei++) {
                const auto& e = p2.entries()[ei];
                if (e.is_dir && !e.expanded) {
                    for (const auto& ep : s.expanded_paths) {
                        if (ep == e.full_path) { p2.toggle_expand(ei); break; }
                    }
                }
            }
            const auto& ents2 = p2.entries();
            int new_cur = 0;
            bool found = false;
            for (int ei = 0; ei < (int)ents2.size(); ei++) {
                if (ents2[ei].full_path == s.cursor_path) { new_cur = ei; found = true; break; }
            }
            if (!found)
                new_cur = std::min((int)ents2.size() - 1, std::max(0, p2.cursor));
            p2.cursor = new_cur;
            int vis = visible_rows();
            ps2.scroll_offset = std::clamp(s.scroll_offset, 0, std::max(0, (int)ents2.size() - vis));
        }
    };

    // Launch a background operation with spinner overlay.
    // work() runs on a bg thread; on_done() runs on the main thread when complete.
    auto start_loading = [&](std::string msg,
                              std::function<void()> work,
                              std::function<void()> on_done) {
        loading_msg_ = std::move(msg);
        loading_.store(true);
        spinner_frame_.store(0);

        // Work thread
        std::thread([this, &screen, work = std::move(work), on_done = std::move(on_done)]() mutable {
            work();
            { std::lock_guard<std::mutex> lk(result_mutex_); on_load_complete_ = std::move(on_done); }
            loading_.store(false);
            screen.PostEvent(Event::Custom);
        }).detach();

        // Animation thread: drives spinner redraws until work is done
        std::thread([this, &screen]() {
            while (loading_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                if (loading_.load()) screen.PostEvent(Event::Custom);
            }
        }).detach();
    };

    static const std::array<Color, 6> kPaneColors = {
        Color::CyanLight, Color::GreenLight, Color::YellowLight,
        Color::MagentaLight, Color::BlueLight, Color::RedLight,
    };
    static const std::array<Color, 6> kPaneColorsDim = {
        Color::Cyan, Color::Green, Color::Yellow,
        Color::Magenta, Color::Blue, Color::Red,
    };
    static const char* kSpinner[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};

    auto ui = Renderer([&] {
        int vis = visible_rows();
        std::vector<Element> pane_elements;

        for (int pi = 0; pi < (int)panes_.size(); pi++) {
            PaneState& ps = panes_[pi];
            Pane& p = *ps.pane;
            bool is_active = (pi == active_pane_);
            Color accent    = kPaneColors[pi % kPaneColors.size()];
            Color accentDim = kPaneColorsDim[pi % kPaneColorsDim.size()];

            std::vector<Element> rows;

            if (ps.fuzzy_mode) {
                int n = (int)ps.fuzzy_results.size();
                int start = std::clamp(ps.fuzzy_cursor - vis / 2, 0, std::max(0, n - vis));
                int end = std::min(start + vis, n);
                for (int i = start; i < end; i++) {
                    auto row = text(ps.fuzzy_results[i]);
                    if (i == ps.fuzzy_cursor) row = row | inverted;
                    rows.push_back(row);
                }
            } else {
                const auto& entries = p.entries();
                int n = (int)entries.size();
                int end = std::min(ps.scroll_offset + vis, n);
                for (int i = ps.scroll_offset; i < end; i++) {
                    const auto& e = entries[i];
                    std::string indent(e.depth * 2, ' ');
                    std::string icon = e.is_dir ? (e.expanded ? "v " : "> ") : "  ";
                    auto row = text(indent + icon + e.name);
                    if (i == p.cursor) {
                        bool is_paste_target = clipboard_active_ && e.is_dir;
                        row = is_paste_target
                            ? row | inverted | color(Color::Cyan)
                            : row | inverted;
                    } else {
                        switch (path_tag(e.full_path)) {
                            case 1: row = row | color(Color::Black) | bgcolor(Color::Green);       break;
                            case 2: row = row | color(Color::Black) | bgcolor(Color::Yellow);      break;
                            case 3: row = row | color(Color::White) | bgcolor(Color::Red);         break;
                            case 4: row = row | color(Color::Black) | bgcolor(Color::GreenLight);  break;
                            case 5: row = row | color(Color::Black) | bgcolor(Color::YellowLight); break;
                            case 6: break;
                        }
                    }
                    rows.push_back(row);
                }
            }

            Element tree = rows.empty()
                ? (text("(empty)") | dim | hcenter)
                : vbox(std::move(rows));

            bool is_remote = (p.type() == PaneType::Remote);
            std::string badge = is_remote ? " SSH " : " LOC ";
            std::string path_str = is_remote ? p.title() : p.current_path;

            Element title;
            if (is_active) {
                title = hbox({
                    text(badge) | bold | color(Color::Black) | bgcolor(accent),
                    text(" " + path_str + " ") | bold | color(accent),
                }) | hcenter;
            } else {
                title = hbox({text(badge) | dim, text(" " + path_str + " ") | dim}) | hcenter;
            }

            std::vector<Element> pane_inner = {title, separator(), tree | flex};
            if (ps.jump_mode) {
                pane_inner.push_back(separator());
                pane_inner.push_back(hbox({text("cd: ") | dim, text(ps.jump_query + "_")}));
            }

            Element pane_box = vbox(std::move(pane_inner)) | flex;
            if (is_active) pane_box = pane_box | borderHeavy | color(accent);
            else           pane_box = pane_box | border | color(accentDim) | dim;
            pane_elements.push_back(pane_box);
        }

        std::vector<Element> main_elements;
        {
            int n = (int)pane_elements.size();
            if (n <= 3) {
                main_elements.push_back(hbox(std::move(pane_elements)) | flex);
            } else {
                int cols = (n + 1) / 2;
                std::vector<Element> top(std::make_move_iterator(pane_elements.begin()),
                                         std::make_move_iterator(pane_elements.begin() + cols));
                std::vector<Element> bot(std::make_move_iterator(pane_elements.begin() + cols),
                                         std::make_move_iterator(pane_elements.end()));
                main_elements.push_back(vbox({
                    hbox(std::move(top)) | flex,
                    hbox(std::move(bot)) | flex,
                }) | flex);
            }
        }

        if (!op_queue_.empty()) {
            std::vector<Element> qrows;
            int display = std::min((int)op_queue_.size(), 6);
            for (int i = 0; i < display; i++) {
                const auto& op = op_queue_[i];
                std::string label;
                Color c;
                switch (op.type) {
                    case QueuedOp::Type::Copy:
                        label = " [COPY] " + op.src + "  \u2192  " + op.dst;
                        c = Color::Green; break;
                    case QueuedOp::Type::Move:
                        label = " [MOVE] " + op.src + "  \u2192  " + op.dst;
                        c = Color::Yellow; break;
                    case QueuedOp::Type::Delete:
                        label = " [DEL]  " + op.src;
                        c = Color::Red; break;
                }
                qrows.push_back(text(label) | color(c));
            }
            if ((int)op_queue_.size() > 6)
                qrows.push_back(text("  ... +" + std::to_string(op_queue_.size() - 6) + " more") | dim);
            main_elements.push_back(separator());
            main_elements.push_back(vbox(std::move(qrows)));
        }

        PaneState& active_ps = cur_state();
        Element status;
        if (active_ps.fuzzy_mode) {
            status = text("fuzzy: " + active_ps.fuzzy_query);
        } else if (!last_error_.empty()) {
            status = text("ERROR: " + last_error_) | color(Color::Red);
        } else if (!op_queue_.empty()) {
            status = text(" Ctrl+R: run  Ctrl+U: clear  ("
                          + std::to_string(op_queue_.size()) + " pending)") | color(Color::Yellow);
        } else {
            std::string hint = " y: copy  x: cut  p: paste  D: delete  f: fuzzy  /: jump";
            hint += "  t: new  T: SSH  Tab: switch  q: close  Q: quit";
            status = text(hint) | dim;
        }
        main_elements.push_back(separator());
        main_elements.push_back(status);

        Element base = vbox(std::move(main_elements));

        auto show_modal = [](Element modal) -> Element {
            return vbox({filler(), hbox({filler(), std::move(modal), filler()}), filler()})
                   | bgcolor(Color::Black) | color(Color::White);
        };

        // ── Spinner overlay ───────────────────────────────────────────────
        if (loading_) {
            std::string frame = kSpinner[spinner_frame_.load() % 10];
            Element modal = vbox({
                text(""),
                text("  " + frame + "  " + loading_msg_ + "  ") | hcenter,
                text(""),
            }) | border;
            return show_modal(std::move(modal));
        }

        // ── Connect overlay ───────────────────────────────────────────────
        if (overlay_.active) {
            auto field_elem = [&](const std::string& label, const std::string& value,
                                  bool masked, bool focused) {
                std::string display = masked ? std::string(value.size(), '*') : value;
                if (focused) display += "_";
                Element val = text(display);
                if (focused) val = val | inverted;
                return hbox({text(label), val});
            };
            std::vector<Element> rows;
            rows.push_back(text(" Connect to Remote Host ") | bold | hcenter);
            rows.push_back(separator());
            rows.push_back(field_elem(" user@host : ", overlay_.user_host, false, overlay_.field == 0));
            rows.push_back(field_elem(" password  : ", overlay_.password,  true,  overlay_.field == 1));
            if (!overlay_.error.empty())
                rows.push_back(text(" " + overlay_.error + " ") | color(Color::Red));
            rows.push_back(separator());
            rows.push_back(text(" Tab: next field   Enter: connect   Esc: cancel ") | dim | hcenter);
            Element modal = vbox(std::move(rows)) | border;
            return show_modal(std::move(modal));
        }

        return base;
    });

    auto with_events = CatchEvent(ui, [&](Event event) -> bool {
        last_error_.clear();

        // ── Async completion ──────────────────────────────────────────────
        if (event == Event::Custom) {
            spinner_frame_++;
            if (!loading_) {
                std::lock_guard<std::mutex> lk(result_mutex_);
                if (on_load_complete_) {
                    if (!loading_cancelled_) on_load_complete_();
                    on_load_complete_ = nullptr;
                    loading_cancelled_ = false;
                }
            }
            return true;
        }

        // Block all input while loading, except ESC to cancel
        if (loading_) {
            if (event == Event::Escape) {
                loading_cancelled_ = true;
                loading_ = false;
            }
            return true;
        }

        // ── Connect overlay ───────────────────────────────────────────────
        if (overlay_.active) {
            if (event == Event::Escape) { overlay_ = {}; return true; }
            if (event == Event::Tab) { overlay_.field = 1 - overlay_.field; return true; }
            if (event == Event::Return) {
                auto& uh = overlay_.user_host;
                auto at = uh.find('@');
                if (at == std::string::npos || at == 0 || at == uh.size() - 1) {
                    overlay_.error = "Format must be user@host";
                    return true;
                }
                std::string user = uh.substr(0, at);
                std::string host = uh.substr(at + 1);
                std::string password = overlay_.password;
                // shared_ptr<shared_ptr> lets work() write the session for on_done() to read
                auto result = std::make_shared<std::shared_ptr<SshSession>>();
                overlay_.active = false; // hide form while spinner shows
                start_loading("Connecting to " + host + "...",
                    [result, host, user, password]() {
                        auto s = std::make_shared<SshSession>(host, user);
                        if (s->connect_password(password)) *result = s;
                    },
                    [this, result]() {
                        if (*result) {
                            PaneState new_ps;
                            new_ps.pane = std::make_unique<RemotePane>(*result, "/");
                            panes_.push_back(std::move(new_ps));
                            active_pane_ = (int)panes_.size() - 1;
                            overlay_ = {};
                        } else {
                            overlay_.error = "Connection failed";
                            overlay_.active = true;
                        }
                    }
                );
                return true;
            }
            if (event == Event::Backspace) {
                auto& field = overlay_.field == 0 ? overlay_.user_host : overlay_.password;
                if (!field.empty()) field.pop_back();
                return true;
            }
            if (event.is_character()) {
                auto& field = overlay_.field == 0 ? overlay_.user_host : overlay_.password;
                field += event.character();
                return true;
            }
            return true;
        }

        PaneState& ps = cur_state();
        Pane& p = cur_pane();

        // ── Jump mode ─────────────────────────────────────────────────────
        if (ps.jump_mode) {
            auto find_matches = [&]() -> std::vector<int> {
                std::vector<int> matches;
                const auto& ents = p.entries();
                std::string q = ps.jump_query;
                std::transform(q.begin(), q.end(), q.begin(), ::tolower);
                for (int i = 0; i < (int)ents.size(); i++) {
                    std::string name = ents[i].name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (name.rfind(q, 0) == 0) matches.push_back(i);
                }
                return matches;
            };
            auto jump_to = [&](int idx) {
                p.cursor = idx;
                update_scroll(p.cursor, (int)p.entries().size());
            };

            if (event == Event::Escape) {
                ps.jump_mode = false; ps.jump_query.clear(); ps.jump_match = 0;
                return true;
            }
            if (event == Event::Return) {
                const auto& ents = p.entries();
                if (p.cursor < (int)ents.size() && ents[p.cursor].is_dir) {
                    cur_pane().navigate(ents[p.cursor].full_path);
                    ps.scroll_offset = 0; ps.jump_query.clear(); ps.jump_match = 0;
                    if (p.entries().empty()) ps.jump_mode = false;
                }
                return true;
            }
            if (event == Event::Tab) {
                auto m = find_matches();
                if (!m.empty()) { ps.jump_match = (ps.jump_match + 1) % (int)m.size(); jump_to(m[ps.jump_match]); }
                return true;
            }
            if (event == Event::Backspace) {
                if (!ps.jump_query.empty()) {
                    ps.jump_query.pop_back(); ps.jump_match = 0;
                    auto m = find_matches(); if (!m.empty()) jump_to(m[0]);
                } else { ps.jump_mode = false; }
                return true;
            }
            if (event.is_character()) {
                ps.jump_query += event.character(); ps.jump_match = 0;
                auto m = find_matches(); if (!m.empty()) jump_to(m[0]);
                return true;
            }
            return false;
        }

        // ── Fuzzy mode ────────────────────────────────────────────────────
        if (ps.fuzzy_mode) {
            if (event == Event::Escape) {
                ps.fuzzy_mode = false; ps.fuzzy_query.clear();
                ps.fuzzy_results.clear(); ps.fuzzy_cursor = 0;
                return true;
            }
            if (event == Event::Return) {
                if (!ps.fuzzy_results.empty()) {
                    const std::string sel = ps.fuzzy_results[ps.fuzzy_cursor];
                    std::string target_full = sel;
                    std::string nav_to;
                    // For dirs navigate into them; for files navigate to parent
                    bool sel_is_dir = false;
                    if (p.type() == PaneType::Remote) {
                        // Can't call fs::is_directory on remote paths — check entries type
                        // We'll navigate to parent and position on the file name
                        fs::path sp(sel);
                        nav_to = sp.parent_path().string();
                    } else {
                        fs::path sp(sel);
                        sel_is_dir = fs::is_directory(sp);
                        nav_to = sel_is_dir ? sel : sp.parent_path().string();
                    }
                    cur_pane().navigate(nav_to);
                    // Find the selected entry and position cursor on it
                    const auto& ents = cur_pane().entries();
                    for (int i = 0; i < (int)ents.size(); i++) {
                        if (ents[i].full_path == target_full || ents[i].name == fs::path(target_full).filename().string()) {
                            p.cursor = i;
                            int vis = visible_rows() / pane_rows();
                            ps.scroll_offset = std::clamp(i - vis / 2, 0, std::max(0, (int)ents.size() - vis));
                            break;
                        }
                    }
                }
                ps.fuzzy_mode = false; ps.fuzzy_query.clear();
                ps.fuzzy_results.clear(); ps.fuzzy_cursor = 0;
                return true;
            }
            if (event == Event::ArrowDown) {
                if (ps.fuzzy_cursor < (int)ps.fuzzy_results.size() - 1) ps.fuzzy_cursor++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (ps.fuzzy_cursor > 0) ps.fuzzy_cursor--;
                return true;
            }
            if (event == Event::Backspace) {
                if (!ps.fuzzy_query.empty()) {
                    ps.fuzzy_query.pop_back();
                    ps.fuzzy_results = ps.fuzzy_query.empty()
                        ? ps.fuzzy_candidates
                        : fuzzy::search(ps.fuzzy_candidates, ps.fuzzy_query);
                    ps.fuzzy_cursor = 0;
                }
                return true;
            }
            if (event.is_character()) {
                ps.fuzzy_query += event.character();
                ps.fuzzy_results = fuzzy::search(ps.fuzzy_candidates, ps.fuzzy_query);
                ps.fuzzy_cursor = 0;
                return true;
            }
            return false;
        }

        // ── Normal mode ───────────────────────────────────────────────────
        const auto& entries = p.entries();
        int n = (int)entries.size();

        // Ctrl+U: clear queue and yank
        if (event == Event::Special("\x15")) {
            op_queue_.clear(); clipboard_active_ = false;
            return true;
        }

        // Ctrl+R: execute queue async
        if (event == Event::Special("\x12") && !op_queue_.empty()) {
            auto saved  = std::make_shared<std::vector<SavedPane>>(save_pane_states());
            auto ops    = std::make_shared<std::vector<QueuedOp>>(std::move(op_queue_));
            auto errors = std::make_shared<std::string>();
            op_queue_.clear();

            start_loading("Executing...",
                [ops, errors]() {
                    for (const auto& op : *ops) {
                        bool ok = false;
                        bool src_r = (op.src_session != nullptr);
                        bool dst_r = (op.dst_session != nullptr);
                        if (op.type == QueuedOp::Type::Delete) {
                            ok = src_r ? FileOps::delete_remote(*op.src_session, op.src)
                                       : FileOps::delete_local(op.src);
                        } else if (!src_r && !dst_r) {
                            ok = op.type == QueuedOp::Type::Copy
                                ? FileOps::copy_local(op.src, op.dst)
                                : FileOps::move_local(op.src, op.dst);
                        } else if (!src_r && dst_r) {
                            ok = FileOps::copy_local_to_remote(*op.dst_session, op.src, op.dst);
                            if (ok && op.type == QueuedOp::Type::Move) FileOps::delete_local(op.src);
                        } else if (src_r && !dst_r) {
                            ok = FileOps::copy_remote_to_local(*op.src_session, op.src, op.dst);
                            if (ok && op.type == QueuedOp::Type::Move) FileOps::delete_remote(*op.src_session, op.src);
                        } else {
                            ok = FileOps::copy_remote_to_remote(*op.src_session, op.src, *op.dst_session, op.dst);
                            if (ok && op.type == QueuedOp::Type::Move) FileOps::delete_remote(*op.src_session, op.src);
                        }
                        if (!ok) *errors += "failed: " + op.src + "; ";
                    }
                },
                [this, saved, errors, &restore_pane_states]() {
                    if (!errors->empty()) last_error_ = *errors;
                    restore_pane_states(*saved);
                }
            );
            return true;
        }

        if (event == Event::Character('Q')) { screen.ExitLoopClosure()(); return true; }
        if (event == Event::Tab) { active_pane_ = (active_pane_ + 1) % (int)panes_.size(); return true; }

        if (event == Event::Character('r')) { cur_pane().refresh(); return true; }
        if (event == Event::Character('T')) { overlay_ = {}; overlay_.active = true; return true; }
        if (event == Event::Character('t')) {
            PaneState new_ps;
            new_ps.pane = std::make_unique<LocalPane>(p.current_path);
            panes_.push_back(std::move(new_ps));
            active_pane_ = (int)panes_.size() - 1;
            return true;
        }
        if (event == Event::Character('q')) {
            panes_.erase(panes_.begin() + active_pane_);
            if (panes_.empty()) screen.ExitLoopClosure()();
            else active_pane_ = std::min(active_pane_, (int)panes_.size() - 1);
            return true;
        }

        // ── Clipboard / queue ops ─────────────────────────────────────────
        if (event == Event::Character('y')) {
            if (n > 0 && p.cursor < n) {
                clipboard_.op = ClipboardOp::Copy;
                clipboard_.path = entries[p.cursor].full_path;
                clipboard_.session = (p.type() == PaneType::Remote)
                    ? static_cast<RemotePane*>(&p)->session_ptr() : nullptr;
                clipboard_active_ = true;
            }
            return true;
        }
        if (event == Event::Character('x')) {
            if (n > 0 && p.cursor < n) {
                clipboard_.op = ClipboardOp::Cut;
                clipboard_.path = entries[p.cursor].full_path;
                clipboard_.session = (p.type() == PaneType::Remote)
                    ? static_cast<RemotePane*>(&p)->session_ptr() : nullptr;
                clipboard_active_ = true;
            }
            return true;
        }
        if (event == Event::Character('p')) {
            if (clipboard_active_) {
                fs::path src_path(clipboard_.path);
                std::string dest_dir = (n > 0 && p.cursor < n && entries[p.cursor].is_dir)
                    ? entries[p.cursor].full_path : p.current_path;
                std::string dst = (fs::path(dest_dir) / src_path.filename()).string();
                QueuedOp op;
                op.type = clipboard_.op == ClipboardOp::Copy ? QueuedOp::Type::Copy : QueuedOp::Type::Move;
                op.src = clipboard_.path;
                op.dst = dst;
                op.src_session = clipboard_.session;
                op.dst_session = (p.type() == PaneType::Remote)
                    ? static_cast<RemotePane*>(&p)->session_ptr() : nullptr;
                op_queue_.push_back(op);
            }
            return true;
        }
        if (event == Event::Character('D')) {
            if (n > 0 && p.cursor < n) {
                QueuedOp op;
                op.type = QueuedOp::Type::Delete;
                op.src = entries[p.cursor].full_path;
                if (p.type() == PaneType::Remote)
                    op.src_session = static_cast<RemotePane*>(&p)->session_ptr();
                op_queue_.push_back(op);
            }
            return true;
        }

        // ── Navigation ────────────────────────────────────────────────────
        if (event == Event::Character('j') || event == Event::ArrowDown) {
            if (p.cursor < n - 1) { p.cursor++; update_scroll(p.cursor, n); }
            return true;
        }
        if (event == Event::Character('k') || event == Event::ArrowUp) {
            if (p.cursor > 0) { p.cursor--; update_scroll(p.cursor, n); }
            return true;
        }
        if (event == Event::Character(' ')) {
            if (n > 0 && p.cursor < n && entries[p.cursor].is_dir) {
                cur_pane().toggle_expand(p.cursor);
                update_scroll(p.cursor, (int)p.entries().size());
            }
            return true;
        }
        if (event == Event::Character('l') || event == Event::ArrowRight) {
            if (n > 0 && p.cursor < n && entries[p.cursor].is_dir) {
                cur_pane().navigate(entries[p.cursor].full_path);
                ps.scroll_offset = 0;
            }
            return true;
        }
        if (event == Event::Character('h') || event == Event::ArrowLeft) {
            fs::path parent = fs::path(p.current_path).parent_path();
            if (parent.string() != p.current_path) {
                cur_pane().navigate(parent.string());
                ps.scroll_offset = 0;
            }
            return true;
        }
        if (event == Event::Character('f')) {
            int pane_idx = active_pane_;
            bool is_remote = (p.type() == PaneType::Remote);
            std::string path = p.current_path;
            std::shared_ptr<SshSession> sess;
            if (is_remote) sess = static_cast<RemotePane*>(&p)->session_ptr();

            bool is_windows = is_remote && sess->is_windows();
            auto candidates = std::make_shared<std::vector<std::string>>();
            start_loading("Scanning...",
                [candidates, is_remote, is_windows, path, sess]() {
                    if (is_remote) {
                        if (is_windows) {
                            // Windows has no Unix `find`; use recursive SFTP listing instead
                            std::function<void(const std::string&)> recurse = [&](const std::string& dir) {
                                sftp_dir sd = sftp_opendir(sess->sftp(), dir.c_str());
                                if (!sd) return;
                                sftp_attributes attrs;
                                while ((attrs = sftp_readdir(sess->sftp(), sd)) != nullptr) {
                                    std::string name = attrs->name;
                                    bool is_dir = (attrs->type == SSH_FILEXFER_TYPE_DIRECTORY);
                                    sftp_attributes_free(attrs);
                                    if (name == "." || name == "..") continue;
                                    bool drive = (name.size() == 2 && name[1] == ':');
                                    std::string sep = (dir == "/" && drive) ? "" : "/";
                                    std::string child = dir + sep + name + (drive ? "/" : "");
                                    candidates->push_back(child);
                                    if (is_dir) recurse(child);
                                }
                                sftp_closedir(sd);
                            };
                            recurse(path);
                        } else {
                            // Unix remote: fast single round-trip via find
                            std::string output;
                            sess->exec("find " + path + " 2>/dev/null", output);
                            std::istringstream ss(output);
                            std::string line;
                            while (std::getline(ss, line))
                                if (!line.empty()) candidates->push_back(line);
                        }
                    } else {
                        try {
                            fs::recursive_directory_iterator it(
                                path, fs::directory_options::skip_permission_denied);
                            for (const auto& e : it)
                                candidates->push_back(e.path().string());
                        } catch (...) {}
                    }
                },
                [this, pane_idx, candidates]() {
                    if (pane_idx < (int)panes_.size()) {
                        auto& ps2 = panes_[pane_idx];
                        ps2.fuzzy_candidates = *candidates;
                        ps2.fuzzy_results    = *candidates;
                        ps2.fuzzy_cursor     = 0;
                        ps2.fuzzy_mode       = true;
                    }
                }
            );
            return true;
        }
        if (event == Event::Character('/')) {
            ps.jump_mode = true; ps.jump_query.clear(); ps.jump_match = 0;
            return true;
        }
        return false;
    });

    screen.Loop(with_events);
    return 0;
}
