// spout-tool: synthetic Spout sender / receiver / lister on SpoutDX for the OBS Spout2 plugin tests.
//
//   spout-tool send --name N [--size WxH] [--fps 30] [--seed K] [--alpha opaque|straight|premult]
//                   [--adapter <index|substring>] [--format BGRA|RGBA] [--duration S]
//   spout-tool recv --name N [--frames 30] [--timeout 5] [--out file.bgra] [--adapter ...]
//   spout-tool list
//
// JSON contract (one object per line on stdout) is shared with tests/harness/spoutgl_tool.py:
//   send : {"event":"started",...} once the first frame is out, then {"event":"stopped","frames_sent":N,...}
//          when --duration elapses or a line is read on stdin ('q') / stdin closes.
//   recv : {"connected":bool,"width":W,"height":H,"format":DXGI,"frame_first":..,"frame_last":..,"fps":..,
//           "new_frames":..,"distinct_frames":..,"pattern_first":..,"pattern_last":..,"elapsed":..,"out":path}
//          --out writes raw pixels (W*H*4) plus "<out>.json" {"width","height","order":"bgra"|"rgba"}.
//   list : {"senders":[{"name","width","height","format","adapter"}]}
//
// The test pattern mirrors tests/harness/pattern.py exactly (integer arithmetic); keep both in sync.

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

#include "SpoutDX.h"

namespace {

// ----------------------------------------------------------------------------------------------
// Pattern (mirror of harness/pattern.py)
// ----------------------------------------------------------------------------------------------
constexpr int STRIP_CELLS = 16;
constexpr int FRAME_BITS = 16;
constexpr unsigned char GREY = 128;

struct Rgb {
	unsigned char r, g, b;
};
const Rgb HUE_TABLE[6] = {{255, 0, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {255, 0, 255}};

struct Box {
	int x0, y0, x1, y1;
};

int strip_height(int h)
{
	return std::max(8, h / 12);
}

struct Regions {
	Box tl, tr, bl, br, centre, strip;
};

Regions regions(int w, int h)
{
	const int sh = strip_height(h);
	const int body = h - sh;
	const int half_w = w / 2;
	const int half_h = body / 2;
	const int s = std::max(8, std::min(w, body) / 6);
	const int cx0 = w / 2 - s / 2;
	const int cy0 = body / 2 - s / 2;
	Regions r;
	r.tl = {0, 0, half_w, half_h};
	r.tr = {half_w, 0, w, half_h};
	r.bl = {0, half_h, half_w, body};
	r.br = {half_w, half_h, w, body};
	r.centre = {cx0, cy0, cx0 + s, cy0 + s};
	r.strip = {0, body, w, h};
	return r;
}

Box cell_box(int w, int h, int i)
{
	const Box strip = regions(w, h).strip;
	return {i * w / STRIP_CELLS, strip.y0, (i + 1) * w / STRIP_CELLS, strip.y1};
}

enum class Order { BGRA, RGBA };

// pixel buffer is w*h*4, top-down rows, channel order per `order`
void fill(std::vector<unsigned char> &px, int w, int h, const Box &b, unsigned char r, unsigned char g,
	  unsigned char bl, unsigned char a, Order order)
{
	for (int y = b.y0; y < b.y1; ++y) {
		for (int x = b.x0; x < b.x1; ++x) {
			unsigned char *p = &px[(size_t)(y * w + x) * 4];
			if (order == Order::BGRA) {
				p[0] = bl;
				p[1] = g;
				p[2] = r;
			} else {
				p[0] = r;
				p[1] = g;
				p[2] = bl;
			}
			p[3] = a;
		}
	}
}

void paint_strip(std::vector<unsigned char> &px, int w, int h, int frame, Order order)
{
	frame &= (1 << FRAME_BITS) - 1;
	for (int i = 0; i < STRIP_CELLS; ++i) {
		const unsigned char v = ((frame >> i) & 1) ? 255 : 0;
		fill(px, w, h, cell_box(w, h, i), v, v, v, 255, order);
	}
}

std::vector<unsigned char> make_pattern(int w, int h, int frame, int seed, const std::string &alpha, Order order)
{
	std::vector<unsigned char> px((size_t)w * h * 4, 0);
	const Regions r = regions(w, h);
	seed = ((seed % 6) + 6) % 6;
	const Rgb tl = HUE_TABLE[(0 + seed) % 6];
	const Rgb tr = HUE_TABLE[(2 + seed) % 6];
	const Rgb bl = HUE_TABLE[(4 + seed) % 6];
	fill(px, w, h, r.tl, tl.r, tl.g, tl.b, 255, order);
	fill(px, w, h, r.tr, tr.r, tr.g, tr.b, 255, order);
	fill(px, w, h, r.bl, bl.r, bl.g, bl.b, 255, order);
	fill(px, w, h, r.br, GREY, GREY, GREY, 255, order);
	if (alpha == "opaque")
		fill(px, w, h, r.centre, 255, 0, 0, 255, order);
	else if (alpha == "straight")
		fill(px, w, h, r.centre, 255, 0, 0, 128, order);
	else // premult
		fill(px, w, h, r.centre, 255 * 128 / 255, 0, 0, 128, order);
	paint_strip(px, w, h, frame, order);
	return px;
}

Box inner(const Box &b, double frac = 0.6)
{
	const int mx = (int)((b.x1 - b.x0) * (1 - frac) / 2);
	const int my = (int)((b.y1 - b.y0) * (1 - frac) / 2);
	return {b.x0 + mx, b.y0 + my, std::max(b.x0 + mx + 1, b.x1 - mx), std::max(b.y0 + my + 1, b.y1 - my)};
}

double mean_lum(const std::vector<unsigned char> &px, int w, const Box &b)
{
	double sum = 0;
	long n = 0;
	for (int y = b.y0; y < b.y1; ++y)
		for (int x = b.x0; x < b.x1; ++x) {
			const unsigned char *p = &px[(size_t)(y * w + x) * 4];
			sum += (p[0] + p[1] + p[2]) / 3.0;
			++n;
		}
	return n ? sum / n : 0.0;
}

// returns -1 when the strip is not clearly decodable
int decode_frame(const std::vector<unsigned char> &px, int w, int h, int margin = 80)
{
	int frame = 0;
	for (int i = 0; i < STRIP_CELLS; ++i) {
		const double lum = mean_lum(px, w, inner(cell_box(w, h, i)));
		if (lum > 255 - margin)
			frame |= 1 << i;
		else if (lum >= margin)
			return -1;
	}
	return frame;
}

// ----------------------------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------------------------
std::string json_escape(const std::string &s)
{
	std::string out;
	for (char c : s) {
		if (c == '"' || c == '\\')
			out += '\\';
		if (c == '\n')
			out += "\\n";
		else
			out += c;
	}
	return out;
}

void emit(const std::string &json)
{
	std::printf("%s\n", json.c_str());
	std::fflush(stdout);
}

struct Args {
	std::string cmd;
	std::string name;
	int w = 640, h = 360;
	double fps = 30.0;
	int seed = 0;
	std::string alpha = "opaque";
	std::string adapter;
	std::string format = "BGRA";
	double duration = 0.0;
	int frames = 30;
	double timeout = 5.0;
	std::string out;
	bool invert = false;
};

bool parse(int argc, char **argv, Args &a)
{
	if (argc < 2)
		return false;
	a.cmd = argv[1];
	for (int i = 2; i < argc; ++i) {
		std::string k = argv[i];
		auto next = [&](std::string &dst) {
			if (i + 1 >= argc)
				return false;
			dst = argv[++i];
			return true;
		};
		std::string v;
		if (k == "--name" && next(v))
			a.name = v;
		else if (k == "--size" && next(v)) {
			if (std::sscanf(v.c_str(), "%dx%d", &a.w, &a.h) != 2)
				return false;
		} else if (k == "--fps" && next(v))
			a.fps = std::atof(v.c_str());
		else if (k == "--seed" && next(v))
			a.seed = std::atoi(v.c_str());
		else if (k == "--alpha" && next(v))
			a.alpha = v;
		else if (k == "--adapter" && next(v))
			a.adapter = v;
		else if (k == "--format" && next(v))
			a.format = v;
		else if (k == "--duration" && next(v))
			a.duration = std::atof(v.c_str());
		else if (k == "--frames" && next(v))
			a.frames = std::atoi(v.c_str());
		else if (k == "--timeout" && next(v))
			a.timeout = std::atof(v.c_str());
		else if (k == "--out" && next(v))
			a.out = v;
		else if (k == "--invert")
			a.invert = true;
		else
			return false;
	}
	return true;
}

// Resolve "--adapter" (numeric index or case-insensitive substring of the adapter name).
int resolve_adapter(spoutDX &dx, const std::string &spec, std::string &name_out)
{
	const int n = dx.GetNumAdapters();
	char buf[256];
	if (!spec.empty() && std::all_of(spec.begin(), spec.end(), ::isdigit)) {
		const int idx = std::atoi(spec.c_str());
		if (idx >= 0 && idx < n) {
			if (dx.GetAdapterName(idx, buf, sizeof(buf)))
				name_out = buf;
			return idx;
		}
		return -1;
	}
	std::string want = spec;
	std::transform(want.begin(), want.end(), want.begin(), ::tolower);
	for (int i = 0; i < n; ++i) {
		if (!dx.GetAdapterName(i, buf, sizeof(buf)))
			continue;
		std::string have = buf;
		std::transform(have.begin(), have.end(), have.begin(), ::tolower);
		if (have.find(want) != std::string::npos) {
			name_out = buf;
			return i;
		}
	}
	return -1;
}

std::string adapter_json(spoutDX &dx)
{
	char buf[256] = {0};
	const int idx = dx.GetAdapter();
	dx.GetAdapterName(idx, buf, sizeof(buf));
	return "{\"index\":" + std::to_string(idx) + ",\"name\":\"" + json_escape(buf) + "\"}";
}

bool open_dx(spoutDX &dx, const Args &a, std::string &adapter_name)
{
	dx.SetMaxSenders(255);
	if (!a.adapter.empty()) {
		const int idx = resolve_adapter(dx, a.adapter, adapter_name);
		if (idx < 0) {
			emit("{\"event\":\"error\",\"message\":\"adapter not found: " + json_escape(a.adapter) + "\"}");
			return false;
		}
		dx.SetAdapterAuto(false);
		if (!dx.SetAdapter(idx)) {
			emit("{\"event\":\"error\",\"message\":\"SetAdapter failed for index " + std::to_string(idx) + "\"}");
			return false;
		}
	}
	if (!dx.OpenDirectX11()) {
		emit("{\"event\":\"error\",\"message\":\"OpenDirectX11 failed\"}");
		return false;
	}
	return true;
}

// ----------------------------------------------------------------------------------------------
// send
// ----------------------------------------------------------------------------------------------
int cmd_send(const Args &a)
{
	if (a.name.empty()) {
		emit("{\"event\":\"error\",\"message\":\"--name required\"}");
		return 2;
	}
	Order order = Order::BGRA;
	DXGI_FORMAT fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
	if (a.format == "RGBA") {
		order = Order::RGBA;
		fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
	} else if (a.format != "BGRA") {
		emit("{\"event\":\"error\",\"message\":\"--format must be BGRA or RGBA for SendImage\"}");
		return 2;
	}

	spoutDX dx;
	std::string adapter_name;
	if (!open_dx(dx, a, adapter_name))
		return 2;
	dx.SetSenderFormat(fmt);
	dx.SetSenderName(a.name.c_str());

	std::atomic<bool> stop{false};
	std::thread watcher([&stop] {
		char line[64];
		while (std::fgets(line, sizeof(line), stdin)) {
			if (line[0] == 'q' || line[0] == 'Q' || line[0] == '\n')
				break;
		}
		stop = true;
	});
	watcher.detach();

	std::vector<unsigned char> px = make_pattern(a.w, a.h, 0, a.seed, a.alpha, order);
	int frame = 0;
	long sent = 0;
	bool started = false;
	const auto t_end = std::chrono::steady_clock::now() + std::chrono::milliseconds((long long)(a.duration * 1000));
	while (!stop) {
		if (a.duration > 0 && std::chrono::steady_clock::now() >= t_end)
			break;
		paint_strip(px, a.w, a.h, frame, order);
		if (!dx.SendImage(px.data(), (unsigned)a.w, (unsigned)a.h)) {
			emit("{\"event\":\"error\",\"message\":\"SendImage failed\",\"frame\":" + std::to_string(frame) + "}");
			dx.ReleaseSender();
			dx.CloseDirectX11();
			return 2;
		}
		++sent;
		frame = (frame + 1) & 0xFFFF;
		if (!started) {
			started = true;
			emit("{\"event\":\"started\",\"backend\":\"spout-tool\",\"name\":\"" + json_escape(a.name) +
			     "\",\"width\":" + std::to_string(a.w) + ",\"height\":" + std::to_string(a.h) +
			     ",\"fps\":" + std::to_string(a.fps) + ",\"seed\":" + std::to_string(a.seed) +
			     ",\"alpha\":\"" + a.alpha + "\",\"format\":\"" + a.format + "\",\"adapter\":" + adapter_json(dx) +
			     ",\"pid\":" + std::to_string(GetCurrentProcessId()) + "}");
		}
		if (a.fps > 0)
			dx.HoldFps((int)a.fps);
	}
	const long spout_frame = dx.GetFrame();
	dx.ReleaseSender();
	dx.CloseDirectX11();
	emit("{\"event\":\"stopped\",\"backend\":\"spout-tool\",\"name\":\"" + json_escape(a.name) +
	     "\",\"frames_sent\":" + std::to_string(sent) + ",\"last_frame\":" + std::to_string(frame) +
	     ",\"spout_frame\":" + std::to_string(spout_frame) + "}");
	return 0;
}

// ----------------------------------------------------------------------------------------------
// recv
// ----------------------------------------------------------------------------------------------
unsigned long adler32(const unsigned char *data, size_t len)
{
	unsigned long a = 1, b = 0;
	for (size_t i = 0; i < len; ++i) {
		a = (a + data[i]) % 65521;
		b = (b + a) % 65521;
	}
	return (b << 16) | a;
}

int cmd_recv(const Args &a)
{
	if (a.name.empty()) {
		emit("{\"event\":\"error\",\"message\":\"--name required\"}");
		return 2;
	}
	spoutDX dx;
	std::string adapter_name;
	if (!open_dx(dx, a, adapter_name))
		return 2;
	dx.SetReceiverName(a.name.c_str());

	const auto t0 = std::chrono::steady_clock::now();
	const auto deadline = t0 + std::chrono::milliseconds((long long)(a.timeout * 1000));
	std::vector<unsigned char> buf;
	unsigned w = 0, h = 0;
	DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
	long reads = 0, distinct = 0;
	unsigned long last_digest = 0;
	bool have_digest = false;
	long frame_first = -1, frame_last = 0;
	int pattern_first = -2, pattern_last = -2;
	std::vector<unsigned char> last_img;

	while (std::chrono::steady_clock::now() < deadline && distinct < a.frames) {
		// ReceiveImage needs a buffer of the sender size; IsUpdated() tells us when that changes.
		if (buf.empty())
			dx.ReceiveImage(nullptr, 0, 0, false, a.invert);
		else
			dx.ReceiveImage(buf.data(), w, h, false, a.invert);
		if (dx.IsUpdated()) {
			w = dx.GetSenderWidth();
			h = dx.GetSenderHeight();
			fmt = dx.GetSenderFormat();
			buf.assign((size_t)w * h * 4, 0);
			continue;
		}
		if (!buf.empty() && dx.IsConnected() && dx.IsFrameNew()) {
			++reads;
			const unsigned long d = adler32(buf.data(), buf.size());
			if (!have_digest || d != last_digest) {
				have_digest = true;
				last_digest = d;
				++distinct;
				last_img = buf;
				const long f = dx.GetSenderFrame();
				frame_last = f;
				if (frame_first < 0)
					frame_first = f;
				const int pf = decode_frame(buf, (int)w, (int)h);
				pattern_last = pf;
				if (pattern_first == -2)
					pattern_first = pf;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(4));
	}
	const bool connected = dx.IsConnected() && w > 0 && reads > 0;
	const double fps = dx.GetSenderFps();
	const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::string out_json = "null";
	if (!a.out.empty() && !last_img.empty()) {
		FILE *f = std::fopen(a.out.c_str(), "wb");
		if (f) {
			std::fwrite(last_img.data(), 1, last_img.size(), f);
			std::fclose(f);
			const std::string order = (fmt == DXGI_FORMAT_R8G8B8A8_UNORM) ? "rgba" : "bgra";
			FILE *j = std::fopen((a.out + ".json").c_str(), "wb");
			if (j) {
				std::fprintf(j, "{\"width\":%u,\"height\":%u,\"order\":\"%s\",\"format\":%d}", w, h, order.c_str(), (int)fmt);
				std::fclose(j);
			}
			out_json = "\"" + json_escape(a.out) + "\"";
		}
	}
	dx.ReleaseReceiver();
	dx.CloseDirectX11();

	auto pat = [](int v) { return v < 0 ? std::string("null") : std::to_string(v); };
	emit("{\"backend\":\"spout-tool\",\"name\":\"" + json_escape(a.name) + "\",\"sender\":" +
	     (connected ? "\"" + json_escape(a.name) + "\"" : "null") + ",\"connected\":" + (connected ? "true" : "false") +
	     ",\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) + ",\"format\":" + std::to_string((int)fmt) +
	     ",\"frame_first\":" + std::to_string(frame_first < 0 ? 0 : frame_first) + ",\"frame_last\":" + std::to_string(frame_last) +
	     ",\"fps\":" + std::to_string(fps) + ",\"new_frames\":" + std::to_string(reads) + ",\"distinct_frames\":" +
	     std::to_string(distinct) + ",\"pattern_first\":" + pat(pattern_first) + ",\"pattern_last\":" + pat(pattern_last) +
	     ",\"elapsed\":" + std::to_string(elapsed) + ",\"out\":" + out_json + ",\"adapter\":" + adapter_json(dx) + "}");
	return connected ? 0 : 1;
}

// ----------------------------------------------------------------------------------------------
// list
// ----------------------------------------------------------------------------------------------
int cmd_list(const Args &)
{
	spoutDX dx;
	dx.SetMaxSenders(255);
	std::string json = "{\"backend\":\"spout-tool\",\"senders\":[";
	const int n = dx.GetSenderCount();
	for (int i = 0; i < n; ++i) {
		char name[256] = {0};
		if (!dx.GetSender(i, name, sizeof(name)))
			continue;
		unsigned w = 0, h = 0;
		HANDLE handle = nullptr;
		DWORD fmt = 0;
		dx.GetSenderInfo(name, w, h, handle, fmt);
		char adapter[256] = {0};
		const int ad = dx.GetSenderAdapter(name, adapter, sizeof(adapter));
		if (i)
			json += ",";
		json += "{\"name\":\"" + json_escape(name) + "\",\"width\":" + std::to_string(w) + ",\"height\":" +
			std::to_string(h) + ",\"format\":" + std::to_string(fmt) + ",\"adapter\":{\"index\":" + std::to_string(ad) +
			",\"name\":\"" + json_escape(adapter) + "\"}}";
	}
	json += "]}";
	emit(json);
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	Args a;
	if (!parse(argc, argv, a)) {
		std::fprintf(stderr, "usage: spout-tool send|recv|list [options]\n");
		return 2;
	}
	if (a.cmd == "send")
		return cmd_send(a);
	if (a.cmd == "recv")
		return cmd_recv(a);
	if (a.cmd == "list")
		return cmd_list(a);
	std::fprintf(stderr, "unknown command '%s'\n", a.cmd.c_str());
	return 2;
}
