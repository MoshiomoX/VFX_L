#pragma once
#include "ParticleEmitter.h"
#include <vector>


class CPUParticleSystem
{

public:
	CPUParticleSystem();
	bool Initialize(int maxParticles = 1000);
	void Update(float dt);	
	void Reset();	

	void SetEmitter(const ParticleEmitter& emitter){ m_Emitter = emitter; }
	ParticleEmitter& GetEmitter() { return m_Emitter; }

	void SetEmitting(bool emit) { m_IsEmitting = emit; }
	bool IsEmitting() const { return m_IsEmitting; }

	// データ取得（レンダリング用）
	const std::vector<Particle>& GetParticles() const { return m_Particles; }
	int GetAliveCount() const;
private:
	void Emit(float dt);
	void UpdateParticles(float dt);
	int FindDeadParticle();
private:
	std::vector<Particle> m_Particles;
	ParticleEmitter m_Emitter;

	float m_EmitAccumulator = 0.0f; // 発射タイミング管理用	
	bool m_IsEmitting = true; // 発射中フラグ
};