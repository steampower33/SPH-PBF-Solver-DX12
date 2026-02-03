#pragma once

class Camera
{
public:
	Camera();

	void Initialize(float aspectRatio);
	void Update(float dt);

	// Handle Mouse Input
	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);
	void OnMouseWheel(float wheelDelta);

	SM::Matrix GetViewMatrix() const;
	SM::Matrix GetProjectionMatrix() const;
	SM::Vector3 GetEyePos() const { return m_EyePos; }

	void SetAspectRatio(float aspectRatio);

private:
	void UpdateViewMatrix();

private:
	SM::Vector3 m_EyePos = { 0.0f, 0.0f, 10.0f };
	SM::Vector3 m_Target = { 0.0f, 0.0f, 0.0f };
	SM::Vector3 m_Up = SM::Vector3::Up; // (0, 1, 0)

	// Projection Parameters
	float m_FovY = DX::XMConvertToRadians(45.0f);
	float m_AspectRatio = 1.777f;
	float m_NearZ = 0.1f;
	float m_FarZ = 1000.0f;

	// Orbit Control
	float m_Theta = 0.0f;   // Yaw
	float m_Phi = 0.0f;     // Pitch
	float m_Radius = 30.0f; // Zoom

	// Mouse State
	POINT m_LastMousePos = { 0, 0 };
	bool  m_IsDragging = false;
};