
/*
MIT License

Copyright (c) 2025 Gothic Multiplayer Team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "ZenGin/zGothicAPI.h"
#include "gothic-api/ZenGin/DirectX7/include/dinput.h"
#include "gothic-api/ZenGin/Gothic_II_Addon/API/zInput_Const.h"

struct MOUSEPOS {
  float m_fX;
  float m_fY;
};

enum {
  DIMOUSE_LEFTBUTTON = 0,
  DIMOUSE_RIGHTBUTTON = 1,
  DIMOUSE_MIDDLEBUTTON = 2,
  DIMOUSE_4BUTTON = 3,
  DIMOUSE_5BUTTON = 4,
  DIMOUSE_6BUTTON = 5,
  DIMOUSE_7BUTTON = 6,
  DIMOUSE_8BUTTON = 7
};

class CMouse {
public:
  static CMouse& GetInstance();

  void ClearKeyBuffer();
  void Update();
  void Render();

  MOUSEPOS GetCursorPosition() const;
  void GetRealCursorPosition(int& x, int& y);
  void SetCursorPosition(const MOUSEPOS& pos);
  void SetCursorPositionCenter();
  void Show();
  void Hide();

  bool KeyDown(int button) const;
  bool KeyClick(int button) const;

  void setSensitivity(float val);

  zCView* GetCursor() const {
    return m_pCursor;
  }
  bool IsVisible() const {
    return m_visibleCount > 0;
  }
  bool HasMoved() const {
    return m_hasMoved;
  }

  ~CMouse();

private:
  CMouse();

  void UpdateResolution();
  float ClampToView(float value) const;
  void EnsureDevice();
  void AcquireDevice();
  void UnacquireDevice();
  static LPDIRECTINPUTDEVICEA FetchGothicMouseDevice();

  zCView* m_pCursor;
  LPDIRECTINPUTDEVICEA m_pDIMouseDevice;
  DIMOUSESTATE2 m_state;
  DIMOUSESTATE2 m_prevState;
  float m_fSensitivity;
  float m_fCursorX;
  float m_fCursorY;
  bool m_bInverted;
  unsigned int m_visibleCount;
  bool m_cursorOnScreen;
  int m_iGothicResX;
  int m_iGothicResY;
  bool m_hasMoved;
};

void MouseLoop();
