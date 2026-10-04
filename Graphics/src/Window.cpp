#include "stdafx.h"
#include "Window.hpp"
#include "Image.hpp"
#include "Gamepad_Impl.hpp"
#include <Shared/Profiling.hpp>

static void GetDisplayBounds(Vector<Shared::Recti>& bounds)
{
	const int displayNum = SDL_GetNumVideoDisplays();
	if (displayNum <= 0) return;

	for (int monitorId = 0; monitorId < displayNum; ++monitorId)
	{
		SDL_Rect rect;
		if (SDL_GetDisplayBounds(monitorId, &rect) < 0) break;

		bounds.emplace_back(Shared::Recti{ {rect.x, rect.y}, {rect.w, rect.h} });
	}
}

namespace Graphics
{
	/* SDL Instance singleton */
	class SDL
	{
	protected:
		SDL()
		{
			SDL_SetMainReady();
			/*
				SDL_INIT_GAMECONTROLLER is required on iOS: MFi and Xbox pads are
				only exposed with the standard button/axis layout through SDL's
				GameController API. The raw joystick interface reports their
				physical buttons in an arbitrary order, so the game's controller
				bindings (which assume an Xbox style layout) did not match anything
				and the pad looked unrecognised.
			*/
			int r = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
			if (r != 0)
			{
				Logf("SDL_Init Failed: %s", Logger::Severity::Error, SDL_GetError());
				assert(false);
			}
		}

	public:
		~SDL()
		{
			SDL_Quit();
		}
		static SDL &Main()
		{
			static SDL sdl;
			return sdl;
		}
	};

	class Window_Impl
	{
	public:
		// Handle to outer class to send delegates
		Window &outer;

	public:
		Window_Impl(Window &outer, Vector2i size, uint8 sampleCount) : outer(outer)
		{
			ProfilerScope $("Creating Window");
			SDL::Main();

			m_clntSize = size;

#ifdef _DEBUG
			m_caption = L"USC-Game Debug";
#else
			m_caption = L"USC-Game";
#endif
			String titleUtf8 = Utility::ConvertToUTF8(m_caption);

			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, sampleCount);
			SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 2);
			SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

			m_window = SDL_CreateWindow(*titleUtf8, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
#ifdef USC_IOS
										/*
											The window is created in points and stays that way:
											SDL_WINDOW_ALLOW_HIGHDPI is deliberately not set.

											With it, the window size (points) and the framebuffer
											size (pixels) differ, and which of the two an event or
											a query reports depends on the driver and on the exact
											moment it is asked. The engine derives g_resolution,
											the viewport and the touch mapping from those values,
											and a single mismatched report made the game render
											into the lower left quarter of the screen.

											Rendering in points is always self consistent: the
											window size, the drawable size and the touch
											coordinates agree, and iOS simply scales the
											framebuffer up to the panel, which is what the
											engine's own render resolution setting already
											controls.
										*/
										m_clntSize.x, m_clntSize.y,
										SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
#else
										m_clntSize.x, m_clntSize.y, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
#endif
			assert(m_window);

			uint32 numJoysticks = SDL_NumJoysticks();
			if (numJoysticks == 0)
			{
				Log("No joysticks found", Logger::Severity::Warning);
			}
			else
			{
				Logf("Listing %d Joysticks:", Logger::Severity::Info, numJoysticks);
				for (uint32 i = 0; i < numJoysticks; i++)
				{
					SDL_Joystick *joystick = SDL_JoystickOpen(i);
					if (!joystick)
					{
						Logf("[%d] <failed to open>", Logger::Severity::Warning, i);
						continue;
					}
					String deviceName = SDL_JoystickName(joystick);

					Logf("[%d] \"%s\" (%d buttons, %d axes, %d hats)", Logger::Severity::Info,
						 i, deviceName, SDL_JoystickNumButtons(joystick), SDL_JoystickNumAxes(joystick), SDL_JoystickNumHats(joystick));

					SDL_JoystickClose(joystick);
				}
			}
		}

		~Window_Impl()
		{
			// Release gamepads
			for (auto it : m_gamepads)
			{
				it.second.reset();
			}

			SDL_DestroyWindow(m_window);
		}

		void SetWindowPos(const Vector2i &pos)
		{
			SDL_SetWindowPosition(m_window, pos.x, pos.y);
		}

		void SetWindowPosToCenter(int32 monitorId)
		{
			SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(monitorId), SDL_WINDOWPOS_CENTERED_DISPLAY(monitorId));
		}

		void ShowMessageBox(const String& title, const String& message, int severity)
		{
			uint32 flags;
			switch (severity)
			{
			case 0:
				flags = SDL_MESSAGEBOX_ERROR;
				break;
			case 1:
				flags = SDL_MESSAGEBOX_WARNING;
				break;
			default:
				flags = SDL_MESSAGEBOX_INFORMATION;
			}
			SDL_ShowSimpleMessageBox(flags, title.c_str(), message.c_str(), m_window);
		}

		bool ShowYesNoMessage(const String& title, const String& message)
		{
			const SDL_MessageBoxButtonData buttons[] =
				{
					{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "no"},
					{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "yes"},
				};
			const SDL_MessageBoxData messageboxdata =
				{
					SDL_MESSAGEBOX_INFORMATION,
					nullptr,
					*title,
					*message,
					SDL_arraysize(buttons),
					buttons,
					nullptr};
			int buttonid;
			if (SDL_ShowMessageBox(&messageboxdata, &buttonid) < 0)
			{
				Logf("Could not display message box for '%s'", Logger::Severity::Info, *message);
				return false;
			}
			return buttonid == 1;
		}

		Vector2i GetWindowPos() const
		{
			Vector2i res;
			SDL_GetWindowPosition(m_window, &res.x, &res.y);
			return res;
		}

		void SetWindowSize(const Vector2i &size)
		{
			SDL_SetWindowSize(m_window, size.x, size.y);
		}

		Vector2i GetWindowSize() const
		{
			Vector2i res;
			SDL_GetWindowSize(m_window, &res.x, &res.y);
			return res;
		}

		void SetVSync(int8 setting)
		{
			if (SDL_GL_SetSwapInterval(setting) == -1)
				Logf("Failed to set VSync: %s", Logger::Severity::Error, SDL_GetError());
		}

		void SetWindowStyle(WindowStyle style)
		{
		}

		/* input handling */
		void HandleKeyEvent(const SDL_Keysym &keySym, uint8 newState, int32 repeat, int32 delta)
		{
			const SDL_Scancode code = keySym.scancode;
			auto m = static_cast<SDL_Keymod>(keySym.mod);

			m_modKeys = ModifierKeys::None;

			if ((m & KMOD_ALT) != 0)
				(uint8 &)m_modKeys |= (uint8)ModifierKeys::Alt;
			if ((m & KMOD_CTRL) != 0)
				(uint8 &)m_modKeys |= (uint8)ModifierKeys::Ctrl;
			if ((m & KMOD_SHIFT) != 0)
				(uint8 &)m_modKeys |= (uint8)ModifierKeys::Shift;

			uint8 &currentState = m_keyStates[code];

			if (currentState != newState)
			{
				currentState = newState;
				if (newState == 1)
				{
					outer.OnKeyPressed.Call(code, delta);
				}
				else
				{
					outer.OnKeyReleased.Call(code, delta);
				}
			}
			if (currentState == 1)
			{
				outer.OnKeyRepeat.Call(code);
			}
		}

		/* Window show hide, positioning, etc.*/
		void Show() const
		{
			SDL_ShowWindow(m_window);
		}
		void Hide() const
		{
			SDL_HideWindow(m_window);
		}
		void SetCaption(const WString &cap)
		{
			m_caption = L"Window";
			String titleUtf8 = Utility::ConvertToUTF8(m_caption);
			SDL_SetWindowTitle(m_window, *titleUtf8);
		}

		void SetCursor(const Ref<class ImageRes>& image, Vector2i hotspot)
		{
#ifdef _WIN32
			if (currentCursor)
			{
				SDL_FreeCursor(currentCursor);
				currentCursor = nullptr;
			}
			if (image)
			{
				Vector2i size = image->GetSize();
				void *bits = image->GetBits();
				SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(bits, size.x, size.y, 32, size.x * 4,
															 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
				if (surf)
				{
					currentCursor = SDL_CreateColorCursor(surf, hotspot.x, hotspot.y);
				}
			}
			SDL_SetCursor(currentCursor);
#endif
			/// NOTE: Cursor transparency is broken on linux
		}

		// Update loop
		Timer t;
		bool Update()
		{
			static SDL_Event events[SIZE_EVENTS];
			int eventCount;

			SDL_PumpEvents();
			Uint32 tick = SDL_GetTicks();
			do
			{
				eventCount = SDL_PeepEvents(&events[0], SIZE_EVENTS, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
				// Don't use range loop since it could loop through previously processed events from the previous loop
				for (int i = 0; i < eventCount; ++i)
				{
					HandleEvent(events[i], tick);
				}
			}
			while (eventCount == SIZE_EVENTS);

			return !m_closed;
		}

		/*
			Dispatches one SDL event to the window delegates.

			Both the message pump above and InjectEvent() end up here, so a synthetic
			key press coming from the iOS touch controls is handled exactly like a key
			press coming from a real keyboard.
		*/
		void HandleEvent(const SDL_Event& evt, uint32 tick)
		{
			int32 delta = tick - evt.common.timestamp;
			{
					if (evt.type == SDL_EventType::SDL_KEYDOWN)
					{
						HandleKeyEvent(evt.key.keysym, 1, evt.key.repeat, delta);
					}
					else if (evt.type == SDL_EventType::SDL_KEYUP)
					{
						HandleKeyEvent(evt.key.keysym, 0, 0, delta);
					}
					else if (evt.type == SDL_EventType::SDL_JOYBUTTONDOWN)
					{
						Gamepad_Impl **gp = m_joystickMap.Find(evt.jbutton.which);
						if (gp && !gp[0]->IsUsingController())
							gp[0]->HandleInputEvent(evt.jbutton.button, true, delta);
					}
					else if (evt.type == SDL_EventType::SDL_JOYBUTTONUP)
					{
						Gamepad_Impl **gp = m_joystickMap.Find(evt.jbutton.which);
						if (gp && !gp[0]->IsUsingController())
							gp[0]->HandleInputEvent(evt.jbutton.button, false, delta);
					}
					else if (evt.type == SDL_EventType::SDL_JOYAXISMOTION)
					{
						Gamepad_Impl **gp = m_joystickMap.Find(evt.jaxis.which);
						if (gp && !gp[0]->IsUsingController())
							gp[0]->HandleAxisEvent(evt.jaxis.axis, evt.jaxis.value);
					}
					else if (evt.type == SDL_EventType::SDL_JOYHATMOTION)
					{
						Gamepad_Impl **gp = m_joystickMap.Find(evt.jhat.which);
						if (gp)
							gp[0]->HandleHatEvent(evt.jhat.hat, evt.jhat.value);
					}
					/*
						When a device is open through the GameController interface SDL
						reports both joystick and controller events, and the button and
						axis numbers in the joystick ones are the raw hardware ones.
						The controller events carry the standardised numbering that the
						game's bindings expect, so only they are forwarded for those
						devices (see the IsUsingController() guards above).
					*/
					else if (evt.type == SDL_EventType::SDL_CONTROLLERBUTTONDOWN)
					{
						Gamepad_Impl **gp = m_joystickMap.Find(evt.cbutton.which);
						if (gp)
							gp[0]->HandleInputEvent(evt.cbutton.button, true, delta);
					}
					else if (evt.type == SDL_EventType::SDL_CONTROLLERBUTTONUP)
					{
						Gamepad_Impl **gp = m_joystickMap.Find(evt.cbutton.which);
						if (gp)
							gp[0]->HandleInputEvent(evt.cbutton.button, false, delta);
					}
					else if (evt.type == SDL_EventType::SDL_CONTROLLERAXISMOTION)
					{
						Gamepad_Impl **gp = m_joystickMap.Find(evt.caxis.which);
						if (gp)
							gp[0]->HandleAxisEvent(evt.caxis.axis, evt.caxis.value);
					}
					/*
						Pads are commonly connected after the game has started
						(especially over Bluetooth on iOS), so the settings screen
						is told to re-scan its device list.
					*/
					else if (evt.type == SDL_EventType::SDL_CONTROLLERDEVICEADDED ||
							 evt.type == SDL_EventType::SDL_CONTROLLERDEVICEREMOVED ||
							 evt.type == SDL_EventType::SDL_JOYDEVICEADDED ||
							 evt.type == SDL_EventType::SDL_JOYDEVICEREMOVED)
					{
						outer.OnGamepadListChanged.Call();
					}
					else if (evt.type == SDL_EventType::SDL_MOUSEBUTTONDOWN)
					{
						switch (evt.button.button)
						{
							case SDL_BUTTON_LEFT:
								outer.OnMousePressed.Call(MouseButton::Left);
								break;
							case SDL_BUTTON_MIDDLE:
								outer.OnMousePressed.Call(MouseButton::Middle);
								break;
							case SDL_BUTTON_RIGHT:
								outer.OnMousePressed.Call(MouseButton::Right);
								break;
						}
					}
					else if (evt.type == SDL_EventType::SDL_MOUSEBUTTONUP)
					{
						switch (evt.button.button)
						{
							case SDL_BUTTON_LEFT:
								outer.OnMouseReleased.Call(MouseButton::Left);
								break;
							case SDL_BUTTON_MIDDLE:
								outer.OnMouseReleased.Call(MouseButton::Middle);
								break;
							case SDL_BUTTON_RIGHT:
								outer.OnMouseReleased.Call(MouseButton::Right);
								break;
						}
					}
					else if (evt.type == SDL_EventType::SDL_MOUSEWHEEL)
					{
						if (evt.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
						{
							outer.OnMouseScroll.Call(evt.wheel.y);
						}
						else
						{
							outer.OnMouseScroll.Call(-evt.wheel.y);
						}
					}
					else if (evt.type == SDL_EventType::SDL_MOUSEMOTION)
					{
						outer.OnMouseMotion.Call(evt.motion.xrel, evt.motion.yrel);
					}
					else if (evt.type == SDL_EventType::SDL_QUIT)
					{
						m_closed = true;
					}
					else if (evt.type == SDL_EventType::SDL_WINDOWEVENT)
					{
						if (evt.window.windowID == SDL_GetWindowID(m_window))
						{
							if (evt.window.event == SDL_WindowEventID::SDL_WINDOWEVENT_SIZE_CHANGED)
							{
								Vector2i newSize(evt.window.data1, evt.window.data2);
								outer.OnResized.Call(newSize);
							}
							else if (evt.window.event == SDL_WindowEventID::SDL_WINDOWEVENT_FOCUS_GAINED)
							{
								outer.OnFocusChanged.Call(true);
							}
							else if (evt.window.event == SDL_WindowEventID::SDL_WINDOWEVENT_FOCUS_LOST)
							{
								outer.OnFocusChanged.Call(false);
							}
							else if (evt.window.event == SDL_WindowEventID::SDL_WINDOWEVENT_MOVED)
							{
								Vector2i newPos(evt.window.data1, evt.window.data2);
								outer.OnMoved.Call(newPos);
							}
						}
					}
					else if (evt.type == SDL_EventType::SDL_TEXTINPUT)
					{
						outer.OnTextInput.Call(evt.text.text);
					}
					else if (evt.type == SDL_EventType::SDL_TEXTEDITING)
					{
						SDL_Rect scr;
						SDL_GetWindowPosition(m_window, &scr.x, &scr.y);
						SDL_GetWindowSize(m_window, &scr.w, &scr.h);
						SDL_SetTextInputRect(&scr);

						m_textComposition.composition = evt.edit.text;
						m_textComposition.cursor = evt.edit.start;
						m_textComposition.selectionLength = evt.edit.length;
						outer.OnTextComposition.Call(m_textComposition);
					}
					else if (evt.type == SDL_EventType::SDL_DROPFILE)
					{
						const char* file = evt.drop.file;
						if (file != nullptr)
						{
							outer.OnFileDropped.Call(evt.drop.file);
							SDL_free(evt.drop.file);
						}
					}
					else if (evt.type == SDL_EventType::SDL_FINGERDOWN)
					{
						outer.OnFingerDown.Call((int32)evt.tfinger.fingerId,
							Vector2(evt.tfinger.x, evt.tfinger.y), evt.tfinger.pressure);
					}
					else if (evt.type == SDL_EventType::SDL_FINGERMOTION)
					{
						outer.OnFingerMotion.Call((int32)evt.tfinger.fingerId,
							Vector2(evt.tfinger.x, evt.tfinger.y), evt.tfinger.pressure);
					}
					else if (evt.type == SDL_EventType::SDL_FINGERUP)
					{
						outer.OnFingerUp.Call((int32)evt.tfinger.fingerId,
							Vector2(evt.tfinger.x, evt.tfinger.y), evt.tfinger.pressure);
					}
					outer.OnAnyEvent.Call(evt);
					m_lastEventTick = tick;
			}
		}

		// Injects a synthetic event. Used by the on-screen controls on iPadOS.
		void InjectEvent(const SDL_Event& evt)
		{
			SDL_Event copy = evt;
			const uint32 tick = SDL_GetTicks();
			// Handlers use the delta between events, so make the injected event look
			// like it arrived right after the previous one.
			copy.common.timestamp = m_lastEventTick;
			HandleEvent(copy, tick);
		}

		void SetWindowed(const Vector2i& pos, const Vector2i& size)
		{
			SDL_SetWindowFullscreen(m_window, 0);
			SDL_RestoreWindow(m_window);

			SetWindowSize(size);

			SDL_SetWindowResizable(m_window, SDL_TRUE);
			SDL_SetWindowBordered(m_window, SDL_TRUE);

			SetWindowPos(pos);

			m_fullscreen = false;
		}

		void SetWindowedFullscreen(int32 monitorId)
		{
			if (monitorId == -1)
			{
				monitorId = SDL_GetWindowDisplayIndex(m_window);
			}

			SDL_DisplayMode dm;
			SDL_GetDesktopDisplayMode(monitorId, &dm);

			SDL_Rect bounds;
			SDL_GetDisplayBounds(monitorId, &bounds);

			SDL_RestoreWindow(m_window);
			SDL_SetWindowSize(m_window, dm.w, dm.h);
			SDL_SetWindowPosition(m_window, bounds.x, bounds.y);
			SDL_SetWindowResizable(m_window, SDL_FALSE);

			m_fullscreen = true;
		}

		void SetFullscreen(int32 monitorId, const Vector2i& res)
		{
			if (monitorId == -1)
			{
				monitorId = SDL_GetWindowDisplayIndex(m_window);
			}

			SDL_DisplayMode dm;
			SDL_GetDesktopDisplayMode(monitorId, &dm);

			if (res.x != -1)
			{
				dm.w = res.x;
			}

			if (res.y != -1)
			{
				dm.h = res.y;
			}

			// move to correct display
			SetWindowPosToCenter(monitorId);

			SDL_SetWindowDisplayMode(m_window, &dm);
			SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN);

			m_fullscreen = true;
		}

		void SetPosAndShape(const Window::PosAndShape& posAndShape, bool ensureInBound)
		{
			int32 monitorId = posAndShape.monitorId;
			if (monitorId == -1)
			{
				monitorId = SDL_GetWindowDisplayIndex(m_window);
			}

			if (ensureInBound)
			{
				// Adjust the monitor to use, if the monitor does not exist.
				SDL_DisplayMode dm;
				if (SDL_GetDesktopDisplayMode(monitorId, &dm) < 0)
				{
					Logf("Monitor %d is not available; using 0 instead", Logger::Severity::Warning, monitorId);
					monitorId = 0;
				}
			}

			switch (posAndShape.mode)
			{
			case Window::PosAndShape::Mode::Windowed:
			{
				Vector2i windowPos = posAndShape.windowPos;
				Vector2i windowSize = posAndShape.windowSize;

				SetWindowed(windowPos, windowSize);

				// Adjust window position and size, if the window can't be fit into the display region.
				if (ensureInBound)
				{
					int borderTop = 0, borderLeft = 0, borderBottom = 0, borderRight = 0;
					SDL_GetWindowBordersSize(m_window, &borderTop, &borderLeft, &borderBottom, &borderRight);

					Shared::Recti windowRect(windowPos - Vector2i{ borderLeft, borderTop }, windowSize + Vector2i{ borderLeft+borderRight, borderTop+borderBottom });

					Vector<Shared::Recti> bounds;
					GetDisplayBounds(bounds);

					if (bounds.empty())
					{
						break;
					}

					Logf("Adjusting window size for %u windows...", Logger::Severity::Info, bounds.size());

					if (monitorId >= static_cast<int>(bounds.size()))
					{
						monitorId = 0;
					}

					bool foundContained = bounds[monitorId].Contains(windowRect);
					if (foundContained)
					{
						break;
					}

					bool foundLargeEnough = bounds[monitorId].NotSmallerThan(windowRect.size);
					if (!foundLargeEnough)
					{

						for (int i = 0; i < static_cast<int>(bounds.size()); ++i)
						{
							if (bounds[i].Contains(windowRect))
							{
								foundContained = true;
								break;
							}

							if (bounds[i].NotSmallerThan(windowRect.size))
							{
								foundLargeEnough = true;
								monitorId = i;
								break;
							}
						}
					}

					if (foundContained)
					{
						break;
					}

					// Optimally, this should be set to the largest rectangle inside `bounds intersect windowRect`.
					Shared::Recti adjustedRect = bounds[monitorId];
					if (foundLargeEnough)
					{
						adjustedRect.size = windowRect.size;
					}

					adjustedRect.pos = bounds[monitorId].pos + (bounds[monitorId].size - adjustedRect.size) / 2;

					windowSize = adjustedRect.size - Vector2i{ borderLeft+borderRight, borderTop+borderBottom };

					SetWindowSize(windowSize);
					SetWindowPosToCenter(monitorId);
				}
			}
				break;
			case Window::PosAndShape::Mode::WindowedFullscreen:
				SetWindowedFullscreen(monitorId);
				break;
			case Window::PosAndShape::Mode::Fullscreen:
				SetFullscreen(monitorId, posAndShape.fullscreenSize);
				break;
			}
		}

		inline bool IsFullscreen() const { return m_fullscreen; }

		SDL_Window *m_window;

		SDL_Cursor *currentCursor = nullptr;

		// Window Input State
		Map<SDL_Scancode, uint8> m_keyStates;
		ModifierKeys m_modKeys = ModifierKeys::None;

		// Last requested relative mouse mode. On platforms without it (iOS) the
		// request is remembered instead of being forwarded to SDL, so callers that
		// poll GetRelativeMouseMode() do not retry every frame.
		bool m_relativeMouseMode = false;

		// Mouse position the application sees, used on platforms where SDL's own
		// mouse state is not updated (iOS, where touches are injected by the
		// on-screen controller instead of by SDL's touch-to-mouse emulation).
		Vector2i m_mousePos;

		// Gamepad input
		Map<int32, Ref<Gamepad_Impl>> m_gamepads;
		Map<SDL_JoystickID, Gamepad_Impl *> m_joystickMap;

		// Text input / IME stuff
		TextComposition m_textComposition;

		// Various window state
		bool m_active = true;
		bool m_closed = false;

		bool m_fullscreen = false;

		uint32 m_style;
		Vector2i m_clntSize;
		WString m_caption;
		uint32 m_lastEventTick = 0;
	};

	Window::Window(Vector2i size, uint8 samplecount)
	{
		m_impl = new Window_Impl(*this, size, samplecount);
	}
	Window::~Window()
	{
		delete m_impl;
	}
	void Window::Show()
	{
		m_impl->Show();
	}
	void Window::Hide()
	{
		m_impl->Hide();
	}
	bool Window::Update()
	{
		return m_impl->Update();
	}
	void *Window::Handle()
	{
		return m_impl->m_window;
	}
	void Window::SetCaption(const WString &cap)
	{
		m_impl->SetCaption(cap);
	}
	void Window::Close()
	{
		m_impl->m_closed = true;
	}

	Vector2i Window::GetMousePos()
	{
#ifdef USC_IOS
		// SDL never updates its mouse state on a touch screen, so the position
		// injected by the on-screen controls is used instead.
		return m_impl->m_mousePos;
#else
		Vector2i res;
		SDL_GetMouseState(&res.x, &res.y);
		return res;
#endif
	}
	void Window::SetCursor(const Ref<class ImageRes>& image, Vector2i hotspot /*= Vector2i(0,0)*/)
	{
		m_impl->SetCursor(image, hotspot);
	}
	void Window::SetCursorVisible(bool visible)
	{
		SDL_ShowCursor(visible);
	}

	void Window::SetWindowStyle(WindowStyle style)
	{
		m_impl->SetWindowStyle(style);
	}

	Vector2i Window::GetWindowPos() const
	{
		return m_impl->GetWindowPos();
	}

	Vector2i Window::GetWindowSize() const
	{
		return m_impl->GetWindowSize();
	}

	void Window::SetVSync(int8 setting)
	{
		m_impl->SetVSync(setting);
	}

	void Window::SetPosAndShape(const PosAndShape& posAndShape, bool ensureInBound)
	{
		m_impl->SetPosAndShape(posAndShape, ensureInBound);
	}

	bool Window::IsFullscreen() const
	{
		return m_impl->IsFullscreen();
	}

	int Window::GetDisplayIndex() const
	{
		return SDL_GetWindowDisplayIndex(m_impl->m_window);
	}

	bool Window::IsKeyPressed(SDL_Scancode key) const
	{
		return m_impl->m_keyStates[key] > 0;
	}

	Graphics::ModifierKeys Window::GetModifierKeys() const
	{
		return m_impl->m_modKeys;
	}

	bool Window::IsActive() const
	{
		return SDL_GetWindowFlags(m_impl->m_window) & SDL_WindowFlags::SDL_WINDOW_INPUT_FOCUS;
	}

	void Window::StartTextInput()
	{
		SDL_StartTextInput();
	}
	void Window::StopTextInput()
	{
		SDL_StopTextInput();
	}
	const Graphics::TextComposition &Window::GetTextComposition() const
	{
		return m_impl->m_textComposition;
	}

	void Window::ShowMessageBox(const String& title, const String& message, int severity)
	{
		m_impl->ShowMessageBox(title, message, severity);
	}

	bool Window::ShowYesNoMessage(const String& title, const String& message)
	{
		return m_impl->ShowYesNoMessage(title, message);
	}

	String Window::GetClipboard() const
	{
		char *utf8Clipboard = SDL_GetClipboardText();
		String ret(utf8Clipboard);
		SDL_free(utf8Clipboard);

		return ret;
	}

	int32 Window::GetNumGamepads() const
	{
		return SDL_NumJoysticks();
	}
	Vector<String> Window::GetGamepadDeviceNames() const
	{
		Vector<String> ret;
		uint32 numJoysticks = SDL_NumJoysticks();
		for (uint32 i = 0; i < numJoysticks; i++)
		{
			SDL_Joystick *joystick = SDL_JoystickOpen(i);
			if (!joystick)
			{
				continue;
			}
			String deviceName = SDL_JoystickName(joystick);
			ret.Add(deviceName);

			SDL_JoystickClose(joystick);
		}
		return ret;
	}

	Ref<Gamepad> Window::OpenGamepad(int32 deviceIndex)
	{
		Ref<Gamepad_Impl> *openGamepad = m_impl->m_gamepads.Find(deviceIndex);
		if (openGamepad)
			return Utility::CastRef<Gamepad_Impl, Gamepad>(*openGamepad);
		Ref<Gamepad_Impl> newGamepad;

		auto *gamepadImpl = new Gamepad_Impl();
		// Try to initialize new device
		if (gamepadImpl->Init(this, deviceIndex))
		{
			newGamepad = Ref<Gamepad_Impl>(gamepadImpl);

			// Receive joystick events
			SDL_JoystickEventState(SDL_ENABLE);
		}
		else
		{
			delete gamepadImpl;
		}
		if (newGamepad)
		{
			m_impl->m_gamepads.Add(deviceIndex, newGamepad);
			/*
				Both joystick and controller events identify the device by this
				instance id, so the map is keyed on it either way.
			*/
			const SDL_JoystickID instanceId = gamepadImpl->GetInstanceID();
			if (instanceId >= 0)
				m_impl->m_joystickMap.Add(instanceId, gamepadImpl);
		}
		return Utility::CastRef<Gamepad_Impl, Gamepad>(newGamepad);
	}

	void Window::SetMousePos(const Vector2i &pos)
	{
#ifdef USC_IOS
		// iOS has no mouse cursor to warp.
		(void)pos;
#else
		SDL_WarpMouseInWindow(m_impl->m_window, pos.x, pos.y);
#endif
	}

	void Window::SetRelativeMouseMode(bool enabled)
	{
#ifdef USC_IOS
		// Not supported by iOS. Remembering the request keeps Input::Update, which
		// enables the mode while the mouse is locked during gameplay, from calling
		// this every single frame (and logging a failure each time).
		m_impl->m_relativeMouseMode = enabled;
#else
		if (SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) != 0)
			Logf("SetRelativeMouseMode failed: %s", Logger::Severity::Warning, SDL_GetError());
		m_impl->m_relativeMouseMode = enabled;
#endif
	}

	uint32 Window::GetIdleTimsMs() {
		return SDL_GetTicks() - m_impl->m_lastEventTick;
	}

	bool Window::GetRelativeMouseMode()
	{
#ifdef USC_IOS
		return m_impl->m_relativeMouseMode;
#else
		return SDL_GetRelativeMouseMode() == SDL_TRUE;
#endif
	}

	void Window::InjectEvent(const SDL_Event& evt)
	{
		m_impl->InjectEvent(evt);
	}

	void Window::InjectKey(SDL_Scancode key, bool pressed)
	{
		SDL_Event evt;
		SDL_memset(&evt, 0, sizeof(SDL_Event));
		evt.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
		evt.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
		evt.key.repeat = 0;
		evt.key.keysym.scancode = key;
		evt.key.keysym.sym = SDL_GetKeyFromScancode(key);
		evt.key.keysym.mod = KMOD_NONE;
		m_impl->InjectEvent(evt);
	}

	void Window::InjectMouseMotion(int32 x, int32 y)
	{
		m_impl->m_mousePos += Vector2i(x, y);

		SDL_Event evt;
		SDL_memset(&evt, 0, sizeof(SDL_Event));
		evt.type = SDL_MOUSEMOTION;
		evt.motion.xrel = x;
		evt.motion.yrel = y;
		m_impl->InjectEvent(evt);
	}

	void Window::InjectMousePosition(int32 x, int32 y)
	{
		m_impl->m_mousePos = Vector2i(x, y);

		SDL_Event evt;
		SDL_memset(&evt, 0, sizeof(SDL_Event));
		evt.type = SDL_MOUSEMOTION;
		evt.motion.x = x;
		evt.motion.y = y;
		m_impl->InjectEvent(evt);
	}

	void Window::InjectMouseButton(MouseButton button, bool pressed)
	{
		SDL_Event evt;
		SDL_memset(&evt, 0, sizeof(SDL_Event));
		evt.type = pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		evt.button.state = pressed ? SDL_PRESSED : SDL_RELEASED;
		evt.button.clicks = 1;
		/*
			The UI backends read the cursor position from the button event itself
			(evt.button.x/y), not from the last motion event: NanoVG's Nuklear SDL
			backend does nk_input_button(ctx, ..., evt->button.x, evt->button.y).
			Injected button events that leave those at zero make every tap look
			like a click in the top left corner, so the settings screen could not
			be used with the touch panel. The position is taken from the tracked
			cursor, which InjectMousePosition() keeps up to date.
		*/
		evt.button.x = m_impl->m_mousePos.x;
		evt.button.y = m_impl->m_mousePos.y;
		switch(button)
		{
		case MouseButton::Left:
			evt.button.button = SDL_BUTTON_LEFT;
			break;
		case MouseButton::Middle:
			evt.button.button = SDL_BUTTON_MIDDLE;
			break;
		case MouseButton::Right:
			evt.button.button = SDL_BUTTON_RIGHT;
			break;
		default:
			evt.button.button = SDL_BUTTON_LEFT;
			break;
		}
		m_impl->InjectEvent(evt);
	}
} // namespace Graphics

namespace Graphics
{
	ImplementBitflagEnum(ModifierKeys);
}
