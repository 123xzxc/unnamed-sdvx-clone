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
		// Toggles the visibility of the whole on-screen panel. This is not a game
		// input: it never injects a key, it only flips a flag on the overlay.
		Hide,
		// Touch started on a control but moved outside of it. It keeps the button
		// held while the finger stays off the panel, and is only released when the
		// finger is lifted.
		ButtonDrag,
	};

	// Which visual slot a button maps to when drawing.
	enum Slot
	{
		SlotBT0 = 0, SlotBT1, SlotBT2, SlotBT3,
		SlotFX0, SlotFX1,
		SlotStart, SlotBack, SlotHide,
		SlotCount,
	};

	struct Target
	{
		TargetKind kind = TargetKind::None;
		SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
		int slot = -1;
		// Last finger position and where the finger went down, in game pixels.
		Vector2 lastPos;
		Vector2 dragOrigin;
		// Which way a knob is being turned: -1 until the gesture has moved far
		// enough to tell, then 0 for horizontal and 1 for vertical.
		int knobAxis = -1;
	};

	/*
		Layout of the on-screen controller.

		It mirrors the console the game emulates, arranged the way the player
		sketched it on a landscape iPad:

			[ HIDE ]            ( START )            [ BACK ]

			 (KNOB)                                    (KNOB)

			        A     B     C      D

			      [ FX-L ]          [ FX-R ]

		Everything is stored as a fraction of the game resolution so the panel
		scales to any iPad (or iPhone) screen, in either orientation. Sizes come
		from the shorter edge so a portrait screen does not get giant buttons.
	*/
	struct Layout
	{
		Vector2 knobPos[2];
		float knobRadius = 0.0f;

		Vector2 btPos[4];
		float btSize = 0.0f;

		Vector2 fxPos[2];
		Vector2 fxSize;

		Vector2 startPos;
		Vector2 backPos;
		Vector2 hidePos;
		float cornerSize = 0.0f;

		void Compute(const Vector2& res)
		{
			const float w = res.x;
			const float h = res.y;
			const float unit = std::fmin(w, h);
			const bool portrait = h > w;

			/*
				Top row. The toggle sits in the left corner, Start in the centre so
				either thumb can reach it, and Back in the right corner. Portrait
				screens are much taller, so the row hugs the top edge there.
			*/
			cornerSize = 0.078f * unit;
			const float topY = (portrait ? 0.045f : 0.100f) * h;
			hidePos = Vector2(0.075f * unit, topY);
			startPos = Vector2(0.500f * w, topY);
			backPos = Vector2(w - hidePos.x, topY);

			// Knobs on the upper left and right, clear of the playfield centre.
			knobRadius = 0.085f * unit;
			const float knobX = (portrait ? 0.125f : 0.105f) * w;
			const float knobY = (portrait ? 0.340f : 0.360f) * h;
			knobPos[0] = Vector2(knobX, knobY);
			knobPos[1] = Vector2(w - knobX, knobY);

			/*
				The four BT buttons sit in a row across the middle of the screen,
				centred as a group. BT-D is set slightly further apart than A/B/C,
				which is both what the console does and what the sketch shows.
			*/
			btSize = 0.105f * unit;
			const float btGap = 0.035f * w;
			const float btDGap = 0.065f * w;
			const float btTotal = btSize * 4.0f + btGap * 2.0f + btDGap;
			float btX = (w - btTotal) * 0.5f + btSize * 0.5f;
			btPos[0] = Vector2(btX, (portrait ? 0.600f : 0.610f) * h);
			btX += btSize + btGap;
			btPos[1] = Vector2(btX, btPos[0].y);
			btX += btSize + btGap;
			btPos[2] = Vector2(btX, btPos[0].y);
			btX += btSize + btDGap;
			btPos[3] = Vector2(btX, btPos[0].y);

			/*
				FX-L and FX-R are the wide buttons at the bottom, one under each
				half of the BT row.
			*/
			const float fxY = (portrait ? 0.800f : 0.810f) * h;
			fxSize = Vector2((portrait ? 0.300f : 0.200f) * w, 0.115f * unit);
			const float fxOffset = (portrait ? 0.170f : 0.160f) * w;
			fxPos[0] = Vector2(w * 0.5f - fxOffset, fxY);
			fxPos[1] = Vector2(w * 0.5f + fxOffset, fxY);
		}
	};

	const char* const kButtonLabels[4] = { "A", "B", "C", "D" };
	const char* const kFxLabels[2] = { "FX-L", "FX-R" };

	constexpr float kPi = 3.14159265358979f;

	/*
		A finger turns a knob. The direction that counts as "turning" is decided
		once per gesture from whichever way the finger actually moved, and then
		locked: summing the horizontal and vertical movement (what this used to
		do) meant that the sideways wobble of a thumb pivoting on the glass was
		added to the real movement, and the laser jittered left and right.
	*/
	constexpr float kKnobAxisLockPixels = 14.0f;
	// How many full turns the laser makes when a finger crosses the whole screen.
	constexpr float kKnobTurnsPerSwipe = 2.0f;

	// Extra room around a control that still counts as a hit, so the player does
	// not have to hit the exact outline.
	constexpr float kKnobGrabScale = 1.35f;
	constexpr float kButtonGrabScale = 1.15f;
	constexpr float kToggleGrabScale = 1.30f;

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

	bool PointInSquare(const Vector2& p, const Vector2& center, float size)
	{
		return PointInRect(p, center, Vector2(size, size));
	}

	float Distance(const Vector2& a, const Vector2& b)
	{
		const float dx = a.x - b.x;
		const float dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	float ActivityAlpha(float activity)
	{
		return kIdleAlpha + (kActiveAlpha - kIdleAlpha) * activity;
	}

	// The Start/Back icons are the game's "home plate" pentagon.
	void PentagonPath(NVGcontext* vg, const Vector2& c, float size)
	{
		const float r = size * 0.5f;
		nvgBeginPath(vg);
		for(int i = 0; i < 5; i++)
		{
			const float a = -kPi * 0.5f + (float)i * (kPi * 2.0f / 5.0f);
			const float x = c.x + std::cos(a) * r;
			const float y = c.y + std::sin(a) * r;
			if(i == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		nvgClosePath(vg);
	}

	// Panel toggle: a rounded square, so it is not mistaken for Start or Back.
	void DrawToggle(NVGcontext* vg, const Vector2& p, float size, const char* label, float activity)
	{
		const float alpha = ActivityAlpha(activity);
		const float r = size * 0.28f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, p.x - size * 0.5f, p.y - size * 0.5f, size, size, r);
		nvgFillColor(vg, nvgRGBAf(0.85f, 0.55f, 0.25f, alpha * 0.35f));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgRoundedRect(vg, p.x - size * 0.5f, p.y - size * 0.5f, size, size, r);
		nvgStrokeColor(vg, nvgRGBAf(1.0f, 0.80f, 0.45f, alpha));
		nvgStrokeWidth(vg, size * 0.07f);
		nvgStroke(vg);

		nvgFontSize(vg, size * 0.30f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBAf(1.0f, 0.95f, 0.85f, alpha + (1.0f - alpha) * 0.6f * activity));
		nvgText(vg, p.x, p.y, label, nullptr);
	}

	// Start / Back: the pentagon the game uses for those two buttons.
	void DrawPentagonButton(NVGcontext* vg, const Vector2& p, float size, const char* label, float activity)
	{
		const float alpha = ActivityAlpha(activity);

		PentagonPath(vg, p, size);
		nvgFillColor(vg, nvgRGBAf(0.75f, 0.78f, 0.82f, alpha * 0.30f));
		nvgFill(vg);

		PentagonPath(vg, p, size);
		nvgStrokeColor(vg, nvgRGBAf(0.92f, 0.94f, 0.97f, alpha));
		nvgStrokeWidth(vg, size * 0.07f);
		nvgStroke(vg);

		nvgFontSize(vg, size * 0.26f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha + (1.0f - alpha) * 0.6f * activity));
		nvgText(vg, p.x, p.y + size * 0.04f, label, nullptr);
	}
}

class iOSTouchControls_Impl
{
public:
	Graphics::Window* window = nullptr;
	Layout layout;
	Vector2 resolution = Vector2(1280.0f, 720.0f);
	Vector2i loggedResolution = Vector2i(-1, -1);

	std::map<int32, Target> fingers;

	// Movement that has not reached a whole pixel yet. Without this, slow knob
	// drags would lose motion when it is truncated to an integer.
	float knobAccum[2] = { 0.0f, 0.0f };
	// Visual feedback: 1 right after a touch, fading out so the playfield stays
	// readable while a song is playing.
	float knobActivity[2] = { 0.0f, 0.0f };
	// Angle the finger has turned each knob by, only used to draw the needle.
	float knobAngle[2] = { 0.0f, 0.0f };
	float slotActivity[SlotCount] = { 0.0f };
	bool mousePassthroughDown = false;
	// Set by the top-left toggle: the panel stops drawing and stops grabbing
	// touches, and only the toggle itself stays live.
	bool controlsHidden = false;

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

		const int32 width = (int32)resolution.x;
		const int32 height = (int32)resolution.y;
		if(width != loggedResolution.x || height != loggedResolution.y)
		{
			loggedResolution = Vector2i(width, height);
			Logf("iOS: touch panel laid out for %dx%d (%s)", Logger::Severity::Info,
				width, height, height > width ? "portrait" : "landscape");
		}
	}

	void Tick(float deltaTime)
	{
		for(int i = 0; i < 2; i++)
			knobActivity[i] = std::fmax(0.0f, knobActivity[i] - deltaTime * 0.4f);
		for(int i = 0; i < SlotCount; i++)
			slotActivity[i] = std::fmax(0.0f, slotActivity[i] - deltaTime * 2.5f);
	}

	/*
		How many radians one mouse pixel is worth to the game.

		Input::CalculateRealMouseSens() converts the Mouse_Sensitivity setting
		into "6 / ppr" radians per pixel, and EstimatePprFromSens() estimates the
		ppr with pow(200 / sens, 1.2). Repeating that here lets the knob inject
		exactly the number of pixels that produce the angle the finger asked for,
		so the touch knob feels the same wherever the mouse slider is set. The
		sign is dropped on purpose: dragging right always turns the laser the same
		way, and InvertLaserInput is still there for players who want it flipped.
	*/
	float MouseRadiansPerPixel() const
	{
		const float sens = g_gameConfig.GetFloat(GameConfigKeys::Mouse_Sensitivity);
		const float magnitude = std::fabs(sens);
		// A sensitivity of zero makes the game ignore the mouse device entirely,
		// so there is no pixel count that would move the laser.
		if(magnitude < 0.01f)
			return 0.0f;
		const float ppr = std::pow(200.0f / magnitude, 1.2f);
		return 6.0f / ppr;
	}

	// SDL delivers touch coordinates normalized to 0..1 of the window, while the
	// overlay (and everything else in the engine) works in game pixels.
	Vector2 ToGameSpace(const Vector2& normalized) const
	{
		return Vector2(normalized.x * resolution.x, normalized.y * resolution.y);
	}

	Target HitTest(const Vector2& pos) const
	{
		Target t;

		/*
			The panel toggle is always live. It is checked before anything else and
			gets a wider grab margin than the other controls, because while the
			panel is hidden it is the only way to bring it back.
		*/
		if(PointInSquare(pos, layout.hidePos, layout.cornerSize * kToggleGrabScale))
		{
			t.kind = TargetKind::Hide;
			t.slot = SlotHide;
			return t;
		}

		// A hidden panel is invisible and inert: every other touch falls through
		// to the game UI as a normal tap.
		if(controlsHidden)
			return t;

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
			if(PointInSquare(pos, layout.btPos[i], layout.btSize * kButtonGrabScale))
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

		if(PointInSquare(pos, layout.startPos, layout.cornerSize))
		{
			t.kind = TargetKind::Start;
			t.slot = SlotStart;
			t.key = boundKey[SlotStart];
			return t;
		}

		if(PointInSquare(pos, layout.backPos, layout.cornerSize))
		{
			t.kind = TargetKind::Back;
			t.slot = SlotBack;
			t.key = boundKey[SlotBack];
			return t;
		}

		return t;
	}

	/*
		Releases every key and mouse button the overlay is holding without
		dropping the finger bookkeeping: the entries are turned into ButtonDrag so
		lifting the finger later cannot inject a second release. Used when the
		panel is hidden mid-touch, so nothing stays stuck while it is invisible.
	*/
	void ReleaseKeys()
	{
		if(!window)
			return;
		for(auto& entry : fingers)
		{
			Target& t = entry.second;
			if(t.kind == TargetKind::Passthrough)
			{
				window->InjectMouseButton(MouseButton::Left, false);
				mousePassthroughDown = false;
			}
			else if(t.kind == TargetKind::Button || t.kind == TargetKind::Start ||
					t.kind == TargetKind::Back)
			{
				window->InjectKey(t.key, false);
			}
			if(t.slot >= 0)
				slotActivity[t.slot] = 0.0f;
			t.kind = TargetKind::ButtonDrag;
			t.key = SDL_SCANCODE_UNKNOWN;
		}
	}

	// Turns a knob by the distance the finger has travelled since it went down.
	void ApplyKnobTurn(int index, const Target& target, const Vector2& pos)
	{
		if(!window)
			return;

		const Vector2 total = pos - target.dragOrigin;
		if(target.knobAxis < 0)
		{
			/*
				The first few pixels only decide which way the knob turns; until
				then nothing is sent to the game. 14 game pixels is well below the
				smallest deliberate movement and well above a resting thumb.
			*/
			if(std::fabs(total.x) + std::fabs(total.y) < kKnobAxisLockPixels)
				return;
		}

		const int axis = (target.knobAxis >= 0) ? target.knobAxis :
			(std::fabs(total.x) >= std::fabs(total.y) ? 0 : 1);
		const float travel = (axis == 0) ? total.x : total.y;
		const float span = std::fmax((axis == 0) ? resolution.x : resolution.y, 1.0f);

		// A full swipe across the screen is worth kKnobTurnsPerSwipe turns.
		const float angle = (travel / span) * kKnobTurnsPerSwipe * 2.0f * kPi;
		knobAngle[index] = angle;
		knobActivity[index] = 1.0f;

		const float radiansPerPixel = MouseRadiansPerPixel();
		if(radiansPerPixel <= 0.0f)
			return;

		knobAccum[index] += angle / radiansPerPixel;
		const int32 whole = (int32)std::floor(knobAccum[index]);
		if(whole == 0)
			return;
		knobAccum[index] -= (float)whole;

		// Knob 0 feeds the mouse X axis and knob 1 the mouse Y axis, which is
		// exactly the mapping the game already uses to turn mouse movement into
		// laser input (Mouse_Laser0Axis / Mouse_Laser1Axis), so both knobs behave
		// like the real endless encoders.
		if(index == 0)
			window->InjectMouseMotion(whole, 0);
		else
			window->InjectMouseMotion(0, whole);
	}

	// The trailing pressure argument is part of the Window::OnFinger* delegate
	// signature; the overlay does not use it.
	void OnFingerDown(int32 id, Vector2 pos, float pressure)
	{
		(void)pressure;
		if(!window)
			return;

		pos = ToGameSpace(pos);
		Target target = HitTest(pos);
		target.lastPos = pos;
		target.dragOrigin = pos;

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
			knobAccum[index] = 0.0f;
			knobAngle[index] = 0.0f;
		}
		else if(target.kind == TargetKind::Hide)
		{
			/*
				The panel toggle is handled entirely by the overlay: it flips the
				visibility flag instead of injecting a game key. Hiding the panel
				also releases whatever it was holding, so a button that was down
				when the toggle was tapped cannot stay stuck.
			*/
			slotActivity[SlotHide] = 1.0f;
			if(controlsHidden)
			{
				controlsHidden = false;
			}
			else
			{
				ReleaseKeys();
				controlsHidden = true;
			}
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

		pos = ToGameSpace(pos);
		Target& target = it->second;

		if(target.kind == TargetKind::Passthrough)
		{
			window->InjectMousePosition((int32)pos.x, (int32)pos.y);
			return;
		}

		if(target.kind == TargetKind::Hide)
			return; // The toggle fires on press and ignores any dragging.

		if(target.kind == TargetKind::KnobLeft || target.kind == TargetKind::KnobRight)
		{
			const int index = (target.kind == TargetKind::KnobLeft) ? 0 : 1;
			// The direction is decided from the movement since the finger landed,
			// so the answer never changes half way through a turn.
			if(target.knobAxis < 0)
			{
				const Vector2 total = pos - target.dragOrigin;
				if(std::fabs(total.x) + std::fabs(total.y) < kKnobAxisLockPixels)
				{
					knobActivity[index] = 1.0f;
					return;
				}
				target.knobAxis = (std::fabs(total.x) >= std::fabs(total.y)) ? 0 : 1;
			}
			ApplyKnobTurn(index, target, pos);
			return;
		}

		// Buttons: sliding onto another control releases the old one and presses the
		// new one, which is what a player expects from a touch panel.
		Target newTarget = HitTest(pos);
		// Sliding onto the panel toggle counts as sliding off the pad: the toggle
		// only reacts to a fresh press, never to a finger that wandered over it.
		if(newTarget.kind == TargetKind::Hide)
			newTarget = Target();

		/*
			Sliding off a button must not turn the touch into a UI tap. The old
			behaviour pressed the left mouse button whenever a finger left a control,
			and only released it on finger up: dragging from a button to empty space
			and lifting the finger there left the mouse button held down, so from the
			next frame on every menu click acted like a drag and the menu stopped
			responding. A touch that started on a control now stays a control touch
			for its whole lifetime.
		*/
		if(newTarget.kind == TargetKind::None)
		{
			if(target.kind == TargetKind::ButtonDrag)
				return; // Already released when the finger left the control.

			window->InjectKey(target.key, false);
			if(target.slot >= 0)
				slotActivity[target.slot] = 0.0f;

			target.kind = TargetKind::ButtonDrag;
			target.key = SDL_SCANCODE_UNKNOWN;
			return;
		}

		if(newTarget.kind == target.kind && newTarget.key == target.key)
			return;

		if(target.kind != TargetKind::ButtonDrag)
		{
			window->InjectKey(target.key, false);
			if(target.slot >= 0)
				slotActivity[target.slot] = 0.0f;
		}

		if(newTarget.kind == TargetKind::Button || newTarget.kind == TargetKind::Start ||
		   newTarget.kind == TargetKind::Back)
		{
			newTarget.lastPos = pos;
			newTarget.dragOrigin = pos;
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
			// Sliding onto empty space keeps holding the last button, like a real
			// finger resting on the edge of a pad would.
			if(mousePassthroughDown)
			{
				fingers.erase(it);
			}
			else
			{
				if(target.slot >= 0)
					slotActivity[target.slot] = 0.0f;
				target.kind = TargetKind::ButtonDrag;
				target.lastPos = pos;
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
		else if(target.kind == TargetKind::ButtonDrag)
		{
			// The key was already released when the finger left the control.
		}
		else if(target.kind == TargetKind::Button || target.kind == TargetKind::Start ||
				target.kind == TargetKind::Back)
		{
			window->InjectKey(target.key, false);
			if(target.slot >= 0)
				slotActivity[target.slot] = 0.0f;
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
				else if(t.kind == TargetKind::Button || t.kind == TargetKind::Start ||
						t.kind == TargetKind::Back)
				{
					window->InjectKey(t.key, false);
				}
			}
		}
		fingers.clear();
		mousePassthroughDown = false;
		knobAccum[0] = knobAccum[1] = 0.0f;
		knobAngle[0] = knobAngle[1] = 0.0f;
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
	/*
		The tickables draw inside the same nanovg frame and one of them may have
		left a transform or a clipping rectangle behind. The overlay hit tests in
		plain game pixels, so it has to draw in plain game pixels as well,
		otherwise the buttons appear somewhere else than they react.
	*/
	nvgResetTransform(vg);
	nvgFontFace(vg, "fallback");

	if(controlsHidden)
	{
		/*
			While hidden, only the toggle is drawn, faintly, so the playfield is
			completely unobstructed but the panel can always be brought back.
		*/
		DrawToggle(vg, layout.hidePos, layout.cornerSize, "SHOW", slotActivity[SlotHide]);
		nvgRestore(vg);
		return;
	}

	// Knobs. The needle shows how far the finger has turned them, which is the
	// only feedback an endless encoder can give.
	for(int i = 0; i < 2; i++)
	{
		const float alpha = ActivityAlpha(knobActivity[i]);
		const Vector2 p = layout.knobPos[i];
		const float r = layout.knobRadius;

		nvgBeginPath(vg);
		nvgCircle(vg, p.x, p.y, r);
		nvgFillColor(vg, nvgRGBAf(0.35f, 0.75f, 1.0f, alpha * 0.20f));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgCircle(vg, p.x, p.y, r);
		nvgStrokeColor(vg, nvgRGBAf(0.45f, 0.85f, 1.0f, alpha));
		nvgStrokeWidth(vg, r * 0.09f);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgCircle(vg, p.x, p.y, r * 0.13f);
		nvgFillColor(vg, nvgRGBAf(0.45f, 0.85f, 1.0f, alpha));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgMoveTo(vg, p.x, p.y);
		nvgLineTo(vg, p.x + std::sin(knobAngle[i]) * r * 0.70f,
					  p.y - std::cos(knobAngle[i]) * r * 0.70f);
		nvgStrokeColor(vg, nvgRGBAf(0.75f, 0.95f, 1.0f, alpha));
		nvgStrokeWidth(vg, r * 0.10f);
		nvgStroke(vg);
	}

	// BT buttons: squares, as on the console's button panel.
	for(int i = 0; i < 4; i++)
	{
		const int slot = SlotBT0 + i;
		const float alpha = ActivityAlpha(slotActivity[slot]);
		const Vector2 p = layout.btPos[i];
		const float s = layout.btSize;
		const float radius = s * 0.18f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, p.x - s * 0.5f, p.y - s * 0.5f, s, s, radius);
		nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha * 0.25f));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgRoundedRect(vg, p.x - s * 0.5f, p.y - s * 0.5f, s, s, radius);
		nvgStrokeColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha));
		nvgStrokeWidth(vg, s * 0.07f);
		nvgStroke(vg);

		nvgFontSize(vg, s * 0.42f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha + (1.0f - alpha) * 0.5f * slotActivity[slot]));
		nvgText(vg, p.x, p.y, kButtonLabels[i], nullptr);
	}

	// FX buttons: the wide bars at the bottom.
	for(int i = 0; i < 2; i++)
	{
		const int slot = SlotFX0 + i;
		const float alpha = ActivityAlpha(slotActivity[slot]);
		const Vector2 p = layout.fxPos[i];
		const Vector2 s = layout.fxSize;
		const float radius = s.y * 0.25f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, p.x - s.x * 0.5f, p.y - s.y * 0.5f, s.x, s.y, radius);
		nvgFillColor(vg, nvgRGBAf(1.0f, 0.85f, 0.25f, alpha * 0.25f));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgRoundedRect(vg, p.x - s.x * 0.5f, p.y - s.y * 0.5f, s.x, s.y, radius);
		nvgStrokeColor(vg, nvgRGBAf(1.0f, 0.85f, 0.25f, alpha));
		nvgStrokeWidth(vg, s.y * 0.09f);
		nvgStroke(vg);

		nvgFontSize(vg, s.y * 0.34f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBAf(1.0f, 0.9f, 0.5f, alpha + (1.0f - alpha) * 0.5f * slotActivity[slot]));
		nvgText(vg, p.x, p.y, kFxLabels[i], nullptr);
	}

	// Top row: panel toggle (left), Start (centre) and Back (right).
	DrawToggle(vg, layout.hidePos, layout.cornerSize, "HIDE", slotActivity[SlotHide]);
	DrawPentagonButton(vg, layout.startPos, layout.cornerSize, "START", slotActivity[SlotStart]);
	DrawPentagonButton(vg, layout.backPos, layout.cornerSize, "BACK", slotActivity[SlotBack]);

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
