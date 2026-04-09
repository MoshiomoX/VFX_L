#include "ParticleEmitter.h"
#include <cmath>

ParticleEmitter::ParticleEmitter()
{
    std::random_device rd;
    m_Rng.seed(rd());
}

void ParticleEmitter::InitParticle(Particle& p)
{
    p.position = position;

    // 方向（コーン内ランダム）
    Vector3 dir = RandomDirectionInCone(direction, spreadAngle);
    float speed = RandomFloat(speedMin, speedMax);
    p.velocity = dir * speed;

    // サイズ・寿命
    p.size = RandomFloat(sizeMin, sizeMax);
    p.lifetime = RandomFloat(lifetimeMin, lifetimeMax);
    p.age = 0.0f;

    // 色
    p.color = startColor;

    // 有効化
    p.isAlive = true;
}

float ParticleEmitter::RandomFloat(float min, float max)
{
    std::uniform_real_distribution<float> dist(min, max);
    return dist(m_Rng);
}

Vector3 ParticleEmitter::RandomDirectionInCone(const Vector3& dir, float angleDegrees)
{
    if (angleDegrees <= 0.0f)
        return dir;

    float angleRad = DirectX::XMConvertToRadians(angleDegrees);

    // ランダムな角度
    float theta = RandomFloat(0.0f, DirectX::XM_2PI);
    float phi = RandomFloat(0.0f, angleRad);

    // ローカル方向（Z軸基準）
    float sinPhi = sinf(phi);
    Vector3 localDir(
        sinPhi * cosf(theta),
        sinPhi * sinf(theta),
        cosf(phi)
    );

    // dirをZ軸に合わせる回転
    Vector3 up = Vector3(0, 0, 1);
    Vector3 axis = up.Cross(dir);

    if (axis.LengthSquared() < 0.0001f)
    {
        // dirがほぼZ軸と同じ
        return (dir.z > 0) ? localDir : -localDir;
    }

    axis.Normalize();
    float angle = acosf(std::clamp(up.Dot(dir), -1.0f, 1.0f));
    Matrix rot = Matrix::CreateFromAxisAngle(axis, angle);

    return Vector3::TransformNormal(localDir, rot);
}