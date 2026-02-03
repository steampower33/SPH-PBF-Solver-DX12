#include "Camera.h"

Camera::Camera()
{
}

void Camera::Initialize(float aspectRatio)
{
	m_AspectRatio = aspectRatio;
	UpdateViewMatrix();
}

void Camera::Update(float dt)
{
	UpdateViewMatrix();
}

void Camera::SetAspectRatio(float aspectRatio)
{
	m_AspectRatio = aspectRatio;
}

void Camera::OnMouseDown(WPARAM btnState, int x, int y)
{
	m_LastMousePos.x = x;
	m_LastMousePos.y = y;
	m_IsDragging = true;
}

void Camera::OnMouseUp(WPARAM btnState, int x, int y)
{
	m_IsDragging = false;
}

void Camera::OnMouseMove(WPARAM btnState, int x, int y)
{
    if (!m_IsDragging)
    {
        m_LastMousePos.x = x;
        m_LastMousePos.y = y;
        return;
    }

    if ((btnState & MK_LBUTTON) != 0)
    {
        float dx = DX::XMConvertToRadians(0.25f * static_cast<float>(x - m_LastMousePos.x));
        float dy = DX::XMConvertToRadians(0.25f * static_cast<float>(y - m_LastMousePos.y));

        m_Theta -= dx;
        m_Phi += dy;

        m_Phi = std::clamp(m_Phi, -DX::XM_PIDIV2 + 0.1f, DX::XM_PIDIV2 - 0.1f);
    }

    m_LastMousePos.x = x;
    m_LastMousePos.y = y;
}

void Camera::OnMouseWheel(float wheelDelta)
{
	// Zoom In/Out
	m_Radius -= wheelDelta * 0.01f;
	m_Radius = std::clamp(m_Radius, 1.0f, 200.0f);
}

void Camera::UpdateViewMatrix()
{
	// [Spherical to Cartesian]
	float x = m_Radius * cosf(m_Phi) * sinf(m_Theta);
	float y = m_Radius * sinf(m_Phi);
	float z = m_Radius * cosf(m_Phi) * cosf(m_Theta);

	SM::Vector3 offset(x, y, z);
	m_EyePos = m_Target + offset;
}

SM::Matrix Camera::GetViewMatrix() const
{
	return SM::Matrix::CreateLookAt(m_EyePos, m_Target, m_Up);
}

SM::Matrix Camera::GetProjectionMatrix() const
{
	return SM::Matrix::CreatePerspectiveFieldOfView(m_FovY, m_AspectRatio, m_NearZ, m_FarZ);
}