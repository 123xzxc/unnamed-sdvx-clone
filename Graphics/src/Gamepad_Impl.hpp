#pragma once
#include "Gamepad.hpp"

#include "SDL2/SDL_joystick.h"
#include "SDL2/SDL_gamecontroller.h"

namespace Graphics
{
	class Window;
	class Gamepad_Impl : public Gamepad
	{
	public:
		~Gamepad_Impl();
		bool Init(Graphics::Window* window, uint32 deviceIndex);

		// True when this device is exposed through SDL's standard controller
		// mapping (Xbox/MFi pads), false when it is a raw joystick.
		[[nodiscard]] bool IsUsingController() const;
		[[nodiscard]] SDL_GameController* GetController() const;
		[[nodiscard]] SDL_Joystick* GetJoystick() const;
		[[nodiscard]] SDL_JoystickID GetInstanceID() const;

		// Handles input events straight from the event loop
		void HandleInputEvent(uint32 buttonIndex, uint8 newState, int32 delta);
		void HandleAxisEvent(uint32 axisIndex, int16 newValue);
		void HandleHatEvent(uint32 hadIndex, uint8 newValue);

		class Window* m_window;
		uint32 m_deviceIndex;
		SDL_Joystick* m_joystick;
		/*
			SDL's GameController view of the same device.

			On iOS (and for Xbox pads in general) the raw joystick interface
			reports the physical buttons in an arbitrary order, so the default
			controller bindings did not line up with anything. The
			GameController interface remaps every supported pad to the standard
			Xbox layout, which is what the bindings in GameConfig assume. It is
			null for devices SDL has no mapping for; the raw joystick is used as
			a fallback in that case.
		*/
		SDL_GameController* m_controller;
		bool m_usingController;

		// Current state, indexed by the button/axis numbering that is exposed
		// through GetButton/GetAxis (that is, SDL_CONTROLLER_* when a mapping
		// exists, the raw joystick numbering otherwise).
		Vector<float> m_axisState;
		Vector<uint8> m_buttonStates;

		uint32 m_numButtons;
		uint32 m_numAxes;

		virtual bool GetButton(uint8 button) const override;
		virtual float GetAxis(uint8 idx) const override;
		virtual uint32 NumButtons() const override;
		virtual uint32 NumAxes() const override;
	};
}
