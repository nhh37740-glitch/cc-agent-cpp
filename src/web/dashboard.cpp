#include "web/dashboard.hpp"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <memory>
#include <sstream>
#include <stop_token>
#include <thread>

namespace web {

namespace {

void append_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void append_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

const char* dashboard_html() {
    return R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>边缘智能体关键帧面板</title>
<style>
:root{color-scheme:dark;font-family:Inter,"Microsoft YaHei",sans-serif;background:#090d14;color:#e8eef8}
*{box-sizing:border-box}
body{margin:0;padding:20px;height:100vh;display:flex;flex-direction:column;overflow:hidden}
.top{display:flex;gap:14px;align-items:center;flex-wrap:wrap;margin-bottom:12px}
h1{font-size:20px;margin:0}.pill{padding:6px 12px;border-radius:999px;background:#192332;color:#9fc2ff;font-size:13px}
#detail{color:#94a3b8;font-size:13px}
.main{flex:1;display:flex;gap:16px;min-height:0}
.center{flex:1;display:flex;flex-direction:column;min-width:0;background:#111925;border:1px solid #263346;border-radius:14px;overflow:hidden}
.center-img{flex:1;min-height:0;background:#05070a;display:flex;align-items:center;justify-content:center;position:relative}
.center-img img{max-width:100%;max-height:100%;object-fit:contain}
.center-info{padding:14px;border-top:1px solid #263346;max-height:40%;overflow-y:auto}
.center-meta{font-size:12px;color:#91a3ba;margin-bottom:8px}
.center-analysis{font-size:15px;line-height:1.6;white-space:pre-wrap;word-break:break-word}
.center-tools{margin-top:8px;font-size:12px;color:#a9c5a0}
.thumbs{width:200px;display:flex;flex-direction:column;gap:8px;overflow-y:auto}
.thumbs::-webkit-scrollbar{width:6px}.thumbs::-webkit-scrollbar-thumb{background:#263346;border-radius:3px}
.thumb{flex-shrink:0;background:#111925;border:2px solid transparent;border-radius:8px;overflow:hidden;cursor:pointer}
.thumb.active{border-color:#3b82f6}
.thumb img{width:100%;aspect-ratio:16/9;object-fit:cover;background:#05070a;display:block}
.thumb-time{padding:4px 8px;font-size:11px;color:#91a3ba}
.empty{padding:40px;text-align:center;color:#718096}
</style></head><body>
<div class="top"><h1>边缘智能体实时面板</h1><span id="state" class="pill">连接中</span><span id="detail">正在等待……</span></div>
<div class="main">
  <div class="center" id="center"></div>
  <div class="thumbs" id="thumbs"></div>
</div>
<script>
let last='',activeId=0;
// 模型输出属于不可信文本；插入 innerHTML 前必须转义。
const esc=v=>v==null?'':String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const stateText={starting:'正在启动',running:'正在运行',complete:'处理完成',failed:'运行失败',pushed:'已推送',final:'无需推送',step_limit_reached:'达到步骤上限'};
async function refresh(){try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error(r.status);const s=await r.json();
document.querySelector('#state').textContent=stateText[s.run_state]||'未知';
document.querySelector('#detail').textContent=s.run_detail||`历史 ${s.events.length} 帧`;
const signature=JSON.stringify(s);if(signature!==last){last=signature;render(s)}
}catch(e){document.querySelector('#state').textContent='连接已断开'}}
function render(s){
  const center=document.querySelector('#center');const thumbs=document.querySelector('#thumbs');
  center.replaceChildren();thumbs.replaceChildren();
  if(!s.events.length){center.innerHTML='<div class="empty">尚未推送关键帧</div>';return}
  // 自动跟踪最新帧：每次刷新都切到最新的推送帧
  activeId=s.events[0].id;
  const cur=s.events[0];
  const ts=Date.now();
  center.innerHTML=`<div class="center-img"><img src="${cur.frame_url}?t=${ts}" alt="关键帧 ${cur.id}"></div>`+
    `<div class="center-info"><div class="center-meta">编号 ${cur.id} · 视频时间 ${Number(cur.video_timestamp).toFixed(2)}s · 变化分数 ${Number(cur.change_score).toFixed(3)} · ${stateText[cur.status]||'未知'}</div>`+
    `<div class="center-analysis">${cur.observation?'视觉分析：'+esc(cur.observation):''}${cur.analysis?'\n\n推送摘要：'+esc(cur.analysis):''}${cur.reason?'\n\n原因：'+esc(cur.reason):''}</div>`+
    `${(cur.tool_results&&cur.tool_results.length)?'<div class="center-tools">工具调用：推送'+(cur.tool_results.every(v=>v.success)?'成功':'失败')+'</div>':''}</div>`;
  for(const x of s.events){
    const t=document.createElement('div');t.className='thumb'+(x.id===activeId?' active':'');
    t.onclick=()=>{activeId=x.id;render(s)};
    t.innerHTML=`<img src="${x.frame_url}" alt="${x.id}"><div class="thumb-time">${Number(x.video_timestamp).toFixed(1)}s</div>`;
    thumbs.append(t);
  }
}
refresh();setInterval(refresh,1000);
</script></body></html>)HTML";
}

bool send_all(SOCKET socket, const char* data, std::size_t size) {
    while (size > 0) {
        const int chunk = send(socket, data, static_cast<int>(std::min<std::size_t>(size, 1 << 20)), 0);
        if (chunk <= 0) return false;
        data += chunk;
        size -= static_cast<std::size_t>(chunk);
    }
    return true;
}

void send_response(SOCKET socket, int status, const char* status_text,
                   const char* content_type, const uint8_t* body, std::size_t body_size) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
            << "Content-Type: " << content_type << "\r\n"
            << "Content-Length: " << body_size << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n"
            << "Connection: close\r\n\r\n";
    const std::string h = headers.str();
    send_all(socket, h.data(), h.size());
    if (body_size) send_all(socket, reinterpret_cast<const char*>(body), body_size);
}

void send_text(SOCKET socket, int status, const char* status_text,
               const char* content_type, const std::string& body) {
    send_response(socket, status, status_text, content_type,
                  reinterpret_cast<const uint8_t*>(body.data()), body.size());
}

bool parse_frame_id(const std::string& path, uint64_t& id) {
    constexpr const char* prefix = "/frame/";
    constexpr const char* suffix = ".bmp";
    if (path.rfind(prefix, 0) != 0 || path.size() <= 7 + 4 ||
        path.compare(path.size() - 4, 4, suffix) != 0) return false;
    const std::string_view number(path.data() + 7, path.size() - 7 - 4);
    const auto [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), id);
    return ec == std::errc() && ptr == number.data() + number.size();
}

}  // namespace

DashboardState::DashboardState(std::size_t max_events)
    : max_events_(std::max<std::size_t>(1, max_events)) {}

void DashboardState::set_run_state(std::string state, std::string detail) {
    std::lock_guard lock(mutex_);
    run_state_ = std::move(state);
    run_detail_ = std::move(detail);
}

std::vector<uint8_t> DashboardState::rgb_to_bmp(const video::CandidateFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.rgb.size() < static_cast<std::size_t>(frame.width) * frame.height * 3) return {};
    const uint32_t row = (static_cast<uint32_t>(frame.width) * 3u + 3u) & ~3u;
    const uint32_t pixels = row * static_cast<uint32_t>(frame.height);
    std::vector<uint8_t> out;
    out.reserve(54u + pixels);
    out.push_back('B'); out.push_back('M'); append_u32(out, 54u + pixels);
    append_u16(out, 0); append_u16(out, 0); append_u32(out, 54);
    append_u32(out, 40); append_u32(out, static_cast<uint32_t>(frame.width));
    append_u32(out, static_cast<uint32_t>(frame.height)); append_u16(out, 1); append_u16(out, 24);
    append_u32(out, 0); append_u32(out, pixels); append_u32(out, 2835); append_u32(out, 2835);
    append_u32(out, 0); append_u32(out, 0);
    const uint32_t padding = row - static_cast<uint32_t>(frame.width) * 3u;
    for (int y = frame.height - 1; y >= 0; --y) {
        const uint8_t* src = frame.rgb.data() + static_cast<std::size_t>(y) * frame.width * 3;
        for (int x = 0; x < frame.width; ++x) {
            out.push_back(src[x * 3 + 2]); out.push_back(src[x * 3 + 1]); out.push_back(src[x * 3]);
        }
        for (uint32_t p = 0; p < padding; ++p) out.push_back(0);
    }
    return out;
}

uint64_t DashboardState::publish_candidate(const video::CandidateFrame& frame) {
    Event event;
    event.video_timestamp = frame.timestamp;
    event.change_score = frame.change_score;
    event.width = frame.width;
    event.height = frame.height;
    event.bmp = rgb_to_bmp(frame);
    std::lock_guard lock(mutex_);
    event.id = next_id_++;
    events_.push_front(std::move(event));
    return events_.front().id;
}

void DashboardState::publish_observation(uint64_t id, std::string observation) {
    std::lock_guard lock(mutex_);
    auto it = std::find_if(events_.begin(), events_.end(), [id](const Event& e) { return e.id == id; });
    if (it == events_.end()) return;
    it->observation = std::move(observation);
    it->status = "deciding";
}

bool DashboardState::publish_push(uint64_t id, std::string summary) {
    std::lock_guard lock(mutex_);
    auto it = std::find_if(events_.begin(), events_.end(), [id](const Event& e) { return e.id == id; });
    if (it == events_.end()) return false;
    it->pushed = true;
    it->status = "pushed";
    it->analysis = std::move(summary);
    while (events_.size() > max_events_) events_.pop_back();
    return true;
}

void DashboardState::discard(uint64_t id) {
    std::lock_guard lock(mutex_);
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [id](const Event& e) { return e.id == id; });
    if (it != events_.end() && !it->pushed) events_.erase(it);
}

void DashboardState::publish_result(uint64_t id, const agent::AgentResult& result,
                                    double latency_seconds) {
    std::lock_guard lock(mutex_);
    auto it = std::find_if(events_.begin(), events_.end(), [id](const Event& e) { return e.id == id; });
    if (it == events_.end()) return;
    it->latency_seconds = latency_seconds;
    if (it->pushed) {
        it->status = "pushed";
    } else if (result.status == agent::AgentStatus::CompletedFinal) {
        it->status = "final";
        it->analysis = result.content;
    } else {
        it->status = "step_limit_reached";
        it->reason = result.reason;
    }
    for (const auto& tr : result.tool_results) it->tool_results.push_back(tr.to_json());
}

nlohmann::ordered_json DashboardState::snapshot_json() const {
    std::lock_guard lock(mutex_);
    nlohmann::ordered_json out;
    out["run_state"] = run_state_;
    out["run_detail"] = run_detail_;
    out["events"] = nlohmann::ordered_json::array();
    for (const auto& e : events_) {
        if (!e.pushed) continue;
        out["events"].push_back({{"id", e.id}, {"video_timestamp", e.video_timestamp},
            {"change_score", e.change_score}, {"width", e.width}, {"height", e.height},
            {"status", e.status}, {"observation", e.observation},
            {"analysis", e.analysis}, {"reason", e.reason},
            {"latency_seconds", e.latency_seconds}, {"tool_results", e.tool_results},
            {"frame_url", "/frame/" + std::to_string(e.id) + ".bmp"}});
    }
    return out;
}

bool DashboardState::frame_bmp(uint64_t id, std::vector<uint8_t>& out) const {
    std::lock_guard lock(mutex_);
    const auto it = std::find_if(events_.begin(), events_.end(), [id](const Event& e) { return e.id == id; });
    if (it == events_.end() || it->bmp.empty()) return false;
    out = it->bmp;
    return true;
}

struct DashboardServer::Impl {
    DashboardConfig config;
    DashboardState& state;
    std::atomic<SOCKET> listener{INVALID_SOCKET};
    std::jthread thread;
    bool winsock_started = false;

    Impl(DashboardConfig c, DashboardState& s) : config(std::move(c)), state(s) {}

    void handle(SOCKET client) {
        char request[8192];
        const int n = recv(client, request, sizeof(request) - 1, 0);
        if (n <= 0) return;
        request[n] = '\0';
        std::istringstream first_line(std::string(request, static_cast<std::size_t>(n)));
        std::string method, path, version;
        first_line >> method >> path >> version;
        const auto query = path.find('?');
        if (query != std::string::npos) path.erase(query);
        if (method != "GET") {
            send_text(client, 405, "Method Not Allowed", "text/plain; charset=utf-8", "GET only\n");
        } else if (path == "/" || path == "/index.html") {
            send_text(client, 200, "OK", "text/html; charset=utf-8", dashboard_html());
        } else if (path == "/api/state") {
            send_text(client, 200, "OK", "application/json; charset=utf-8", state.snapshot_json().dump());
        } else if (path == "/healthz") {
            send_text(client, 200, "OK", "text/plain; charset=utf-8", "ok\n");
        } else {
            uint64_t id = 0;
            std::vector<uint8_t> bmp;
            if (parse_frame_id(path, id) && state.frame_bmp(id, bmp)) {
                send_response(client, 200, "OK", "image/bmp", bmp.data(), bmp.size());
            } else {
                send_text(client, 404, "Not Found", "text/plain; charset=utf-8", "not found\n");
            }
        }
    }
};

DashboardServer::DashboardServer(DashboardConfig config, DashboardState& state)
    : impl_(new Impl(std::move(config), state)) {}

DashboardServer::~DashboardServer() { stop(); delete impl_; }

bool DashboardServer::start(std::string& error) {
    if (impl_->config.port == 0) return true;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { error = "WSAStartup 失败"; return false; }
    impl_->winsock_started = true;
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const std::string port = std::to_string(impl_->config.port);
    if (getaddrinfo(impl_->config.bind_address.c_str(), port.c_str(), &hints, &addresses) != 0) {
        error = "无法解析 Web bind 地址: " + impl_->config.bind_address; stop(); return false;
    }
    SOCKET listener = socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    if (listener == INVALID_SOCKET) { freeaddrinfo(addresses); error = "创建 Web socket 失败"; stop(); return false; }
    BOOL reuse = TRUE; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    if (bind(listener, addresses->ai_addr, static_cast<int>(addresses->ai_addrlen)) == SOCKET_ERROR ||
        listen(listener, 16) == SOCKET_ERROR) {
        freeaddrinfo(addresses); closesocket(listener); error = "Web 端口绑定/监听失败: " + display_url(); stop(); return false;
    }
    freeaddrinfo(addresses);
    impl_->listener.store(listener);
    impl_->thread = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested()) {
            SOCKET current = impl_->listener.load();
            if (current == INVALID_SOCKET) break;
            SOCKET client = accept(current, nullptr, nullptr);
            if (client == INVALID_SOCKET) { if (st.stop_requested()) break; continue; }
            impl_->handle(client);
            shutdown(client, SD_BOTH); closesocket(client);
        }
    });
    return true;
}

void DashboardServer::stop() {
    if (!impl_) return;
    if (impl_->thread.joinable()) impl_->thread.request_stop();
    const SOCKET listener = impl_->listener.exchange(INVALID_SOCKET);
    if (listener != INVALID_SOCKET) { shutdown(listener, SD_BOTH); closesocket(listener); }
    if (impl_->thread.joinable()) impl_->thread.join();
    if (impl_->winsock_started) { WSACleanup(); impl_->winsock_started = false; }
}

bool DashboardServer::running() const { return impl_ && impl_->listener.load() != INVALID_SOCKET; }

std::string DashboardServer::display_url() const {
    const std::string host = impl_->config.bind_address == "0.0.0.0" ? "<本机局域网IP>" : impl_->config.bind_address;
    return "http://" + host + ":" + std::to_string(impl_->config.port) + "/";
}

}  // namespace web
