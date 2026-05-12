#include "DebugCamera.h"
#include "InputManager.h"

void DebugCamera::Update(float dt)
{
    UpdateState();
    if (m_State == None) return;

    // マウス移動量
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    m_Arg.mouseMove = { (float)(cursorPos.x - m_OldPos.x), (float)(cursorPos.y - m_OldPos.y) };
    m_OldPos = cursorPos;

    // カメラ情報
    m_Arg.vCamPos = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&m_Position));
    m_Arg.vCamLook = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&m_Target));
    DirectX::XMVECTOR vCamUp = DirectX::XMVector3Normalize(
        DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&m_Up)));
    DirectX::XMVECTOR vFront = DirectX::XMVectorSubtract(m_Arg.vCamLook, m_Arg.vCamPos);

    // カメラ姿勢
    m_Arg.vCamFront = DirectX::XMVector3Normalize(vFront);
    m_Arg.vCamSide = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(vCamUp, m_Arg.vCamFront));
    m_Arg.vCamUp = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(m_Arg.vCamFront, m_Arg.vCamSide));

    DirectX::XMStoreFloat(&m_Arg.focus, DirectX::XMVector3Length(vFront));

    switch (m_State)
    {
    case Orbit:  UpdateOrbit();  break;
    case Track:  UpdateTrack();  break;
    case Dolly:  UpdateDolly();  break;
    case Flight: UpdateFlight(); break;
    }

    m_Dirty = true;
}

void DebugCamera::UpdateState()
{
    auto& input = InputManager::Get();
    State prev = m_State;

    if (input.GetKeyPress(VK_MENU))
    {
        m_State = None;
        if (input.GetMousePress(0)) m_State = Orbit;
        if (input.GetMousePress(2)) m_State = Track;
        if (input.GetMousePress(1)) m_State = Dolly;
    }
    else if (input.GetMousePress(1))
    {
        m_State = Flight;
    }
    else
    {
        m_State = None;
    }

    if (prev != m_State)
    {
        GetCursorPos(&m_OldPos);
    }
}

void DebugCamera::UpdateOrbit()
{
    float angleX = 360.0f * m_Arg.mouseMove.x / 1280.0f;
    float angleY = 180.0f * m_Arg.mouseMove.y / 720.0f;

    DirectX::XMMATRIX matRotY = DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(angleX));
    DirectX::XMVECTOR vAxis = DirectX::XMVector3Normalize(
        DirectX::XMVector3TransformCoord(m_Arg.vCamSide, matRotY));

    DirectX::XMMATRIX matRotSide = DirectX::XMMatrixRotationAxis(vAxis, DirectX::XMConvertToRadians(angleY));
    DirectX::XMVECTOR vRelative = DirectX::XMVectorScale(m_Arg.vCamFront, m_Arg.focus);
    vRelative = DirectX::XMVector3TransformCoord(vRelative, matRotY * matRotSide);

    DirectX::XMFLOAT3 newPos;
    DirectX::XMStoreFloat3(&newPos, DirectX::XMVectorSubtract(m_Arg.vCamLook, vRelative));
    m_Position = { newPos.x, newPos.y, newPos.z };

    DirectX::XMFLOAT3 newUp;
    DirectX::XMStoreFloat3(&newUp, DirectX::XMVector3Normalize(
        DirectX::XMVector3Cross(DirectX::XMVector3Normalize(vRelative), vAxis)));
    m_Up = { newUp.x, newUp.y, newUp.z };
}

void DebugCamera::UpdateTrack()
{
    float farZ = 1000.0f;
    float fovy = DirectX::XMConvertToRadians(45.0f);
    float aspect = 1600.0f / 900.0f;

    float farScreenHeight = tanf(fovy * 0.5f) * farZ;
    float screenRateW = m_Arg.mouseMove.x / 640.0f;
    float screenRateH = m_Arg.mouseMove.y / 360.0f;
    float farMoveX = -farScreenHeight * screenRateW * aspect;
    float farMoveY = farScreenHeight * screenRateH;

    float rate = m_Arg.focus / farZ;
    DirectX::XMVECTOR vMove = DirectX::XMVectorZero();
    vMove = DirectX::XMVectorAdd(vMove, DirectX::XMVectorScale(m_Arg.vCamSide, farMoveX * rate));
    vMove = DirectX::XMVectorAdd(vMove, DirectX::XMVectorScale(m_Arg.vCamUp, farMoveY * rate));

    DirectX::XMFLOAT3 newPos, newTarget;
    DirectX::XMStoreFloat3(&newPos, DirectX::XMVectorAdd(m_Arg.vCamPos, vMove));
    DirectX::XMStoreFloat3(&newTarget, DirectX::XMVectorAdd(m_Arg.vCamLook, vMove));
    m_Position = { newPos.x, newPos.y, newPos.z };
    m_Target = { newTarget.x, newTarget.y, newTarget.z };
}

void DebugCamera::UpdateDolly()
{
    float farZ = 1000.0f;
    float rate = m_Arg.focus / farZ;
    DirectX::XMVECTOR vMove = DirectX::XMVectorScale(
        m_Arg.vCamFront, farZ * 0.01f * rate * (m_Arg.mouseMove.x + m_Arg.mouseMove.y));

    DirectX::XMFLOAT3 newPos;
    DirectX::XMStoreFloat3(&newPos, DirectX::XMVectorAdd(m_Arg.vCamPos, vMove));
    m_Position = { newPos.x, newPos.y, newPos.z };
}

void DebugCamera::UpdateFlight()
{
    float angleX = 360.0f * m_Arg.mouseMove.x / 1280.0f;
    float angleY = 180.0f * m_Arg.mouseMove.y / 720.0f;

    DirectX::XMMATRIX matUpRot = DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(angleX));
    DirectX::XMVECTOR vSideAxis = DirectX::XMVector3Normalize(
        DirectX::XMVector3TransformCoord(m_Arg.vCamSide, matUpRot));

    DirectX::XMMATRIX matSideRot = DirectX::XMMatrixRotationAxis(vSideAxis, DirectX::XMConvertToRadians(angleY));
    DirectX::XMVECTOR vFrontAxis = DirectX::XMVector3Normalize(
        DirectX::XMVector3TransformCoord(m_Arg.vCamFront, matUpRot * matSideRot));

    auto& input = InputManager::Get();
    DirectX::XMVECTOR vMove = DirectX::XMVectorZero();
    if (input.GetKeyPress('W')) vMove = DirectX::XMVectorAdd(vMove, vFrontAxis);
    if (input.GetKeyPress('S')) vMove = DirectX::XMVectorSubtract(vMove, vFrontAxis);
    if (input.GetKeyPress('A')) vMove = DirectX::XMVectorSubtract(vMove, vSideAxis);
    if (input.GetKeyPress('D')) vMove = DirectX::XMVectorAdd(vMove, vSideAxis);
    if (input.GetKeyPress('Q')) vMove = DirectX::XMVectorAdd(vMove, DirectX::XMVectorSet(0, 1, 0, 0));
    if (input.GetKeyPress('E')) vMove = DirectX::XMVectorAdd(vMove, DirectX::XMVectorSet(0, -1, 0, 0));
    vMove = DirectX::XMVectorScale(vMove, flightSpeed);

    DirectX::XMVECTOR vNewPos = DirectX::XMVectorAdd(m_Arg.vCamPos, vMove);

    DirectX::XMFLOAT3 newPos, newTarget, newUp;
    DirectX::XMStoreFloat3(&newPos, vNewPos);
    DirectX::XMStoreFloat3(&newTarget, DirectX::XMVectorAdd(vNewPos, DirectX::XMVectorScale(vFrontAxis, m_Arg.focus)));
    DirectX::XMStoreFloat3(&newUp, DirectX::XMVector3Normalize(DirectX::XMVector3Cross(vFrontAxis, vSideAxis)));
    m_Position = { newPos.x, newPos.y, newPos.z };
    m_Target = { newTarget.x, newTarget.y, newTarget.z };
    m_Up = { newUp.x, newUp.y, newUp.z };
}