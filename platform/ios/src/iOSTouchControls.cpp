#include "stdafx.h"

#include "iOSTouchControls.hpp"
#include "iOSPlatform.h"

#include <Application.hpp>
#include <GameConfig.hpp>
#include <Input.hpp>
#include <Graphics/Window.hpp>
#include <Shared/Log.hpp>

#include <nanovg.h>

#include <cmath>
#include <map>

iOSTouchControls* g_iosTouchControls = nullptr;

namespace
{
	enum class TargetKind
	{
		None,
		// Touch started on empty space: forwarded to the UI as a mouse tap.
		Passthrough,
		KnobLeft,
		KnobRight,
		Button,
		Start,
		Back,
	};

	// Which visual slot a button maps to when drawing.
	enum Slot
	{
		SlotBT0 = 0, SlotBT1, SlotBT2, SlotBT3,
		SlotFX0, SlotFX1,
		SlotStart, SlotBack,
		SlotCount,
	};

	struct Target
	{
		TargetKind kind = TargetKind::None;
		SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
		int slot = -1;
		Vector2 lastPos;
	};

	/*
		Layout of the on-screen controller.

		It follows the console layout the game emulates: two knobs on the far left
		and right, BT-A/B/C plus FX-L for the left hand, BT-D plus FX-R for the
		right hand, and small Start/Back buttons in the top corners.

		Everything is stored as a fraction of the game resolution so the panel
		scales to any iPad (or iPhone) screen.
	*/
	struct Layout
	{
		Vector2 knobPos[2];
		float knobRadius = 0.0f;

		Vector2 btPos[4];
		float btRadius = 0.0f;

		Vector2 fxPos[2];
		Vector2 fxSize;

		Vector2 startPos;
		Vector2 backPos;
		Vector2 cornerSize;

		void Compute(const Vector2& res)
		{
			const float w = res.x;
			const float h = res.y;

			knobRadius = 0.085f * h;
			knobPos[0] = Vector2(0.100f * w, 0.520f * h);
			knobPos[1] = Vector2(0.900f * w, 0.520f * h);

			btRadius = 0.062f * h;
			btPos[0] = Vector2(0.225f * w, 0.865f * h); // BT-A
			btPos[1] = Vector2(0.325f * w, 0.865f * h); // BT-B
			btPos[2] = Vector2(0.425f * w, 0.865f * h); // BT-C
			btPos[3] = Vector2(0.575f * w, 0.865f * h); // BT-D

			fxSize = Vector2(0.055f * w, 0.185f * h);
			fxPos[0] = Vector2(0.085f * w, 0.865f * h); // FX-L
			fxPos[1] = Vector2(0.915f * w, 0.865f * h); // FX-R

			cornerSize = Vector2(0.085f * w, 0.075f * h);
			startPos = Vector2(0.065f * w, 0.075f * h);
			backPos = Vector2(0.935f * w, 0.075f * h);
		}
	};

	const char* const kButtonLabels[4] = { "A", "B", "C", "D" };
	const char* const kFxLabels[2] = { "FX-L", "FX-R" };

	// Finger movement is handed to the mouse laser device 1:1; this only exists to
	// make the knob a little less twitchy on a small screen.
	constexpr float kKnobDragGain = 1.5f;

	// Extra room around a control that still counts as a hit, so the player does
	// not have to hit the exact outline.
	constexpr float kKnobGrabScale = 1.9f;
	constexpr float kButtonGrabScale = 1.3f;

	// Opacity of the controls when idle and right after they were used.
	constexpr float kIdleAlpha = 0.20f;
	constexpr float kActiveAlpha = 0.80f;

	bool PointInRect(const Vector2& p, const Vector2& center, const Vector2& size)
	{
		const float hw = size.x * 0.5f;
		const float hh = size.y * 0.5f;
		return p.x >= center.x - hw && p.x <= center.x + hw &&
			   p.y >= center.y - hh && p.y <= center.y + hh;
	}

	float Distance(const Vector2& a, const Vector2& b)
	{
		const float dx = a.x - b.x;
		const float dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy);
	}
}

class iOSTouchControls_Impl
{
public:
	Graphics::Window* window = nullptr;
	Layout layout;
	Vector2 resolution = Vector2(1280.0f, 720.0f);

	std::map<int32, Target> fingers;

	// Movement that has not reached a whole pixel yet. Without this, slow knob
	// drags would lose motion when it is truncated to an integer.
	float knobAccum[2] = { 0.0f, 0.0f };
	// Visual feedback: 1 right after a touch, fading out so the playfield stays
	// readable while a song is playing.
	float knobActivity[2] = { 0.0f, 0.0f };
	float slotActivity[SlotCount] = { 0.0f };
	bool mousePassthroughDown = false;

	SDL_Scancode boundKey[SlotCount] = { SDL_SCANCODE_UNKNOWN };

public:
	void RefreshBindings()
	{
		// The on-screen buttons follow the player's key bindings. A binding that was
		// cleared in the settings (-1) falls back to the game default so a button can
		// never end up dead.
		auto resolve = [](GameConfigKeys key, SDL_Scancode fallback) -> SDL_Scancode
		{
			const int32 bound = g_gameConfig.GetInt(key);
			return bound < 0 ? fallback : (SDL_Scancode)bound;
		};

		boundKey[SlotBT0] = resolve(GameConfigKeys::Key_BT0, SDL_SCANCODE_D);
		boundKey[SlotBT1] = resolve(GameConfigKeys::Key_BT1, SDL_SCANCODE_F);
		boundKey[SlotBT2] = resolve(GameConfigKeys::Key_BT2, SDL_SCANCODE_J);
		boundKey[SlotBT3] = resolve(GameConfigKeys::Key_BT3, SDL_SCANCODE_K);
		boundKey[SlotFX0] = resolve(GameConfigKeys::Key_FX0, SDL_SCANCODE_C);
		boundKey[SlotFX1] = resolve(GameConfigKeys::Key_FX1, SDL_SCANCODE_M);
		boundKey[SlotStart] = resolve(GameConfigKeys::Key_BTS, SDL_SCANCODE_1);
		boundKey[SlotBack] = resolve(GameConfigKeys::Key_Back, SDL_SCANCODE_ESCAPE);
	}

	void RecomputeLayout()
	{
		resolution = Vector2((float)g_resolution.x, (float)g_resolution.y);
		if(resolution.x <= 0.0f || resolution.y <= 0.0f)
			resolution = Vector2(1280.0f, 720.0f);
		layout.Compute(resolution);
	}

	void Tick(float deltaTime)
	{
		for(int i = 0; i < 2; i++)
			knobActivity[i] = std::fmax(0.0f, knobActivity[i] - deltaTime * 0.4f);
		for(int i = 0; i < SlotCount; i++)
			slotActivity[i] = std::fmax(0.0f, slotActivity[i] - deltaTime * 2.5f);
	}

	Target HitTest(const Vector2& pos) const
	{
		Target t;

		for(int i = 0; i < 2; i++)
		{
			if(Distance(pos, layout.knobPos[i]) <= layout.knobRadius * kKnobGrabScale)
			{
				t.kind = (i == 0) ? TargetKind::KnobLeft : TargetKind::KnobRight;
				return t;
			}
		}

		for(int i = 0; i < 4; i++)
		{
			if(Distance(pos, layout.btPos[i]) <= layout.btRadius * kButtonGrabScale)
			{
				t.kind = TargetKind::Button;
				t.slot = SlotBT0 + i;
				t.key = boundKey[t.slot];
				return t;
			}
		}

		for(int i = 0; i < 2; i++)
		{
			if(PointInRect(pos, layout.fxPos[i], layout.fxSize * kButtonGrabScale))
			{
				t.kind = TargetKind::Button;
				t.slot = SlotFX0 + i;
				t.key = boundKey[t.slot];
				return t;
			}
		}

		if(PointInRect(pos, layout.startPos, layout.cornerSize))
		{
			t.kind = TargetKind::Start;
			t.slot = SlotStart;
			t.key = boundKey[SlotStart];
			return t;
		}

		if(PointInRect(pos, layout.backPos, layout.cornerSize))
		{
			t.kind = TargetKind::Back;
			t.slot = SlotBack;
			t.key = boundKey[SlotBack];
			return t;
		}

		return t;
	}

	// The trailing pressure argument is part of the Window::OnFinger* delegate
	// signature; the overlay does not use it.
	void OnFingerDown(int32 id, Vector2 pos, float pressure)
	{
		(void)pressure;
		if(!window)
			return;

		Target target = HitTest(pos);
		target.lastPos = pos;

		if(target.kind == TargetKind::None)
		{
			// Empty space: let the player use the menus by forwarding the touch as a
			// mouse tap. SDL's own touch-to-mouse emulation is disabled on iOS so
			// that dragging the playfield cannot move the lasers by accident.
			target.kind = TargetKind::Passthrough;
			if(mousePassthroughDown)
				return; // Only ever track a single UI pointer.
			mousePassthroughDown = true;
			window->InjectMousePosition((int32)pos.x, (int32)pos.y);
			window->InjectMouseButton(MouseButton::Left, true);
		}
		else if(target.kind == TargetKind::KnobLeft || target.kind == TargetKind::KnobRight)
		{
			const int index = (target.kind == TargetKind::KnobLeft) ? 0 : 1;
			knobActivity[index] = 1.0f;
		}
		else
		{
			window->InjectKey(target.key, true);
			if(target.slot >= 0)
				slotActivity[target.slot] = 1.0f;
		}

		fingers[id] = target;
	}

	void OnFingerMotion(int32 id, Vector2 pos, float pressure)
	{
		(void)pressure;
		auto it = fingers.find(id);
		if(it == fingers.end() || !window)
			return;

		Target& target = it->second;

		if(target.kind == TargetKind::Passthrough)
		{
			window->InjectMousePosition((int32)pos.x, (int32)pos.y);
			return;
		}

		if(target.kind == TargetKind::KnobLeft || target.kind == TargetKind::KnobRight)
		{
			const int index = (target.kind == TargetKind::KnobLeft) ? 0 : 1;
			const Vector2 delta = pos - target.lastPos;
			target.lastPos = pos;
			knobActivity[index] = 1.0f;

			// A knob does not care which way it is turned, so horizontal and vertical
			// drags are both accepted and simply summed.
			knobAccum[index] += (delta.x + delta.y) * kKnobDragGain;
			const int32 whole = (int32)std::floor(knobAccum[index]);
			if(whole == 0)
				return;
			knobAccum[index] -= (float)whole;

			// Knob 0 feeds the mouse X axis and knob 1 the mouse Y axis. With the
			// default Mouse_Laser0Axis/Mouse_Laser1Axis mapping that is exactly how
			// the game already converts mouse movement into laser input, so both
			// knobs behave like the real endless encoders.
			if(index == 0)
				window->InjectMouseMotion(whole, 0);
			else
				window->InjectMouseMotion(0, whole);
			return;
		}

		// Buttons: sliding onto another control releases the old one and presses the
		// new one, which is what a player expects from a touch panel.
		Target newTarget = HitTest(pos);
		if(newTarget.kind == target.kind && newTarget.key == target.key)
			return;

		window->InjectKey(target.key, false);
		if(target.slot >= 0)
			slotActivity[target.slot] = 0.0f;

		if(newTarget.kind == TargetKind::Button || newTarget.kind == TargetKind::Start ||
		   newTarget.kind == TargetKind::Back)
		{
			newTarget.lastPos = pos;
			window->InjectKey(newTarget.key, true);
			if(newTarget.slot >= 0)
				slotActivity[newTarget.slot] = 1.0f;
			fingers[id] = newTarget;
		}
		else if(newTarget.kind == TargetKind::KnobLeft || newTarget.kind == TargetKind::KnobRight)
		{
			// Sliding from a button onto a knob just drops the button: turning a
			// knob needs the finger to start on it, otherwise a stray slide would
			// move the laser.
			fingers.erase(it);
		}
		else
		{
			// Sliding off the panel turns the touch into a UI tap so the finger can
			// keep being used for menu clicks instead of going dead.
			if(mousePassthroughDown)
			{
				fingers.erase(it);
			}
			else
			{
				if(target.slot >= 0)
					slotActivity[target.slot] = 0.0f;
				Target passthrough;
				passthrough.kind = TargetKind::Passthrough;
				passthrough.lastPos = pos;
				mousePassthroughDown = true;
				window->InjectMousePosition((int32)pos.x, (int32)pos.y);
				window->InjectMouseButton(MouseButton::Left, true);
				fingers[id] = passthrough;
			}
		}
	}

	void OnFingerUp(int32 id, Vector2 pos, float pressure)
	{
		(void)pos;
		(void)pressure;
		if(!window)
			return;
		auto it = fingers.find(id);
		if(it == fingers.end())
			return;

		const Target& target = it->second;
		if(target.kind == TargetKind::Passthrough)
		{
			window->InjectMouseButton(MouseButton::Left, false);
			mousePassthroughDown = false;
		}
		else if(target.slot >= 0)
		{
			slotActivity[target.slot] = 0.0f;
		}
		fingers.erase(it);
	}

	// Releases everything we currently hold. Used when the app is backgrounded.
	void ReleaseAll()
	{
		if(window)
		{
			for(auto& entry : fingers)
			{
				const Target& t = entry.second;
				if(t.kind == TargetKind::Passthrough)
				{
					window->InjectMouseButton(MouseButton::Left, false);
				}
				else if(t.kind != TargetKind::KnobLeft && t.kind != TargetKind::KnobRight)
				{
					window->InjectKey(t.key, false);
				}
			}
		}
		fingers.clear();
		mousePassthroughDown = false;
		knobAccum[0] = knobAccum[1] = 0.0f;
		for(int i = 0; i < 2; i++)
			knobActivity[i] = 0.0f;
		for(int i = 0; i < SlotCount; i++)
			slotActivity[i] = 0.0f;
	}

	void Render();
};

void iOSTouchControls_Impl::Render()
{
	NVGcontext* vg = g_application ? g_application->GetNVGContext() : nullptr;
	if(!vg)
		return;

	nvgSave(vg);
	nvgFontFace(vg, "fallback");

	// Playfield hint: keeps the panel visible without covering the notes.
	for(int i = 0; i < 2; i++)
	{
		const float alpha = kIdleAlpha + (kActiveAlpha - kIdleAlpha) * knobActivity[i];
		const Vector2 p = layout.knobPos[i];
		const float r = layout.knobRadius;

		nvgBeginPath(vg);
		nvgCircle(vg, p.x, p.y, r);
		nvgFillColor(vg, nvgRGBAf(0.35f, 0.75f, 1.0f, alpha * 0.22f));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgCircle(vg, p.x, p.y, r);
		nvgStrokeColor(vg, nvgRGBAf(0.45f, 0.85f, 1.0f, alpha));
		nvgStrokeWidth(vg, r * 0.10f);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgCircle(vg, p.x, p.y, r * 0.16f);
		nvgFillColor(vg, nvgRGBAf(0.45f, 0.85f, 1.0f, alpha));
		nvgFill(vg);

		nvgFontSize(vg, r * 0.34f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha * 0.9f));
		nvgText(vg, p.x, p.y + r * 0.62f, "KNOB", nullptr);
	}

	// BT buttons.
	for(int i = 0; i < 4; i++)
	{
		const int slot = SlotBT0 + i;
		const float alpha = kIdleAlpha + (kActiveAlpha - kIdleAlpha) * slotActivity[slot];
		const Vector2 p = layout.btPos[i];
		const float r = layout.btRadius;

		nvgBeginPath(vg);
		nvgCircle(vg, p.x, p.y, r);
		nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha * 0.25f));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgCircle(vg, p.x, p.y, r);
		nvgStrokeColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha));
		nvgStrokeWidth(vg, r * 0.10f);
		nvgStroke(vg);

		nvgFontSize(vg, r * 0.90f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha + (1.0f - alpha) * 0.5f * slotActivity[slot]));
		nvgText(vg, p.x, p.y, kButtonLabels[i], nullptr);
	}

	// FX buttons.
	for(int i = 0; i < 2; i++)
	{
		const int slot = SlotFX0 + i;
		const float alpha = kIdleAlpha + (kActiveAlpha - kIdleAlpha) * slotActivity[slot];
		const Vector2 p = layout.fxPos[i];
		const Vector2 s = layout.fxSize;
		const float radius = s.x * 0.35f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, p.x - s.x * 0.5f, p.y - s.y * 0.5f, s.x, s.y, radius);
		nvgFillColor(vg, nvgRGBAf(1.0f, 0.85f, 0.25f, alpha * 0.25f));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgRoundedRect(vg, p.x - s.x * 0.5f, p.y - s.y * 0.5f, s.x, s.y, radius);
		nvgStrokeColor(vg, nvgRGBAf(1.0f, 0.85f, 0.25f, alpha));
		nvgStrokeWidth(vg, s.x * 0.09f);
		nvgStroke(vg);

		nvgFontSize(vg, s.x * 0.42f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBAf(1.0f, 0.9f, 0.5f, alpha + (1.0f - alpha) * 0.5f * slotActivity[slot]));
		nvgText(vg, p.x, p.y, kFxLabels[i], nullptr);
	}

	// Start / Back.
	{
		const char* const labels[2] = { "START", "BACK" };
		const Vector2 positions[2] = { layout.startPos, layout.backPos };
		for(int i = 0; i < 2; i++)
		{
			const int slot = SlotStart + i;
			const float alpha = kIdleAlpha + (kActiveAlpha - kIdleAlpha) * slotActivity[slot];
			const Vector2 p = positions[i];
			const Vector2 s = layout.cornerSize;

			nvgBeginPath(vg);
			nvgRoundedRect(vg, p.x - s.x * 0.5f, p.y - s.y * 0.5f, s.x, s.y, s.y * 0.35f);
			nvgFillColor(vg, nvgRGBAf(0.6f, 0.6f, 0.6f, alpha * 0.30f));
			nvgFill(vg);

			nvgBeginPath(vg);
			nvgRoundedRect(vg, p.x - s.x * 0.5f, p.y - s.y * 0.5f, s.x, s.y, s.y * 0.35f);
			nvgStrokeColor(vg, nvgRGBAf(0.85f, 0.85f, 0.85f, alpha));
			nvgStrokeWidth(vg, s.y * 0.10f);
			nvgStroke(vg);

			nvgFontSize(vg, s.y * 0.42f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha + (1.0f - alpha) * 0.5f * slotActivity[slot]));
			nvgText(vg, p.x, p.y, labels[i], nullptr);
		}
	}

	nvgRestore(vg);
}

iOSTouchControls::iOSTouchControls()
	: m_impl(new iOSTouchControls_Impl())
{
}

iOSTouchControls::~iOSTouchControls()
{
	Cleanup();
	delete m_impl;
}

void iOSTouchControls::Init(Graphics::Window& window)
{
	if(m_impl->window)
		return;

	m_impl->window = &window;
	m_impl->RefreshBindings();
	m_impl->RecomputeLayout();

	window.OnFingerDown.Add(m_impl, &iOSTouchControls_Impl::OnFingerDown);
	window.OnFingerMotion.Add(m_impl, &iOSTouchControls_Impl::OnFingerMotion);
	window.OnFingerUp.Add(m_impl, &iOSTouchControls_Impl::OnFingerUp);

	Log("iOS: on-screen controller registered", Logger::Severity::Info);
}

void iOSTouchControls::Cleanup()
{
	if(!m_impl->window)
		return;
	m_impl->ReleaseAll();
	m_impl->window->OnFingerDown.RemoveAll(m_impl);
	m_impl->window->OnFingerMotion.RemoveAll(m_impl);
	m_impl->window->OnFingerUp.RemoveAll(m_impl);
	m_impl->window = nullptr;
}

void iOSTouchControls::Tick(float deltaTime)
{
	m_impl->Tick(deltaTime);
}

void iOSTouchControls::Render(float deltaTime)
{
	(void)deltaTime;
	m_impl->RecomputeLayout();
	m_impl->Render();
}

void iOSTouchControls::ReleaseAll()
{
	m_impl->ReleaseAll();
}
