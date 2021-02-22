#ifndef UI_H
#define UI_H

#include "sdl.h"

#include <cstdint>
#include <cctype>
#include <string>
#include <algorithm>
#include <variant>
#include <functional>
#include <map>
#include <vector>
#include <stack>
#include <optional>
#include <regex>

struct Rect {
	int x{ 0 }, y{ 0 }, width{ 0 }, height{ 0 };

	void pad(int left, int top, int right, int bottom) {
		x += left;
		y += top;
		width -= (left + right);
		height -= (top + bottom);
	}

	bool has(int px, int py) const {
		return px >= x &&
				px <= x + width &&
				py >= y &&
				py <= y + height;
	}

	bool valid() const {
		return width * height > 0;
	}

	Rect() = default;
	Rect(const Rect& o) : x(o.x), y(o.y), width(o.width), height(o.height) {}
	Rect(int x, int y, int width, int height) : x(x), y(y), width(width), height(height) {}
};

struct Color {
	uint8_t r, g, b;
};

class Device {
public:
	Device(SDL_Window* window, SDL_Renderer* renderer) : m_window(window), m_renderer(renderer) {
		SDL_StartTextInput();
	}

	~Device() {
		SDL_DestroyTexture(m_theme);
	}

	/**
	 * @brief  Loads a skin texture
	 * @note   Must be in BMP format
	 * @param  path: Image path
	 * @retval None
	 */
	void loadSkin(const std::string& path) {
		if (m_theme) {
			SDL_DestroyTexture(m_theme);
		}
		m_charOffsets.clear();
		m_charAdvances.clear();

		SDL_Surface* surf = SDL_LoadBMP(path.c_str());
		surf = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGB24, 0);

		// Read char offsets (blue dots)
		SDL_LockSurface(surf);
		uint8_t* pixels = (uint8_t*)surf->pixels;

		const int cellW = surf->w / 16;
		const int cellH = surf->h / 16;
		for (int ty = 0; ty < 16; ty++) {
			for (int tx = 0; tx < 16; tx++) {
				int tpx = tx * cellW,
					tpy = ty * cellH;
				
				int fx = 0, fy = 0, ax = cellW;
				for (int oy = 0; oy < cellH; oy++) {
					for (int ox = 0; ox < cellW; ox++) {
						int i = ((ox + tpx) + (oy + tpy) * surf->w) * 3;
						if (pixels[i + 2] == 255 && pixels[i + 1] == 0 && pixels[i] == 0) {
							pixels[i + 0] = 255;
							fx = ox; fy = cellH - oy;
						} else if (pixels[i + 2] == 0 && pixels[i + 1] == 255 && pixels[i] == 0) {
							pixels[i + 0] = 255;
							pixels[i + 1] = 0;
							pixels[i + 2] = 255;
							ax = ox;
						}
					}
				}
				m_charOffsets[tx + ty * 16] = std::make_pair(fx, fy);
				m_charAdvances[tx + ty * 16] = ax;
			}
		}

		SDL_UnlockSurface(surf);

		SDL_SetColorKey(surf, 1, SDL_MapRGB(surf->format, 255, 0, 255));
		m_theme = SDL_CreateTextureFromSurface(m_renderer, surf);
		SDL_FreeSurface(surf);
		SDL_QueryTexture(m_theme, nullptr, nullptr, &m_themeWidth, &m_themeHeight);
	}

	int textWidth(const std::string& str) {
		return str.size() * ((m_themeWidth / 16) + m_charSpacingX);
	}

	void debugRect(int x, int y, int w, int h) {
		m_commands.push_back(Command{
			.type = Command::CmdDebug,
			.glyph = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
			.clip = { x, y, w, h },
			.order = m_currentOrder++
		});
	}

	int drawChar(char c, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
		const int cellW = m_themeWidth / 16;
		const int cellH = m_themeHeight / 16;

		c &= 0xFF;

		int sx = (int(c) % 16) * cellW;
		int sy = (int(c) / 16) * cellH;

		auto off = m_charOffsets[int(c)];

		int cx = x - std::get<0>(off);
		int cy = y + std::get<1>(off);

		m_commands.push_back(Command{
			.type = Command::CmdDraw,
			.glyph = { cx, cy, cellW, cellH, sx, sy, cellW, cellH, r, g, b },
			.clip = { 0, 0, 0, 0 },
			.order = m_currentOrder++
		});

		return m_charAdvances[int(c)] - std::get<0>(off)/*  + m_charSpacingX */;
	}

	void drawTileSection(int index, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, int rx, int ry, int rw, int rh) {
		const int cellW = m_themeWidth / 16;
		const int cellH = m_themeHeight / 16;

		index &= 0xFF;

		rx = rx > cellW ? cellW : rx;
		rx = rx < 0 ? cellW+rx : rx;
		ry = ry > cellH ? cellH : ry;
		ry = ry < 0 ? cellH+ry : ry;
		rw = rw < 0 ? cellW+rw : rw;
		rh = rh < 0 ? cellH+rh : rh;
		rw = std::clamp(rw, 0, cellW);
		rh = std::clamp(rh, 0, cellH);

		int sx = (int(index) % 16) * cellW;
		int sy = (int(index) / 16) * cellH;

		m_commands.push_back(Command{
			.type = Command::CmdDraw,
			.glyph = { x, y, w, h, sx + rx, sy + ry, rw, rh, r, g, b },
			.clip = { 0, 0, 0, 0 },
			.order = m_currentOrder++
		});
	}

	void drawText(const std::string& str, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
		int tx = 0, ty = 0;
		for (char c : str) {
			if (c == '\n') {
				tx = 0;
				ty += (m_themeHeight / 16) + m_charSpacingY;
			} else if (isspace(c) && c != '\n') {
				tx += (m_themeWidth / 16) + m_charSpacingX;
			} else {
				tx += drawChar(c, tx + x, ty + y, r, g, b);
			}
		}
	}

	void drawPatch(int index, int x, int y, int w, int h, uint8_t r = 0xFF, uint8_t g = 0xFF, uint8_t b = 0xFF) {
		const int p = m_patchPadding;

		// corners
		drawTileSection(index,  x, y, p, p,                  r, g, b,   0,  0, p, p);
		drawTileSection(index,  x + w - p, y, p, p,          r, g, b,  -p,  0, p, p);
		drawTileSection(index,  x, y + h - p, p, p,          r, g, b,   0, -p, p, p);
		drawTileSection(index,  x + w - p, y + h - p, p, p,  r, g, b,  -p, -p, p, p);

		// beams
		drawTileSection(index,  x + p, y, w - p*2, p,           r, g, b,    p,  0, -p*2, p);
		drawTileSection(index,  x + p, y + h - p, w - p*2, p,   r, g, b,    p, -p, -p*2, p);
		drawTileSection(index,  x, y + p, p, h - p*2,           r, g, b,    0,  p,  p,  -p*2);
		drawTileSection(index,  x + w - p, y + p, p, h - p*2,   r, g, b,   -p,  p,  p,  -p*2);

		//middle
		drawTileSection(index,  x + p, y + p, w - p*2, h - p*2,  r, g, b,  p, p, -p*2, -p*2);
	}

	void drawBalloon(int x, int y, int width, int height) {
		drawPatch(8, x - width / 2, y + 4, width, height);
		drawPatch(9, x - cellWidth() / 2, y - (cellHeight() - 4), cellWidth(), cellHeight());
	}

	void clip(int x, int y, int w, int h) {
		m_commands.push_back(Command{
			.type = Command::CmdClip,
			.glyph = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
			.clip = { x, y, w, h },
			.order = m_currentOrder++
		});
	}

	void unclip() {
		m_commands.push_back(Command{
			.type = Command::CmdUnClip,
			.glyph = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
			.clip = { 0, 0, 0, 0 },
			.order = m_currentOrder++
		});
	}

	void flush() {
		SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
		SDL_RenderClear(m_renderer);

		std::sort(m_commands.begin(), m_commands.end(), [&](const Command& a, const Command& b){ return a.order < b.order; });

		for (auto& cmd : m_commands) {
			switch (cmd.type) {
				case Command::CmdDraw: {
					SDL_Rect src = { cmd.glyph.rx, cmd.glyph.ry, cmd.glyph.rw, cmd.glyph.rh };
					SDL_Rect dst = { cmd.glyph.x, cmd.glyph.y, cmd.glyph.w, cmd.glyph.h };
					SDL_SetTextureColorMod(m_theme, cmd.glyph.r, cmd.glyph.g, cmd.glyph.b);
					SDL_RenderCopy(m_renderer, m_theme, &src, &dst);
				} break;
				case Command::CmdClip: {
					clipPush(cmd.clip.x, cmd.clip.y, cmd.clip.w, cmd.clip.h);
				} break;
				case Command::CmdUnClip: {
					clipPop();
				} break;
				case Command::CmdDebug: {
					SDL_Rect r = { cmd.clip.x, cmd.clip.y, cmd.clip.w, cmd.clip.h };
					SDL_SetRenderDrawColor(m_renderer, 0, 255, 100, 255);
					SDL_RenderDrawRect(m_renderer, &r);
				} break;
			}
		}

		m_commands.clear();
		while (!m_orderStack.empty()) m_orderStack.pop();
		m_currentOrder = 0;
	}

	void pushOrder(int base) {
		m_orderStack.push(m_currentOrder);
		m_currentOrder = base;
	}

	void popOrder() {
		if (m_orderStack.empty()) return;
		m_currentOrder = m_orderStack.top();
		m_orderStack.pop();
	}

	int charSpacingX() const { return m_charSpacingX; }
	void charSpacingX(int charSpacingX) { m_charSpacingX = charSpacingX; }

	int charSpacingY() const { return m_charSpacingY; }
	void charSpacingY(int charSpacingY) { m_charSpacingY = charSpacingY; }

	int patchPadding() const { return m_patchPadding; }
	void patchPadding(int patchPadding) { m_patchPadding = patchPadding; }

	const int& themeWidth() const { return m_themeWidth; }
	const int& themeHeight() const { return m_themeHeight; }

	int cellWidth() const { return m_themeWidth / 16; }
	int cellHeight() const { return m_themeHeight / 16; }

	std::pair<int, int> size() const {
		int w, h;
		SDL_GetWindowSize(m_window, &w, &h);
		return std::make_pair(w, h);
	}

private:
	struct Command {
		enum {
			CmdDraw = 0,
			CmdClip,
			CmdUnClip,
			CmdDebug
		} type;

		struct {
			int x, y, w, h, rx, ry, rw, rh;
			uint8_t r, g, b;
		} glyph;

		struct {
			int x, y, w, h;
		} clip;

		int order{ 0 };
	};

	std::vector<Command> m_commands;
	std::stack<int> m_orderStack;
	std::stack<Rect> m_clips;
	std::map<uint8_t, std::pair<int, int>> m_charOffsets;
	std::map<uint8_t, int> m_charAdvances;
	int m_currentOrder{ 0 };

	SDL_Renderer* m_renderer;
	SDL_Window* m_window;

	SDL_Texture* m_theme{ nullptr };
	int m_themeWidth, m_themeHeight;

	int m_charSpacingX{ -4 }, m_charSpacingY{ -2 }, m_patchPadding{ 5 };
	
	void clipPush(int x, int y, int w, int h) {
		// if (!m_clips.empty()) {
		// 	Rect b = m_clips.top();
		// 	int minx = std::min(b.x, x);
		// 	int maxx = std::max(b.x + b.width, x + w);
		// 	if (maxx - minx < 1) {
		// 		return;
		// 	}

		// 	int miny = std::min(b.y, y);
		// 	int maxy = std::min(b.y + b.height, y + h);
		// 	if (maxy - miny < 1) {
		// 		return;
		// 	}

		// 	x = minx;
		// 	y = miny;
		// 	w = maxx - minx;
		// 	h = maxy - miny;
		// }

		SDL_Rect rec = { x, y, w, h };
		SDL_RenderSetClipRect(m_renderer, &rec);
		m_clips.push(Rect(x, y, w, h));
	}

	void clipPop() {
		if (!m_clips.empty()) m_clips.pop();

		if (!m_clips.empty()) {
			Rect b = m_clips.top();
			SDL_Rect rec = { b.x, b.y, b.width, b.height };
			SDL_RenderSetClipRect(m_renderer, &rec);
		} else {
			SDL_RenderSetClipRect(m_renderer, nullptr);
		}
	}

};

enum Alignment {
	Near = 0,
	Center,
	Far
};

struct MouseEvent {
	enum {
		MouseEventDown,
		MouseEventUp,
		MouseEventMove
	} type{ MouseEventMove };
	int x{ 0 }, y{ 0 }, button{ 0 };
};

struct KeyboardEvent {
	enum {
		KeyEventDown,
		KeyEventType,
		KeyEventCommand
	} type{ KeyEventDown };
	uint32_t key{ 0 };
	char input{ 0 };
};

struct Context {
	Rect bounds{};
};

using WID = uint32_t;

struct Text {
	std::string text{};
	Alignment align{ Alignment::Near };
	Color color{ .r = 255, .g = 255, .b = 255 };
};

enum ButtonState {
	ButtonStateNormal = 0,
	ButtonStateHover,
	ButtonStatePressed
};

struct Button {
	std::string text{};
	std::function<void()> onPressed;
	bool disabled{ false };

	ButtonState state{ ButtonStateNormal };
};

struct Root {
	WID child;
};

struct Container {
	int width{ 0 }, height{ 0 };
	WID child;
	int padding{ 5 };
	bool background{ false };
};

struct Layout {
	WID top, bottom, left, right, center;
};

struct Column {
	std::vector<WID> children;
	Alignment alignment{ Alignment::Center };
	int spacing{ 3 };
};

struct Placement {
	float x{ 0.0f }, y{ 0.0f };
	WID child;
};

struct Slider {
	int min{ 0 }, max{ 100 };
	int value{ 0 };
	bool disabled{ false };
	std::function<void(int)> onChange;

	ButtonState __state{ ButtonState::ButtonStateNormal };
};

struct Input {
	std::string text{};
	std::string pattern{ ".*" };
	bool masked{ false }, disabled{ false };

	int __cursor{ 0 }, __viewx{ 0 };
};

using Widget = std::variant<
	Root, Container, Layout, Column, Placement,
	Text, Button, Slider, Input
>;

#define UI_DECLARE_WIDGET(T) \
	template<> \
	void draw<T>(Device& dev, WID wid, T& w, const Context& ctx, UISystem* sys); \
	template<> \
	Rect bounds<T>(Device& dev, WID wid, T& w, const Context& ctx, UISystem* sys); \
	template<> \
	bool onMouseEvent<T>(Device& dev, const MouseEvent& e, WID wid, T& w, const Context& ctx, UISystem* sys); \
	template<> \
	void onKeyEvent(Device& dev, const KeyboardEvent& e, WID wid, T& w, UISystem* sys) {}

#define UI_DECLARE_WIDGET_KB(T) \
	template<> \
	void draw<T>(Device& dev, WID wid, T& w, const Context& ctx, UISystem* sys); \
	template<> \
	Rect bounds<T>(Device& dev, WID wid, T& w, const Context& ctx, UISystem* sys); \
	template<> \
	bool onMouseEvent<T>(Device& dev, const MouseEvent& e, WID wid, T& w, const Context& ctx, UISystem* sys); \
	template<> \
	void onKeyEvent(Device& dev, const KeyboardEvent& e, WID wid, T& w, UISystem* sys);

#define UI_WIDGET_DRAW_IMPL(T) \
	template<> \
	void internal::draw<T>(Device& dev, WID wid, T& w, const Context& ctx, UISystem* sys)

#define UI_WIDGET_BOUNDS_IMPL(T) \
	template<> \
	Rect internal::bounds<T>(Device& dev, WID wid, T& w, const Context& ctx, UISystem* sys)

#define UI_WIDGET_MOUSE_EVENT_IMPL(T) \
	template<> \
	bool internal::onMouseEvent<T>(Device& dev, const MouseEvent& e, WID wid, T& w, const Context& ctx, UISystem* sys)

#define UI_WIDGET_KEY_EVENT_IMPL(T) \
	template<> \
	void internal::onKeyEvent(Device& dev, const KeyboardEvent& e, WID wid, T& w, UISystem* sys)

class UISystem;
namespace internal {
	
	template<typename W>
	bool onMouseEvent(Device& dev, const MouseEvent& e, WID wid, W& w, const Context& ctx, UISystem* sys) { return false; }

	template<typename W>
	void onKeyEvent(Device& dev, const KeyboardEvent& e, WID wid, W& w, UISystem* sys) {}

	template<typename W>
	void draw(Device& dev, WID wid, W& w, const Context& ctx, UISystem* sys) { }

	template<typename W>
	Rect bounds(Device& dev, WID wid, W& w, const Context& ctx, UISystem* sys) { return Rect(0, 0, 1, 1); }

	UI_DECLARE_WIDGET(Root)
	UI_DECLARE_WIDGET(Text)
	UI_DECLARE_WIDGET(Button)
	UI_DECLARE_WIDGET(Placement)
	UI_DECLARE_WIDGET(Container)
	UI_DECLARE_WIDGET(Column)
	UI_DECLARE_WIDGET(Slider)
	UI_DECLARE_WIDGET_KB(Input)
	UI_DECLARE_WIDGET(Layout)

};

class UISystem {
public:

	template<typename W>
	WID create(const W& w) {
		WID id = m_current++;
		m_widgets[id] = w;
		return id;
	}

	template<typename W>
	std::optional<W&> get(WID id) {
		if (m_widgets.find(id) == m_widgets.end()) return {};
		return std::get<W>(m_widgets[id]);
	}

	void draw(Device& dev, WID id, const Context& ctx) {
		std::vector<int> ids;
		for (const auto& [key, _] : m_widgets) {
			ids.push_back(key);
		}
		if (id == *std::max_element(ids.begin(), ids.end())) {
			bounds(dev, id, ctx);
		}
		auto& wid = m_widgets[id];
		std::visit([&](auto&& w) { internal::draw(dev, id, w, ctx, this); }, wid);
	}

	void bounds(Device& dev, WID id, const Context& ctx) {
		auto& wid = m_widgets[id];
		m_widgetBounds[id] = std::visit([&](auto&& w) { return internal::bounds(dev, id, w, ctx, this); }, wid);
	}

	bool processMouse(Device& dev, const MouseEvent& e, WID id, const Context& ctx) {
		auto& wid = m_widgets[id];
		return std::visit([&](auto&& w) { return internal::onMouseEvent(dev, e, id, w, ctx, this); }, wid);
	}

	void processKeyboard(Device& dev, const KeyboardEvent& e, WID id) {
		if (id == 0) return;
		auto& wid = m_widgets[id];
		std::visit([&](auto&& w) { internal::onKeyEvent(dev, e, id, w, this); }, wid);
	}

	void processEvents(Device& dev, const SDL_Event& e, WID id) {
		Context ctx{};

		switch (e.type) {
			case SDL_MOUSEBUTTONDOWN: processMouse(dev, MouseEvent{ .type = MouseEvent::MouseEventDown, .x = e.button.x, .y = e.button.y, .button = e.button.button }, id, ctx); break;
			case SDL_MOUSEBUTTONUP: processMouse(dev, MouseEvent{ .type = MouseEvent::MouseEventUp, .x = e.button.x, .y = e.button.y, .button = e.button.button }, id, ctx); break;
			case SDL_MOUSEMOTION: processMouse(dev, MouseEvent{ .type = MouseEvent::MouseEventMove, .x = e.motion.x, .y = e.motion.y }, id, ctx); break;
			case SDL_KEYDOWN: {
				if (SDL_GetModState() & KMOD_CTRL) {
					processKeyboard(dev, KeyboardEvent{
						.type = KeyboardEvent::KeyEventCommand,
						.key = uint32_t(e.key.keysym.sym)
					}, focused);
				} else {
					processKeyboard(dev, KeyboardEvent{
						.type = KeyboardEvent::KeyEventDown,
						.key = uint32_t(e.key.keysym.sym)
					}, focused);
				}
			} break;
			case SDL_TEXTINPUT: {
				if (!(SDL_GetModState() & KMOD_CTRL)) {
					processKeyboard(dev, KeyboardEvent{
						.type = KeyboardEvent::KeyEventType,
						.key = 0,
						.input = e.text.text[0]
					}, focused);
				}
			} break;
		}
	}

	const Rect& bounds(WID id) { return m_widgetBounds[id]; }
	void updateBounds(WID id, const Rect& r) { m_widgetBounds[id] = r; }

	WID focused{ 0 };

private:
	WID m_current{ 1 };
	std::map<WID, Widget> m_widgets;
	std::map<WID, Rect> m_widgetBounds;

};

constexpr int SliderHeight = 16;
constexpr int SliderThumbWidth = 16;
constexpr int InputHeight = 22;

enum class UI_LayoutSide {
	Top = 0,
	Bottom,
	Left,
	Right,
	Center
};

struct Bounds {
	int left, right, bottom, top;
};

static Rect calculateBounds(Bounds& b, WID wid, UI_LayoutSide side, UISystem* sys) {
	const int spacing = -1;
	Rect wb = sys->bounds(wid);
	Rect nb(wb);

	int& left = b.left;
	int& right = b.right;
	int& bottom = b.bottom;
	int& top = b.top;

	switch (side) {
		case UI_LayoutSide::Top: {
			nb.x = left; nb.y = top;
			nb.width = right - left;
			top += (wb.height + spacing);
		} break;
		case UI_LayoutSide::Bottom: {
			nb.x = left; nb.y = (bottom - wb.height);
			nb.width = right - left;
			bottom -= (wb.height + spacing);
		} break;
		case UI_LayoutSide::Left: {
			nb.x = left; nb.y = top;
			nb.height = bottom - top;
			left += wb.width + spacing;
		} break;
		case UI_LayoutSide::Right: {
			nb.x = (right - wb.width); nb.y = top;
			nb.height = bottom - top;
			right -= wb.width + spacing;
		} break;
		case UI_LayoutSide::Center: {
			nb.x = left; nb.y = top;
			nb.width = right - left;
			nb.height = bottom - top;
		} break;
	}
	return nb;
}

UI_WIDGET_DRAW_IMPL(Layout) {
	WID cids[] = { w.top, w.bottom, w.left, w.right, w.center };
	for (int i = 0; i < 5; i++) {
		if (cids[i]) sys->draw(dev, cids[i], Context{ .bounds = sys->bounds(cids[i]) });
	}
}

UI_WIDGET_BOUNDS_IMPL(Layout) {
	Rect b = ctx.bounds;
	Bounds bds{ .left = b.x, .right = b.x + b.width, .bottom = b.y + b.height, .top = b.y };
	WID cids[] = { w.top, w.bottom, w.left, w.right, w.center };
	for (int i = 0; i < 5; i++) {
		if (cids[i]) {
			sys->bounds(dev, cids[i], ctx);
			sys->bounds(dev, cids[i], Context{ .bounds = calculateBounds(bds, cids[i], UI_LayoutSide(i), sys) });
		}
	}
	return ctx.bounds;
}

UI_WIDGET_MOUSE_EVENT_IMPL(Layout) {
	WID cids[] = { w.top, w.bottom, w.left, w.right, w.center };
	for (int i = 0; i < 5; i++) {
		if (cids[i] && sys->processMouse(dev, e, cids[i], Context{ .bounds = sys->bounds(cids[i]) })) {
			return true;
		}
	}
	return false;
}

UI_WIDGET_DRAW_IMPL(Input) {
	Rect pb = sys->bounds(wid);
	if (w.disabled) {
		dev.drawPatch(3, pb.x, pb.y, pb.width, pb.height);
	} else {
		dev.drawPatch(sys->focused == wid ? 5 : 4, pb.x, pb.y, pb.width, pb.height);
	}

	int& vx = w.__viewx;
	int cursorX = (w.__cursor * (dev.cellWidth() + dev.charSpacingX())) - dev.cellWidth() / 2;

	uint8_t shade = w.disabled ? 37 : 255;
	std::string text = w.masked ? std::string(w.text.size(), '*') : w.text;

	Rect tb(pb);
	tb.pad(4, 2, 4, 2);

	dev.clip(tb.x, tb.y, tb.width, tb.height);
	dev.drawText(
		text,
		pb.x - vx,
		pb.y + (pb.height / 2 - dev.cellHeight() / 2),
		shade, shade, shade
	);
	dev.unclip();

	if (!w.disabled && sys->focused == wid) {
		dev.drawText("|", (pb.x + cursorX) - vx, pb.y + (pb.height / 2 - dev.cellHeight() / 2), 255, 255, 255);
	}

	// dev.debugRect(tb.x, tb.y, tb.width, tb.height);
}

UI_WIDGET_BOUNDS_IMPL(Input) {
	return Rect(ctx.bounds.x, ctx.bounds.y, ctx.bounds.width, InputHeight);
}

static void updateView(WID wid, Input& w, Device& dev, UISystem* sys) {
	Rect pb = sys->bounds(wid);
	auto& cx = w.__cursor;
	auto& vx = w.__viewx;
	const int margin = dev.cellWidth();
	int cursorX = (cx * (dev.cellWidth() + dev.charSpacingX())) - dev.cellWidth() / 2;
	if (cursorX-vx > pb.width-margin) vx = cursorX - (pb.width-margin);
	else if (cursorX-vx < 0) vx = cursorX;
}

UI_WIDGET_KEY_EVENT_IMPL(Input) {
	if (w.disabled) return;

	Rect pb = sys->bounds(wid);

	auto& cx = w.__cursor;
	auto& vx = w.__viewx;
	const int margin = dev.cellWidth();

	if (e.type == KeyboardEvent::KeyEventType) {
		if (std::regex_match(std::to_string(e.input), std::regex(w.pattern))) {
			w.text.insert(cx++, 1, e.input);
			updateView(wid, w, dev, sys);
		}
	} else if (e.type == KeyboardEvent::KeyEventDown) {
		switch (e.key) {
			default: break;
			case SDLK_LEFT: {
				if (cx > 0) cx--;
			} break;
			case SDLK_RIGHT: {
				if (cx < w.text.size()) cx++;
			} break;
			case SDLK_DELETE: if (w.text.substr(cx).size() > 0) w.text.erase(cx, 1); break;
			case SDLK_BACKSPACE: if (w.text.substr(0, cx).size() > 0) w.text.erase(--cx, 1); break;
			case SDLK_HOME: cx = 0; break;
			case SDLK_END: cx = w.text.size(); break;
		}
		updateView(wid, w, dev, sys);
	} else if (e.type == KeyboardEvent::KeyEventCommand) {
		switch (e.key) {
			default: break;
			case SDLK_c: break; // TODO: Implement the copy command, or atleast try to...
			case SDLK_v: {
				w.text.insert(cx, std::string(SDL_GetClipboardText()));
			} break;
		}
		updateView(wid, w, dev, sys);
	}
}

UI_WIDGET_DRAW_IMPL(Slider) {
	std::string txt = std::to_string(w.value);
	int textWidth = dev.textWidth(txt) + 12;

	Rect pb = sys->bounds(wid);
	Rect track(pb.x + SliderThumbWidth / 2, pb.y, pb.width - SliderThumbWidth, SliderHeight);
	
	float ratio = float(w.value - w.min) / (w.max - w.min);
	int vx = ratio * track.width;
	Rect tb(pb.x + vx, pb.y, SliderThumbWidth, SliderHeight);

	dev.drawPatch(w.disabled ? 3 : 4, pb.x, pb.y, pb.width, pb.height);
	dev.drawPatch(w.disabled ? 3 : 0, tb.x, tb.y, tb.width, tb.height);
	if (w.__state == ButtonState::ButtonStatePressed) {
		Rect bb(tb.x + SliderThumbWidth / 2, tb.y + SliderHeight + 1, textWidth, dev.cellHeight() + 2);
		
		dev.pushOrder(99999);
			dev.drawBalloon(bb.x, bb.y, bb.width, bb.height);
			dev.drawText(
				txt,
				bb.x - (dev.textWidth(txt) / 2 + 2),
				(bb.y + 4) + (bb.height / 2 - dev.cellHeight() / 2),
				255, 255, 255
			);
		dev.popOrder();
	}
}

UI_WIDGET_BOUNDS_IMPL(Slider) {
	Rect pb = ctx.bounds;
	return Rect(pb.x, pb.y, pb.width, SliderHeight);
}

UI_WIDGET_DRAW_IMPL(Text) {
	Rect pb = ctx.bounds;

	int x = 0;
	switch (w.align) {
		default: break;
		case Alignment::Center: x = pb.width / 2 - dev.textWidth(w.text) / 2; break;
		case Alignment::Far: x = pb.width - dev.textWidth(w.text); break;
	}

	dev.drawText(w.text, x + pb.x, (pb.height / 2 - dev.cellHeight() / 2) + pb.y, w.color.r, w.color.g, w.color.b);
}

UI_WIDGET_BOUNDS_IMPL(Text) {
	Rect pb = ctx.bounds;
	return Rect(pb.x, pb.y, dev.textWidth(w.text), dev.cellHeight());
}

UI_WIDGET_DRAW_IMPL(Button) {
	Rect pb = sys->bounds(wid);
	Rect tb(pb);
	tb.pad(10, 5, 10, 5);

	const int s = w.disabled ? 3 : w.state;
	uint8_t shade = w.disabled ? 37 : 255;
	dev.drawPatch(s, pb.x, pb.y, pb.width, pb.height);

	if (w.state == ButtonState::ButtonStatePressed) tb.y++;

	int x = tb.width / 2 - dev.textWidth(w.text) / 2;
	dev.clip(pb.x, pb.y, pb.width, pb.height);
	dev.drawText(w.text, x + tb.x, (tb.height / 2 - dev.cellHeight() / 2) + tb.y, shade, shade, shade);
	dev.unclip();
}

UI_WIDGET_BOUNDS_IMPL(Button) {
	return ctx.bounds;
}

UI_WIDGET_DRAW_IMPL(Root) {
	Rect b = sys->bounds(wid);
	if (w.child != 0) {
		sys->draw(dev, w.child, Context{ .bounds = b });
	}
}

UI_WIDGET_BOUNDS_IMPL(Root) {
	auto size = dev.size();
	Rect b(0, 0, std::get<0>(size), std::get<1>(size));
	if (w.child) sys->bounds(dev, w.child, Context{ .bounds = b });
	return b;
}

UI_WIDGET_DRAW_IMPL(Placement) {
	Rect nb = sys->bounds(wid);
	if (w.child != 0) {
		sys->draw(dev, w.child, Context{ .bounds = nb });
	}
}

UI_WIDGET_BOUNDS_IMPL(Placement) {
	int pw = int(float(ctx.bounds.width) * w.x);
	int ph = int(float(ctx.bounds.height) * w.y);

	Rect b = ctx.bounds;
	Rect pb(b.x + pw, b.y + ph, b.width, b.height);
	if (w.child) sys->bounds(dev, w.child, Context{ .bounds = pb });
	return pb;
}

UI_WIDGET_DRAW_IMPL(Container) {
	Rect pb = sys->bounds(wid);
	Rect tb(pb);
	tb.pad(w.padding, w.padding, w.padding, w.padding);

	if (w.background) dev.drawPatch(6, pb.x, pb.y, pb.width, pb.height);
	if (w.child != 0) {
		dev.clip(tb.x-1, tb.y-1, tb.width+2, tb.height+2);
		sys->draw(dev, w.child, Context{ .bounds = tb });
		dev.unclip();
	}
}

UI_WIDGET_BOUNDS_IMPL(Container) {
	Rect b = Rect(ctx.bounds.x, ctx.bounds.y, w.width, w.height);
	if (w.width <= 0) b.width = ctx.bounds.width;
	if (w.height <= 0) b.height = ctx.bounds.height;

	Rect tb(b);
	tb.pad(5, 5, 5, 5);
	if (w.child) sys->bounds(dev, w.child, Context{ .bounds = tb });
	return b;
}

UI_WIDGET_DRAW_IMPL(Column) {
	int y = 0;
	for (auto cid : w.children) {
		if (!cid) continue;
		Rect b = sys->bounds(cid);
		sys->draw(dev, cid, Context{ .bounds = b });
	}
}

UI_WIDGET_BOUNDS_IMPL(Column) {
	Rect pb = ctx.bounds;
	int wh = pb.width, ht = 0;
	int y = 0;
	for (auto cid : w.children) {
		if (!cid) continue;

		sys->bounds(dev, cid, Context{ .bounds = Rect(pb.x, pb.y + y, pb.width, pb.height) });
		Rect b = sys->bounds(cid);
		int x = 0;
		switch (w.alignment) {
			default: x = 0;
			case Alignment::Far: x = pb.width - b.width; break;
			case Alignment::Center: x = pb.width / 2 - b.width / 2; break;
		}
		sys->bounds(dev, cid, Context{ .bounds = Rect(pb.x + x, pb.y + y, pb.width, pb.height) });

		ht += b.height + w.spacing;
		y += b.height + w.spacing;
	}
	return Rect(ctx.bounds.x, ctx.bounds.y, wh, ht);
}

// --------------- EVENTS

UI_WIDGET_MOUSE_EVENT_IMPL(Button) {
	if (w.disabled) return false;
	Rect b = sys->bounds(wid);
	switch (e.type) {
		case MouseEvent::MouseEventMove: {
			if (w.state == ButtonState::ButtonStateNormal) {
				if (b.has(e.x, e.y)) {
					w.state = ButtonState::ButtonStateHover;
				}
			} else if (w.state == ButtonState::ButtonStateHover) {
				if (!b.has(e.x, e.y)) {
					w.state = ButtonState::ButtonStateNormal;
				}
			}
		} break;
		case MouseEvent::MouseEventDown: {
			if (w.state == ButtonState::ButtonStateHover) {
				sys->focused = wid;
				w.state = ButtonState::ButtonStatePressed;
				return true;
			}
		} break;
		case MouseEvent::MouseEventUp: {
			if (w.state == ButtonState::ButtonStatePressed) {
				if (b.has(e.x, e.y)) {
					if (w.onPressed) w.onPressed();
					w.state = ButtonState::ButtonStateHover;
					return true;
				} else {
					w.state = ButtonState::ButtonStateNormal;
				}
			}
		} break;
	}
	return false;
}

UI_WIDGET_MOUSE_EVENT_IMPL(Slider) {
	std::string txt = std::to_string(w.value);
	Rect b = sys->bounds(wid);
	Rect track(b.x + SliderThumbWidth / 2, b.y, b.width - SliderThumbWidth, SliderThumbWidth);

	auto sliderBehavior = [&]() {
		sys->focused = wid;

		if (!b.has(e.x, e.y)) {
			w.__state = ButtonState::ButtonStateNormal;
			return false;
		}

		float ratio = float(e.x - track.x) / track.width;
		int newValue = std::clamp(w.min + int(ratio * (w.max - w.min)), w.min, w.max);
		if (newValue != w.value) {
			w.value = newValue;
			if (w.onChange) w.onChange(w.value);
			return true;
		}

		return false;
	};

	if (e.type == MouseEvent::MouseEventDown) {
		w.__state = ButtonState::ButtonStatePressed;
		return sliderBehavior();
	} else if (e.type == MouseEvent::MouseEventMove) {
		if (w.__state == ButtonState::ButtonStatePressed) {
			return sliderBehavior();
		}
	} else if (e.type == MouseEvent::MouseEventUp) {
		w.__state = ButtonState::ButtonStateNormal;
	}
	return false;
}

UI_WIDGET_MOUSE_EVENT_IMPL(Text) { return false; }

UI_WIDGET_MOUSE_EVENT_IMPL(Root) {
	if (w.child == 0) return false;
	auto size = dev.size();
	return sys->processMouse(dev, e, w.child, Context{ .bounds = Rect(0, 0, std::get<0>(size), std::get<1>(size)) });
}

UI_WIDGET_MOUSE_EVENT_IMPL(Placement) {
	if (w.child == 0) return false;
	int pw = int(float(ctx.bounds.width) * w.x);
	int ph = int(float(ctx.bounds.height) * w.y);

	Rect b = ctx.bounds;
	return sys->processMouse(dev, e, w.child, Context{ .bounds = Rect(b.x + pw, b.y + ph, b.width, b.height) });
}

UI_WIDGET_MOUSE_EVENT_IMPL(Container) {
	if (w.child == 0) return false;
	Rect pb = ctx.bounds;
	Rect tb(pb);
	tb.pad(5, 5, 5, 5);

	if (!tb.has(e.x, e.y)) return false;

	return sys->processMouse(dev, e, w.child, Context{ .bounds = tb });
}

UI_WIDGET_MOUSE_EVENT_IMPL(Column) {
	for (auto cid : w.children) {
		if (!cid) continue;
		if (sys->processMouse(dev, e, cid, Context{ .bounds = sys->bounds(cid) })) return true;
	}
	return false;
}

UI_WIDGET_MOUSE_EVENT_IMPL(Input) {
	if (w.disabled) return false;
	Rect b = sys->bounds(wid);
	if (b.has(e.x, e.y) && e.type == MouseEvent::MouseEventDown) {
		updateView(wid, w, dev, sys);
		sys->focused = wid;
		return true;
	}
	return false;
}

#endif // UI_H
