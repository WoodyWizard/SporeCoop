// Devkit console: TCP line protocol on 127.0.0.1, command registry, main-thread queue.
//
// Request : one text line, e.g.  mem.read 0x401000 16
// Response: one JSON line {"ok":true,"out":"..."}
#include <winsock2.h>
#include <ws2tcpip.h>

#include "core.h"
#include "task_drop.h"

#include <deque>
#include <map>
#include <memory>
#include <mutex>

namespace sk {

// ---------------- main thread tasks ----------------
static std::mutex g_task_mu;
struct Task {
    std::function<void()> fn;
    const void* code = nullptr; // a mod's function (h_run_on_main / h_run_outside_frame), else null
    bool frame_only = false;    // tests (SK_CMD_FRAME_ONLY): run by a frame's pump only, not the window procedure's
};
static std::deque<Task> g_tasks;
static DWORD g_main_tid = 0;

void set_main_thread() { g_main_tid = GetCurrentThreadId(); }
DWORD main_thread_id() { return g_main_tid; }

void run_on_main(std::function<void()> fn) { run_on_main_from(nullptr, std::move(fn)); }

// tests (BUG-0966): a task the window procedure's pump leaves for the next frame's (inside the frame the game's own
// handler catches a fault; a fault in a message the game's loop dispatches ends the game)
void run_on_main_in_frame(std::function<void()> fn)
{
    std::lock_guard<std::mutex> lk(g_task_mu);
    Task t{std::move(fn), nullptr};
    t.frame_only = true;
    g_tasks.push_back(std::move(t));
}

void run_on_main_from(const void* code, std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lk(g_task_mu);
        g_tasks.push_back({std::move(fn), code});
    }
    // also wake the window procedure: works during loading screens / broken frames
    if (GetCurrentThreadId() != g_main_tid)
        if (HWND w = (HWND)game_window())
            PostMessageW(w, WM_SK_PUMP, 0, 0);
}

// tasks that must not run inside IDirect3DDevice9::Present (DXVK holds its locks there):
// anything that may create D3D resources, e.g. Spore UI windows (fonts, textures)
static std::deque<Task> g_out_tasks;

void run_outside_frame(std::function<void()> fn) { run_outside_frame_from(nullptr, std::move(fn)); }

void run_outside_frame_from(const void* code, std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lk(g_task_mu);
        g_out_tasks.push_back({std::move(fn), code});
    }
    if (HWND w = (HWND)game_window())
        PostMessageW(w, WM_SK_PUMP_OUT, 0, 0);
}

// BUG-0672: the repost at once looped for ever when the frame flag stayed set after a fault the game's handler caught
// (kit.fix frameunwind clears it after App::Canvas::Tick, but Canvas::Tick's PeekMessage loop got the reposted message
// back every time and never returned: no frame ran again). With kit.fix outpump the tasks wait for the frame's end
// (d3d9hook.cpp: the end of a frame, of Present, or the cut-off), which posts the message again.
static bool g_out_deferred;

void outside_tasks_resume()
{
    if (!g_out_deferred)
        return;
    g_out_deferred = false;
    if (HWND w = (HWND)game_window())
        PostMessageW(w, WM_SK_PUMP_OUT, 0, 0);
}

void pump_outside_tasks()
{
    command_tasks_cut_off((uintptr_t)__builtin_frame_address(0)); // BUG-0966
    if (in_frame()) { // called from a message dispatched inside Present: try again later
        if (fixed(g_fix_outpump)) {
            g_out_deferred = true;
            return;
        }
        if (HWND w = (HWND)game_window())
            PostMessageW(w, WM_SK_PUMP_OUT, 0, 0);
        return;
    }
    for (int guard = 0; guard < 256; guard++) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lk(g_task_mu);
            if (g_out_tasks.empty())
                return;
            fn = std::move(g_out_tasks.front().fn);
            g_out_tasks.pop_front();
        }
        fn();
    }
}

// BUG-0060: a hot reload frees the old image of a mod; a task it queued (and the window procedure
// postponed while a message was dispatched inside Present) would then call into unmapped memory
size_t drop_tasks_in(uintptr_t lo, uintptr_t hi)
{
    // BUG-0860: the dropped tasks are destroyed after g_task_mu is unlocked (task_drop.h); `kit.fix taskdrop 0`: under it
    return drop_tasks(g_task_mu, {&g_tasks, &g_out_tasks},
                      [&](const Task& t) { return (uintptr_t)t.code >= lo && (uintptr_t)t.code < hi; }, fixed(g_fix_taskdrop));
}

struct DelayedTask {
    uint64_t due;
    std::function<void()> fn;
};
static std::vector<DelayedTask> g_delayed;

void run_after_frames(int frames, std::function<void()> fn)
{
    std::lock_guard<std::mutex> lk(g_task_mu);
    g_delayed.push_back({frame_index() + (uint64_t)(frames > 0 ? frames : 0), std::move(fn)});
}

void pump_main_tasks()
{
    command_tasks_cut_off((uintptr_t)__builtin_frame_address(0)); // BUG-0966
    std::vector<std::function<void()>> due;
    {
        std::lock_guard<std::mutex> lk(g_task_mu);
        uint64_t f = frame_index();
        for (size_t i = 0; i < g_delayed.size();) {
            if (g_delayed[i].due <= f) {
                due.push_back(std::move(g_delayed[i].fn));
                g_delayed.erase(g_delayed.begin() + i);
            } else
                i++;
        }
    }
    for (auto& fn : due)
        fn();
    for (int guard = 0; guard < 256; guard++) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lk(g_task_mu);
            if (g_tasks.empty() || (g_tasks.front().frame_only && !in_frame()))
                return;
            fn = std::move(g_tasks.front().fn);
            g_tasks.pop_front();
        }
        fn();
    }
}

// ---------------- command registry ----------------
struct Cmd {
    SkMod* mod;
    std::string help;
    SkCommandFn fn;
    void* user;
    int flags;
};
static std::mutex g_cmd_mu;
static std::map<std::string, Cmd> g_cmds;

int command_add(SkMod* mod, const char* name, const char* help, SkCommandFn fn, void* user,
                int flags)
{
    std::lock_guard<std::mutex> lk(g_cmd_mu);
    g_cmds[name] = Cmd{mod, help ? help : "", fn, user, flags};
    return 0;
}

void commands_remove_all(SkMod* mod)
{
    std::lock_guard<std::mutex> lk(g_cmd_mu);
    for (auto it = g_cmds.begin(); it != g_cmds.end();)
        if (it->second.mod == mod)
            it = g_cmds.erase(it);
        else
            ++it;
}

std::vector<std::pair<std::string, std::string>> commands_list()
{
    std::lock_guard<std::mutex> lk(g_cmd_mu);
    std::vector<std::pair<std::string, std::string>> v;
    for (auto& [n, c] : g_cmds)
        v.emplace_back(n, c.help);
    return v;
}

std::vector<std::string> split_args(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool inq = false, have = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inq) {
            if (c == '\\' && i + 1 < line.size()) {
                cur += line[++i];
            } else if (c == '"')
                inq = false;
            else
                cur += c;
        } else if (c == '"') {
            inq = true;
            have = true;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (have || !cur.empty())
                out.push_back(cur);
            cur.clear();
            have = false;
        } else
            cur += c;
    }
    if (have || !cur.empty())
        out.push_back(cur);
    return out;
}

// BUG-0966: a console command run as a main-thread (or outside-frame) task that faults, with the game's own handler
// catching the fault, never came back: its request was never answered, the client waited for ever (the lab's 90 s
// timeout) and every later line of that connection queued behind it. Seen with coop.puppet respawn whose destroy
// faulted in a first version of BUG-0930's fix; the frames went on (BUG-0667). A running task is noted with its stack
// address (main thread only); the next pump or game loop pass at or above it (not nested inside it) finds the note
// still there: the task was cut off and its request is answered with an error. `kit.fix taskcut 0`: no answer.
struct RunningCmd {
    uint64_t id;
    uintptr_t sp;
    std::string name;
    std::function<void(const std::string&)> answer;
};
static std::vector<RunningCmd> g_running; // innermost last; the main thread only
static uint64_t g_running_ids;
static uint32_t g_cmds_cut;

static void running_cut(RunningCmd& r)
{
    g_cmds_cut++;
    SKLOG(SK_WARN, "console: the command '%s' was cut off by a fault the game's handler caught (%u so far); "
                   "it is answered with an error (BUG-0966)", r.name.c_str(), g_cmds_cut);
    r.answer("error: '" + r.name + "' was cut off by a fault the game's handler caught (the exception is in the "
             "SporeKit log); the game goes on");
}

void command_tasks_cut_off(uintptr_t sp)
{
    if (g_running.empty() || GetCurrentThreadId() != g_main_tid)
        return;
    while (!g_running.empty() && sp >= g_running.back().sp) {
        RunningCmd r = std::move(g_running.back());
        g_running.pop_back();
        running_cut(r);
    }
}

static uint64_t running_begin(uintptr_t sp, const std::string& name, std::function<void(const std::string&)> answer)
{
    if (!fixed(g_fix_taskcut) || GetCurrentThreadId() != g_main_tid)
        return 0;
    uint64_t id = ++g_running_ids;
    // sp: the task's own frame (a pump or game loop pass later at or above it is not nested inside it)
    g_running.push_back({id, sp, name, std::move(answer)});
    return id;
}

static void running_end(uint64_t id)
{
    while (id && !g_running.empty()) {
        RunningCmd r = std::move(g_running.back());
        g_running.pop_back();
        if (r.id == id)
            return;
        running_cut(r); // a task nested inside this one that a fault cut off
    }
}

static int run_cmd(const Cmd& c, const std::vector<std::string>& args, std::string& out)
{
    std::vector<const char*> argv;
    for (auto& a : args)
        argv.push_back(a.c_str());
    Out o;
    int r = c.fn(c.user, (int)argv.size(), argv.data(), reinterpret_cast<SkOut*>(&o));
    out += o.text;
    return r;
}

// Executes a full line. If from_main is false and the command needs the main
// thread, the call is marshalled and waited for.
int command_exec_line(const std::string& line, std::string& out, bool from_main)
{
    auto args = split_args(line);
    if (args.empty())
        return 0;
    Cmd c;
    {
        std::lock_guard<std::mutex> lk(g_cmd_mu);
        auto it = g_cmds.find(args[0]);
        if (it == g_cmds.end()) {
            out += "unknown command: " + args[0] + " (try 'help')";
            return 1;
        }
        c = it->second;
    }
    // on the main thread a command that waits for the next frames (or suspends the main thread)
    // waits for itself: the game stops (BUG-0065, the cheat console `sk shot`)
    if ((c.flags & SK_CMD_NOT_ON_MAIN) && GetCurrentThreadId() == main_thread_id() && fixed(g_fix_cheatwait)) {
        out += "'" + args[0] + "' waits for the game's frames and cannot run on the game's own thread "
               "(the cheat console): use the devkit console (tools/sporectl.py)";
        return 1;
    }
    if (from_main || (c.flags & SK_CMD_ANY_THREAD))
        return run_cmd(c, args, out);

    struct Req {
        HANDLE ev;
        std::string out;
        int rc = -1;
        std::vector<std::string> args;
        std::string name;
    };
    auto req = std::make_shared<Req>();
    req->ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    req->args = args;
    req->name = args[0];
    auto post = (c.flags & SK_CMD_OUTSIDE_FRAME) ? run_outside_frame
                : (c.flags & SK_CMD_FRAME_ONLY)    ? run_on_main_in_frame
                                                   : run_on_main;
    post([req] {
        Cmd cc;
        {
            std::lock_guard<std::mutex> lk(g_cmd_mu);
            auto it = g_cmds.find(req->name);
            if (it == g_cmds.end()) {
                req->out = "command vanished";
                SetEvent(req->ev);
                return;
            }
            cc = it->second;
        }
        // BUG-0966: answered with an error if a caught fault cuts the command off
        uint64_t id = running_begin((uintptr_t)__builtin_frame_address(0), req->name, [req](const std::string& t) {
            req->out = t;
            req->rc = 1;
            SetEvent(req->ev);
        });
        req->rc = run_cmd(cc, req->args, req->out);
        running_end(id);
        SetEvent(req->ev);
    });
    DWORD w = WaitForSingleObject(req->ev, 60000);
    if (w != WAIT_OBJECT_0) {
        out += "timeout: main thread did not run the command within 60s (game loading or "
               "no frames yet)";
        return 2; // req stays alive via shared_ptr in the queued lambda
    }
    CloseHandle(req->ev);
    out += req->out;
    return req->rc;
}

// ---------------- TCP server ----------------
// One thread, select() over all clients: no thread is created per connection (thread
// start-up can stall in a second Wine instance while the first one runs).
static SOCKET g_listen = INVALID_SOCKET;

struct Client {
    SOCKET s;
    std::string in;
    // in-flight request (requests of one client are answered in order)
    std::shared_ptr<std::string> pending_out;
    std::shared_ptr<int> pending_rc;
    std::shared_ptr<bool> pending_done;
    std::deque<std::string> queue;
    std::string outbuf;
};

static void send_all(SOCKET s, const std::string& d)
{
    size_t off = 0;
    while (off < d.size()) {
        int r = send(s, d.data() + off, (int)(d.size() - off), 0);
        if (r <= 0)
            return;
        off += r;
    }
}

static std::string json_reply(int rc, const std::string& out)
{
    return std::string("{\"ok\":") + (rc == 0 ? "true" : "false") + ",\"rc\":" + std::to_string(rc) +
           ",\"out\":\"" + json_escape(out) + "\"}\n";
}

static std::mutex g_done_mu; // protects pending_* written by the main thread

static void start_next(Client& c)
{
    while (!c.pending_done && !c.queue.empty()) {
        std::string line = c.queue.front();
        c.queue.pop_front();
        auto args = split_args(line);
        if (args.empty())
            continue;
        Cmd cmd;
        bool found;
        {
            std::lock_guard<std::mutex> lk(g_cmd_mu);
            auto it = g_cmds.find(args[0]);
            found = it != g_cmds.end();
            if (found)
                cmd = it->second;
        }
        if (!found) {
            c.outbuf += json_reply(1, "unknown command: " + args[0] + " (try 'help')");
            continue;
        }
        if (cmd.flags & SK_CMD_ANY_THREAD) {
            std::string out;
            int rc = run_cmd(cmd, args, out);
            c.outbuf += json_reply(rc, out);
            continue;
        }
        auto out = std::make_shared<std::string>();
        auto rc = std::make_shared<int>(-1);
        auto done = std::make_shared<bool>(false);
        c.pending_out = out;
        c.pending_rc = rc;
        c.pending_done = done;
        std::string name = args[0];
        auto post = (cmd.flags & SK_CMD_OUTSIDE_FRAME) ? run_outside_frame
                    : (cmd.flags & SK_CMD_FRAME_ONLY)    ? run_on_main_in_frame
                                                         : run_on_main;
        post([out, rc, done, args, name] {
            Cmd cc;
            bool ok;
            {
                std::lock_guard<std::mutex> lk(g_cmd_mu);
                auto it = g_cmds.find(name);
                ok = it != g_cmds.end();
                if (ok)
                    cc = it->second;
            }
            std::string o;
            // BUG-0966: answered with an error if a caught fault cuts the command off
            uint64_t id = ok ? running_begin((uintptr_t)__builtin_frame_address(0), name, [out, rc, done](const std::string& t) {
                std::lock_guard<std::mutex> lk(g_done_mu);
                *out = t;
                *rc = 1;
                *done = true;
            }) : 0;
            int r = ok ? run_cmd(cc, args, o) : 1;
            running_end(id);
            std::lock_guard<std::mutex> lk(g_done_mu);
            *out = ok ? o : "command vanished";
            *rc = r;
            *done = true;
        });
    }
}

static DWORD WINAPI server_thread(void*)
{
    // Non-blocking sockets polled every few ms: Wine's select()/blocking close can
    // stall for tens of seconds in a second game process sharing the wineserver.
    std::vector<Client> clients;
    SKLOG(SK_DEBUG, "console thread %lu running", GetCurrentThreadId());
    u_long nb = 1;
    ioctlsocket(g_listen, FIONBIO, &nb);
    for (;;) {
        bool idle = true;
        SOCKET s = accept(g_listen, nullptr, nullptr);
        if (s != INVALID_SOCKET) {
            idle = false;
            ioctlsocket(s, FIONBIO, &nb);
            if (clients.size() < 32)
                clients.push_back(Client{s});
            else
                closesocket(s);
        }
        for (size_t i = 0; i < clients.size();) {
            Client& c = clients[i];
            bool dead = false;
            char tmp[4096];
            int r = recv(c.s, tmp, sizeof tmp, 0);
            if (r == 0)
                dead = true;
            else if (r < 0) {
                int e = WSAGetLastError();
                if (e != WSAEWOULDBLOCK)
                    dead = true;
            } else {
                idle = false;
                c.in.append(tmp, r);
                size_t nl;
                while ((nl = c.in.find('\n')) != std::string::npos) {
                    std::string line = c.in.substr(0, nl);
                    c.in.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (!line.empty())
                        c.queue.push_back(line);
                }
            }
            if (c.pending_done) {
                std::lock_guard<std::mutex> lk(g_done_mu);
                if (*c.pending_done) {
                    c.outbuf += json_reply(*c.pending_rc, *c.pending_out);
                    c.pending_done.reset();
                    c.pending_out.reset();
                    c.pending_rc.reset();
                }
            }
            start_next(c);
            while (!c.outbuf.empty()) {
                int w = send(c.s, c.outbuf.data(), (int)c.outbuf.size(), 0);
                if (w > 0)
                    c.outbuf.erase(0, w);
                else if (WSAGetLastError() == WSAEWOULDBLOCK)
                    break;
                else {
                    dead = true;
                    break;
                }
            }
            // keep the connection until the answer of an in-flight request was sent
            if (dead && !c.pending_done) {
                closesocket(c.s);
                clients.erase(clients.begin() + i);
            } else
                i++;
        }
        if (idle)
            Sleep(5);
    }
    return 0;
}

bool console_start(int port)
{
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        SKLOG(SK_ERROR, "WSAStartup failed");
        return false;
    }
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET)
        return false;
    BOOL yes = TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_listen, (sockaddr*)&a, sizeof a) != 0 || listen(g_listen, 8) != 0) {
        SKLOG(SK_ERROR, "console: cannot listen on 127.0.0.1:%d (err %d)", port, WSAGetLastError());
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return false;
    }
    HANDLE t = CreateThread(nullptr, 0, server_thread, nullptr, 0, nullptr);
    if (t)
        CloseHandle(t);
    SKLOG(SK_INFO, "console listening on 127.0.0.1:%d", port);
    return true;
}

} // namespace sk
