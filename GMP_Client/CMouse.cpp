
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
#include "CMouse.h"

#include <cstring>

namespace {
constexpr float kViewMax = 8192.0f;
constexpr float kViewMin = 0.0f;
}  // namespace

LPDIRECTINPUTDEVICEA CMouse::FetchGothicMouseDevice() {
  return *reinterpret_cast<LPDIRECTINPUTDEVICEA*>(0x008D1D70);
}

CMouse& CMouse::GetInstance() {
  static CMouse instance;
  return instance;
}

CMouse::CMouse()
    : m_pCursor(new zCView(0, 0, 8192, 8192, VIEW_ITEM)),
      m_pDIMouseDevice(nullptr),
      m_state{},
      m_prevState{},
      m_fSensitivity(5.0f),
      m_fCursorX(kViewMax / 2.0f),
      m_fCursorY(kViewMax / 2.0f),
      m_bInverted(false),
      m_visibleCount(0),
      m_cursorOnScreen(false),
      m_iGothicResX(320),
      m_iGothicResY(258),
      m_hasMoved(false) {
  if (m_pCursor) {
    m_pCursor->SetSize(200, 200);
    m_pCursor->InsertBack("LU.TGA");
  }

  UpdateResolution();
  ClearKeyBuffer();
  EnsureDevice();
}

CMouse::~CMouse() {
  if (m_pCursor) {
    zCView* screenView = Gothic_II_Addon::screen;
    if (m_cursorOnScreen && screenView) {
      screenView->RemoveItem(m_pCursor);
    }
    delete m_pCursor;
    m_pCursor = nullptr;
  }

  UnacquireDevice();
  m_pDIMouseDevice = nullptr;
}

void CMouse::UpdateResolution() {
  if (!zoptions)
    return;

  m_iGothicResX = zoptions->ReadInt(zOPT_SEC_VIDEO, "zVidResFullscreenX", m_iGothicResX);
  m_iGothicResY = zoptions->ReadInt(zOPT_SEC_VIDEO, "zVidResFullscreenY", m_iGothicResY);
}

float CMouse::ClampToView(float value) const {
  if (value < kViewMin)
    return kViewMin;
  if (value > kViewMax)
    return kViewMax;
  return value;
}

void CMouse::EnsureDevice() {
  if (!m_pDIMouseDevice) {
    m_pDIMouseDevice = FetchGothicMouseDevice();
  }
}

void CMouse::AcquireDevice() {
  EnsureDevice();
  if (!m_pDIMouseDevice)
    return;

  HRESULT hr = m_pDIMouseDevice->Acquire();
  if (FAILED(hr) && (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)) {
    m_pDIMouseDevice->Acquire();
  }
}

void CMouse::UnacquireDevice() {
  if (m_pDIMouseDevice) {
    m_pDIMouseDevice->Unacquire();
  }
}

void CMouse::ClearKeyBuffer() {
  std::memset(&m_state, 0, sizeof(m_state));
  std::memset(&m_prevState, 0, sizeof(m_prevState));
  m_hasMoved = false;
}

void CMouse::Update() {
  EnsureDevice();
  if (!m_pDIMouseDevice) {
    ClearKeyBuffer();
    return;
  }

  m_prevState = m_state;

  HRESULT hr = m_pDIMouseDevice->GetDeviceState(sizeof(DIMOUSESTATE2), &m_state);
  if (FAILED(hr)) {
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
      AcquireDevice();
      hr = m_pDIMouseDevice->GetDeviceState(sizeof(DIMOUSESTATE2), &m_state);
    }

    if (FAILED(hr)) {
      std::memset(&m_state, 0, sizeof(m_state));
    }
  }

  if (!IsVisible()) {
    m_hasMoved = false;
    return;
  }

  const float deltaX = static_cast<float>(m_state.lX) * m_fSensitivity;
  const float deltaYRaw = static_cast<float>(m_state.lY) * m_fSensitivity;
  const float deltaY = m_bInverted ? -deltaYRaw : deltaYRaw;

  m_hasMoved = deltaX != 0.0f || deltaYRaw != 0.0f;

  m_fCursorX = ClampToView(m_fCursorX + deltaX);
  m_fCursorY = ClampToView(m_fCursorY + deltaY);
}

void CMouse::Render() {
  if (!IsVisible() || !m_pCursor)
    return;

  m_pCursor->SetPos(static_cast<int>(m_fCursorX), static_cast<int>(m_fCursorY));
  m_pCursor->Top();
}

MOUSEPOS CMouse::GetCursorPosition() const {
  return {m_fCursorX, m_fCursorY};
}

void CMouse::GetRealCursorPosition(int& x, int& y) {
  UpdateResolution();

  const float percentX = m_fCursorX / kViewMax;
  const float percentY = m_fCursorY / kViewMax;
  x = static_cast<int>(percentX * static_cast<float>(m_iGothicResX));
  y = static_cast<int>(percentY * static_cast<float>(m_iGothicResY));
}

void CMouse::SetCursorPosition(const MOUSEPOS& pos) {
  m_fCursorX = ClampToView(pos.m_fX);
  m_fCursorY = ClampToView(pos.m_fY);
  m_hasMoved = false;
}

void CMouse::SetCursorPositionCenter() {
  m_fCursorX = kViewMax / 2.0f;
  m_fCursorY = kViewMax / 2.0f;
  m_hasMoved = false;
}

void CMouse::Show() {
  if (m_visibleCount == 0) {
    UpdateResolution();
    SetCursorPositionCenter();
    ClearKeyBuffer();
    AcquireDevice();

    zCView* screenView = Gothic_II_Addon::screen;
    if (m_pCursor && screenView) {
      screenView->InsertItem(m_pCursor);
      m_cursorOnScreen = true;
    }
  }

  ++m_visibleCount;
}

void CMouse::Hide() {
  if (m_visibleCount == 0)
    return;

  --m_visibleCount;
  if (m_visibleCount == 0) {
    zCView* screenView = Gothic_II_Addon::screen;
    if (m_pCursor && screenView && m_cursorOnScreen) {
      screenView->RemoveItem(m_pCursor);
      m_cursorOnScreen = false;
    }

    UnacquireDevice();
  }
}

bool CMouse::KeyDown(int button) const {
  if (button < 0 || button >= 8)
    return false;

  return (m_state.rgbButtons[button] & 0x80) != 0;
}

bool CMouse::KeyClick(int button) const {
  if (button < 0 || button >= 8)
    return false;

  const bool current = (m_state.rgbButtons[button] & 0x80) != 0;
  const bool previous = (m_prevState.rgbButtons[button] & 0x80) != 0;
  return current && !previous;
}

void CMouse::setSensitivity(float val) {
  m_fSensitivity = val;
}

void MouseLoop() {
  CMouse& mouse = CMouse::GetInstance();
  mouse.Update();
  mouse.Render();
}
