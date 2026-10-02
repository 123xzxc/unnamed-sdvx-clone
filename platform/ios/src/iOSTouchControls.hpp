#pragma once

#include <Shared/Unique.hpp>

namespace Graphics { class Window; }

/*
	On-screen SDVX controller for iPadOS.

	The overlay does not implement its own gameplay input handling; it emulates the
	physical controller through the input devices the engine already supports:

	  * both knobs are injected as relative mouse motion, which is the analogue
	    laser device the game already ships (`InputDevice::Mouse`)
	  * the four BT and two FX buttons plus Start/Back are injected as the key
	    codes currently bound in the game config, so custom key mappings keep working

	Because every control goes through the normal input pipeline, replays, score
	upload and the input calibration screens all see ordinary input.
*/
class iOSTouchControls : public Unique
{
public:
	iOSTouchControls();
	~iOSTouchControls();

	// Subscribes to the window touch/event delegates. Safe to call once.
	void Init(Graphics::Window& window);
	void Cleanup();

	// Per frame housekeeping (screen idle timer, visual feedback animation).
	void Tick(float deltaTime);
	// Draws the control overlay. Must be called while a nanovg frame is active.
	void Render(float deltaTime);

	// Releases every key/mouse button the overlay is currently holding. Called
	// when the app loses focus (e.g. going to the background) so the game does not
	// resume with stuck inputs, and again during shutdown.
	void ReleaseAll();

private:
	class iOSTouchControls_Impl* m_impl;
};

// Global instance, created by Application when USC_IOS is defined.
extern iOSTouchControls* g_iosTouchControls;
