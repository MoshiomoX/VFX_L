#pragma once
#include "CameraBase.h"

class DebugCamera : public CameraBase
{
public:
    void Update(float dt) override;

    float flightSpeed = 0.1f;

private:
    enum State { None, Orbit, Track, Dolly, Flight };

    void UpdateState();
    void UpdateOrbit();
    void UpdateTrack();
    void UpdateDolly();
    void UpdateFlight();

    // 共通引数
    struct Argument
    {
        DirectX::XMFLOAT2 mouseMove;
        DirectX::XMVECTOR vCamPos;
        DirectX::XMVECTOR vCamLook;
        DirectX::XMVECTOR vCamFront;
        DirectX::XMVECTOR vCamSide;
        DirectX::XMVECTOR vCamUp;
        float focus;
    };

    State m_State = None;
    POINT m_OldPos = {};
    Argument m_Arg = {};
};