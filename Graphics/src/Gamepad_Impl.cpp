#include "stdafx.h"
#include "Gamepad_Impl.hpp"
#include "Window.hpp"

namespace Graphics
{

	Gamepad_Impl::~Gamepad_Impl()
	{
		if(m_controller)
			SDL_GameControllerClose(m_controller);
		else if(m_joystick)
			SDL_JoystickClose(m_joystick);
	}
	bool Gamepad_Impl::Init(Window* window, uint32 deviceIndex)
	{
		Logf("Trying to open joystick %d", Logger::Severity::Info, deviceIndex);

		m_window = window;
		m_deviceIndex = deviceIndex;

		/*
			Prefer the GameController interface. On iOS a connected Xbox or MFi
			pad is only usable through it: the raw joystick button numbering does
			not match the standard layout the game's bindings are written
			against, so the pad appeared to be unrecognised (and the menu still
			needed the touch buttons).
		*/
		if(SDL_IsGameController(deviceIndex))
		{
			m_controller = SDL_GameControllerOpen(deviceIndex);
			if(m_controller)
			{
				m_joystick = SDL_GameControllerGetJoystick(m_controller);
				m_usingController = true;
			}
			else
			{
				Logf("Failed to open game controller %d: %s", Logger::Severity::Warning,
					 deviceIndex, SDL_GetError());
			}
		}

		if(!m_controller)
		{
			// No mapping: fall back to the raw device so unusual hardware still
			// works with custom bindings.
			m_joystick = SDL_JoystickOpen(deviceIndex);
			m_usingController = false;
		}

		if(!m_joystick)
		{
			Logf("Failed to open joystick %d", Logger::Severity::Error, deviceIndex);
			return false;
		}

		// Size the state arrays for the numbering that GetButton/GetAxis use.
		if(m_usingController)
		{
			m_numButtons = SDL_CONTROLLER_BUTTON_MAX;
			m_numAxes = SDL_CONTROLLER_AXIS_MAX;
		}
		else
		{
			m_numButtons = SDL_JoystickNumButtons(m_joystick);
			m_numAxes = SDL_JoystickNumAxes(m_joystick);
		}

		for(uint32 i = 0; i < m_numButtons; i++)
			m_buttonStates.Add(0);
		for(uint32 i = 0; i < m_numAxes; i++)
			m_axisState.Add(0.0f);

		String deviceName = SDL_JoystickName(m_joystick);
		Logf("Joystick device \"%s\" opened with %d buttons and %d axes%s", Logger::Severity::Info,
			deviceName, m_numButtons, m_numAxes, m_usingController ? " (game controller mapping)" : "");

		return true;
	}

	bool Gamepad_Impl::IsUsingController() const
	{
		return m_usingController;
	}

	SDL_GameController* Gamepad_Impl::GetController() const
	{
		return m_controller;
	}

	SDL_Joystick* Gamepad_Impl::GetJoystick() const
	{
		return m_joystick;
	}

	SDL_JoystickID Gamepad_Impl::GetInstanceID() const
	{
		return m_joystick ? SDL_JoystickInstanceID(m_joystick) : -1;
	}



	void Gamepad_Impl::HandleInputEvent(uint32 buttonIndex, uint8 newState, int32 delta)
	{
		// SDL_JOY* events keep arriving alongside the controller ones (for the
		// same device), and their raw numbering can exceed what is tracked here.
		if(buttonIndex >= m_buttonStates.size())
			return;
		m_buttonStates[buttonIndex] = newState;
		if(newState != 0)
			OnButtonPressed.Call(buttonIndex, delta);
		else
			OnButtonReleased.Call(buttonIndex, delta);
	}
	void Gamepad_Impl::HandleAxisEvent(uint32 axisIndex, int16 newValue)
	{
		if(axisIndex >= m_axisState.size())
			return;
		m_axisState[axisIndex] = (float)newValue / (float)0x7fff;
	}
	void Gamepad_Impl::HandleHatEvent(uint32 hadIndex, uint8 newValue)
	{
		// TODO Maybe use this if required
	}

	bool Gamepad_Impl::GetButton(uint8 button) const
	{
		if(button >= m_buttonStates.size())
			return false;
		return m_buttonStates[button] != 0;
	}
	float Gamepad_Impl::GetAxis(uint8 idx) const
	{
		if(idx >= m_axisState.size())
			return 0.0f;
		return m_axisState[idx];
	}
	uint32 Gamepad_Impl::NumButtons() const
	{
		return (uint32)m_buttonStates.size();
	}
	uint32 Gamepad_Impl::NumAxes() const
	{
		return (uint32)m_axisState.size();
	}
}
