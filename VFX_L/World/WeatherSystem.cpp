// ============================================================
// WeatherSystem.cpp
// ============================================================
#include "World/WeatherSystem.h"
#include "World/StageConfig.h"
#include "Swarm/AreaVFXPlayer.h"
#include "Swarm/SwarmSystem.h"
#include "Graphics/Renderer/GrassRenderer.h"
#include "Audio/AudioSystem.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr const char* kRainVfx = "Rain.json";
    constexpr const char* kSandVfx = "Sandstorm.json";
    constexpr const char* kRainLoop = "Assets/Audio/SFX/rubberduck_WaterSlime/loop_rain.ogg";

    float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
    float Smooth(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }
    Vector3 LerpV(const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; }
    float Rand(uint32_t& seed, float a, float b)
    {
        seed = seed * 1664525u + 1013904223u;
        return a + (b - a) * (float)((seed >> 8) & 0xFFFFFF) / 16777215.0f;
    }
}

// ============================================================
// 時刻の照明（昼 = 面の設定、夕・夜はそこから作る）
// ============================================================
SceneLighting::Preset WeatherSystem::Lerp(const SceneLighting::Preset& a, const SceneLighting::Preset& b, float t)
{
    SceneLighting::Preset p;
    p.sunPitch = a.sunPitch + (b.sunPitch - a.sunPitch) * t;
    p.sunYaw = a.sunYaw + (b.sunYaw - a.sunYaw) * t;
    p.lightColor = LerpV(a.lightColor, b.lightColor, t);
    p.lightIntensity = a.lightIntensity + (b.lightIntensity - a.lightIntensity) * t;
    p.ambientSky = LerpV(a.ambientSky, b.ambientSky, t);
    p.ambientGround = LerpV(a.ambientGround, b.ambientGround, t);
    p.skyZenith = LerpV(a.skyZenith, b.skyZenith, t);
    p.skyHorizon = LerpV(a.skyHorizon, b.skyHorizon, t);
    p.skyBelow = LerpV(a.skyBelow, b.skyBelow, t);
    p.sunGlow = a.sunGlow + (b.sunGlow - a.sunGlow) * t;
    p.fogUseHorizon = (t < 0.5f) ? a.fogUseHorizon : b.fogUseHorizon;
    const Vector3 fa = a.fogUseHorizon ? a.skyHorizon : a.fogColor, fb = b.fogUseHorizon ? b.skyHorizon : b.fogColor;
    p.fogColor = LerpV(fa, fb, t);
    p.fogUseHorizon = false;   // 補間した色をそのまま使う
    p.fogStart = a.fogStart + (b.fogStart - a.fogStart) * t;
    p.fogEnd = a.fogEnd + (b.fogEnd - a.fogEnd) * t;
    p.fogMax = a.fogMax + (b.fogMax - a.fogMax) * t;
    return p;
}

void WeatherSystem::BuildTimeOfDay(const StageDef& stage)
{
    m_Day = stage.light;
    m_Dusk = m_Day;
    m_NightPreset = m_Day;
    if (stage.biome == TerrainGenerator::Biome::Dungeon)
    {
        // 遺跡は空が無い（元から夜）。夕・夜は月明かりを少し落とすだけ
        m_NightPreset.lightIntensity = m_Day.lightIntensity * 0.7f;
        m_NightPreset.ambientSky = m_Day.ambientSky * 0.8f;
        m_Dusk = Lerp(m_Day, m_NightPreset, 0.5f);
        return;
    }
    const bool desert = stage.biome == TerrainGenerator::Biome::Desert;

    // 夕：太陽が低く橙に。空は上が藍、地平線が橙。霧は地平線の色
    m_Dusk.sunPitch = 16.0f;
    m_Dusk.sunYaw = m_Day.sunYaw + 50.0f;
    m_Dusk.lightColor = { 1.0f, 0.58f, 0.30f };
    m_Dusk.lightIntensity = m_Day.lightIntensity * 0.9f;
    m_Dusk.ambientSky = desert ? Vector3(0.34f, 0.26f, 0.24f) : Vector3(0.30f, 0.26f, 0.34f);
    m_Dusk.ambientGround = { 0.18f, 0.12f, 0.08f };
    m_Dusk.skyZenith = desert ? Vector3(0.20f, 0.16f, 0.30f) : Vector3(0.10f, 0.14f, 0.40f);
    m_Dusk.skyHorizon = { 0.95f, 0.45f, 0.20f };
    m_Dusk.skyBelow = { 0.30f, 0.20f, 0.15f };
    m_Dusk.sunGlow = 0.6f;
    m_Dusk.fogUseHorizon = true;
    m_Dusk.fogStart = m_Day.fogStart * 0.8f;
    m_Dusk.fogEnd = m_Day.fogEnd * 0.9f;

    // 夜：弱い青白い月明かり。空は暗い藍、霧は暗い青（遠くが黒へ溶ける）。松明・幽霊・経験球の光が映える
    m_NightPreset.sunPitch = 48.0f;
    m_NightPreset.sunYaw = m_Day.sunYaw + 80.0f;
    m_NightPreset.lightColor = { 0.45f, 0.55f, 0.95f };
    m_NightPreset.lightIntensity = 0.28f;
    m_NightPreset.ambientSky = desert ? Vector3(0.07f, 0.07f, 0.13f) : Vector3(0.06f, 0.08f, 0.16f);
    m_NightPreset.ambientGround = { 0.03f, 0.03f, 0.05f };
    m_NightPreset.skyZenith = { 0.004f, 0.006f, 0.020f };
    m_NightPreset.skyHorizon = desert ? Vector3(0.04f, 0.035f, 0.06f) : Vector3(0.02f, 0.03f, 0.07f);
    m_NightPreset.skyBelow = { 0.01f, 0.01f, 0.02f };
    m_NightPreset.sunGlow = 0.12f;
    m_NightPreset.fogUseHorizon = false;
    m_NightPreset.fogColor = desert ? Vector3(0.03f, 0.025f, 0.04f) : Vector3(0.015f, 0.02f, 0.04f);
    m_NightPreset.fogStart = m_Day.fogStart * 0.6f;
    m_NightPreset.fogEnd = m_Day.fogEnd * 0.75f;
    m_NightPreset.fogMax = (std::max)(m_Day.fogMax, 0.9f);
}

SceneLighting::Preset WeatherSystem::PresetAt(float phase) const
{
    if (phase <= duskStart) return m_Day;
    if (phase < duskEnd) return Lerp(m_Day, m_Dusk, Smooth((phase - duskStart) / (std::max)(duskEnd - duskStart, 0.01f)));
    if (phase < nightEnd) return Lerp(m_Dusk, m_NightPreset, Smooth((phase - duskEnd) / (std::max)(nightEnd - duskEnd, 0.01f)));
    return m_NightPreset;
}

// 天候で照明を弱める：太陽を落とし、空と霧を曇りの色へ、霧を近くから
void WeatherSystem::ApplyWeatherToPreset(SceneLighting::Preset& p, float w) const
{
    if (w <= 0.0f) return;
    const Vector3 fogBase = p.fogUseHorizon ? p.skyHorizon : p.fogColor;
    Vector3 overcast, fogTo;
    float fogStartMul = 1.0f, fogEndMul = 1.0f, fogMaxTo = p.fogMax;
    switch (m_Kind)
    {
    case Kind::Rain:
        overcast = { 0.32f, 0.34f, 0.38f }; fogTo = { 0.40f, 0.43f, 0.47f };
        fogStartMul = 0.45f; fogEndMul = 0.55f; fogMaxTo = 0.92f;
        break;
    case Kind::Sandstorm:
        overcast = { 0.62f, 0.50f, 0.32f }; fogTo = { 0.66f, 0.54f, 0.36f };
        fogStartMul = 0.3f; fogEndMul = 0.35f; fogMaxTo = 0.97f;
        break;
    default:   // Storm（濃霧）
        overcast = fogBase; fogTo = fogBase * 1.3f;
        fogStartMul = 0.5f; fogEndMul = 0.55f; fogMaxTo = 0.96f;
        break;
    }
    // 夜は曇りの色も暗く（照明の明るさに合わせる）
    const float lum = std::clamp(p.lightIntensity + p.ambientSky.y * 1.5f, 0.15f, 1.6f);
    overcast = overcast * (std::min)(lum, 1.0f);
    fogTo = fogTo * (std::min)(lum, 1.0f);
    p.lightIntensity *= 1.0f - 0.55f * w;
    p.ambientSky = LerpV(p.ambientSky, overcast * 0.9f, 0.5f * w);
    p.ambientGround = p.ambientGround * (1.0f - 0.3f * w);
    p.skyZenith = LerpV(p.skyZenith, overcast * 0.8f, w);
    p.skyHorizon = LerpV(p.skyHorizon, overcast, w);
    p.skyBelow = LerpV(p.skyBelow, overcast * 0.7f, w);
    p.sunGlow *= 1.0f - 0.8f * w;
    p.fogUseHorizon = false;
    p.fogColor = LerpV(fogBase, fogTo, w);
    p.fogStart *= 1.0f - (1.0f - fogStartMul) * w;
    p.fogEnd *= 1.0f - (1.0f - fogEndMul) * w;
    p.fogMax = p.fogMax + (fogMaxTo - p.fogMax) * w;
}

// ============================================================
// Init / Update
// ============================================================
void WeatherSystem::Init(const StageDef& stage, float stageTime, SceneLighting& lighting, AreaVFXPlayer& vfx, const VFXContext* ctx,
    GrassRenderer* grass, SwarmSystem* swarm)
{
    Shutdown();
    m_Biome = (int)stage.biome;
    m_StageTime = (std::max)(stageTime, 1.0f);
    m_Lighting = &lighting;
    m_Vfx = &vfx;
    m_Ctx = ctx;
    m_Grass = grass;
    m_Swarm = swarm;
    switch (stage.biome)
    {
    case TerrainGenerator::Biome::Desert:  m_Kind = Kind::Sandstorm; break;
    case TerrainGenerator::Biome::Dungeon: m_Kind = Kind::Storm; break;
    default:                               m_Kind = Kind::Rain; break;
    }
    BuildTimeOfDay(stage);
    if (m_Grass)
    {
        m_BaseGrassWind = m_Grass->GetSettings().windStrength;
        m_BaseGrassSpeed = m_Grass->GetSettings().windSpeed;
        m_WindYawDeg = m_Grass->GetSettings().windYawDeg;
    }
    if (m_Swarm)
    {
        m_BaseOrbEmissive = m_Swarm->orbLook.emissive;
        m_BaseHpFill = m_Swarm->hpBar.fill;
    }
    m_Seed = (uint32_t)std::random_device{}();
    m_NextEventAt = Rand(m_Seed, firstEventMin, firstEventMax);
    m_NextLightning = Rand(m_Seed, lightningMin, lightningMax);
    m_State = State::Idle;
    m_Intensity = 0.0f;
    m_EventCount = 0;
}

void WeatherSystem::Shutdown()
{
    if (m_Vfx && m_VfxHandle) m_Vfx->StopInstance(m_VfxHandle);
    m_VfxHandle = 0;
    if (!m_AmbientFile.empty()) AudioSystem::Get().SetAmbient("", 0.0f);
    m_AmbientFile.clear();
    if (m_Grass)
    {
        m_Grass->GetSettings().windStrength = m_BaseGrassWind;
        m_Grass->GetSettings().windSpeed = m_BaseGrassSpeed;
    }
    if (m_Swarm)
    {
        m_Swarm->orbLook.emissive = m_BaseOrbEmissive;
        m_Swarm->hpBar.fill = m_BaseHpFill;
    }
    m_Lighting = nullptr; m_Vfx = nullptr; m_Grass = nullptr; m_Swarm = nullptr;
    m_State = State::Idle;
    m_Intensity = 0.0f;
}

void WeatherSystem::TriggerNow(float duration)
{
    if (m_State != State::Idle) return;
    m_Duration = (duration > 0.0f) ? duration : Rand(m_Seed, durationMin, durationMax);
    m_State = State::FadeIn;
    m_StateLeft = fadeIn;
    ++m_EventCount;
}

void WeatherSystem::StopNow()
{
    if (m_State == State::Idle || m_State == State::FadeOut) return;
    m_State = State::FadeOut;
    m_StateLeft = fadeOut;
}

void WeatherSystem::UpdateEvent(float dt, float runTime)
{
    switch (m_State)
    {
    case State::Idle:
        m_Intensity = 0.0f;
        if (eventsEnabled && runTime >= m_NextEventAt) TriggerNow();
        break;
    case State::FadeIn:
        m_StateLeft -= dt;
        m_Intensity = Smooth(1.0f - m_StateLeft / (std::max)(fadeIn, 0.01f));
        if (m_StateLeft <= 0.0f) { m_State = State::Active; m_StateLeft = m_Duration; m_Intensity = 1.0f; }
        break;
    case State::Active:
        m_StateLeft -= dt;
        m_Intensity = 1.0f;
        if (m_StateLeft <= 0.0f) { m_State = State::FadeOut; m_StateLeft = fadeOut; }
        break;
    case State::FadeOut:
        m_StateLeft -= dt;
        m_Intensity = Smooth(m_StateLeft / (std::max)(fadeOut, 0.01f));
        if (m_StateLeft <= 0.0f)
        {
            m_State = State::Idle;
            m_Intensity = 0.0f;
            m_NextEventAt = runTime + Rand(m_Seed, repeatMin, repeatMax);
        }
        break;
    }

    // 雷：濃霧の出来事と、雨の強い時
    const bool thunder = (m_Kind == Kind::Storm && m_Intensity > 0.3f) || (m_Kind == Kind::Rain && m_Intensity > 0.7f);
    if (thunder)
    {
        m_NextLightning -= dt;
        if (m_NextLightning <= 0.0f)
        {
            m_NextLightning = Rand(m_Seed, lightningMin, lightningMax);
            m_FlashLeft = 0.14f;
            AudioSystem::Get().Play("thunder", 0.9f, Rand(m_Seed, 0.85f, 1.1f));
        }
    }
    if (m_FlashLeft > 0.0f) m_FlashLeft -= dt;
}

void WeatherSystem::UpdateVfx(const Vector3& camPos, const Vector3& camForward)
{
    if (!m_Vfx || !m_Ctx || m_Kind == Kind::Storm) return;
    const bool want = m_Intensity > 0.25f;
    if (want && m_VfxHandle == 0)
        m_VfxHandle = m_Vfx->Play(m_Kind == Kind::Rain ? kRainVfx : kSandVfx, camPos, 1.0e7f, false, *m_Ctx);
    else if (!want && m_VfxHandle != 0)
    {
        m_Vfx->StopInstance(m_VfxHandle);
        m_VfxHandle = 0;
    }
    if (m_VfxHandle == 0) return;
    // カメラの少し前の上空に発射器を置く（降る間に見える所へ落ちてくる）
    Vector3 fwd(camForward.x, 0.0f, camForward.z);
    if (fwd.LengthSquared() > 1e-6f) fwd.Normalize();
    Vector3 pos = camPos + fwd * 12.0f;
    pos.y = camPos.y + (m_Kind == Kind::Rain ? 14.0f : 4.0f);
    m_Vfx->SetInstance(m_VfxHandle, pos, Vector3::Zero);
}

void WeatherSystem::UpdateAudio(float dt)
{
    (void)dt;
    const float target = (m_Kind == Kind::Storm) ? 0.0f : m_Intensity * (m_Kind == Kind::Rain ? 0.55f : 0.35f);
    const char* file = (target > 0.0f) ? kRainLoop : "";
    // 砂嵐は雨の環境音を低く鳴らして風代わり（専用の素材が無い）
    AudioSystem::Get().SetAmbient(file, target, m_Kind == Kind::Sandstorm ? 0.55f : 1.0f);
    m_AmbientFile = file;
    m_AmbientVolume = target;
}

void WeatherSystem::UpdateReadability()
{
    if (!m_Swarm) return;
    const float k = Clamp01((std::max)(m_Night, m_Intensity)) * readabilityBoost;
    m_Swarm->orbLook.emissive = m_BaseOrbEmissive * (1.0f + k);
    Vector4 f = m_BaseHpFill;
    f.x = (std::min)(f.x * (1.0f + 0.6f * k), 1.0f);
    f.y = (std::min)(f.y * (1.0f + 0.6f * k), 1.0f);
    f.z = (std::min)(f.z * (1.0f + 0.6f * k), 1.0f);
    m_Swarm->hpBar.fill = f;
}

void WeatherSystem::Update(float dt, float runTime, const Vector3& camPos, const Vector3& camForward)
{
    if (!m_Lighting) return;
    dt = (std::min)(dt, 0.1f);
    m_Phase = (timeOverride >= 0.0f) ? timeOverride : runTime / m_StageTime;
    m_Night = Clamp01((m_Phase - duskEnd) / (std::max)(nightEnd - duskEnd, 0.01f));
    UpdateEvent(dt, runTime);

    if (driveLighting)
    {
        SceneLighting::Preset p = PresetAt(m_Phase);
        ApplyWeatherToPreset(p, m_Intensity);
        if (m_FlashLeft > 0.0f)
        {
            // 雷の白：環境光と空を一瞬持ち上げる（0.14 秒で消える）
            const float k = Clamp01(m_FlashLeft / 0.14f);
            p.ambientSky += Vector3(1.2f, 1.2f, 1.4f) * k;
            p.ambientGround += Vector3(0.4f, 0.4f, 0.5f) * k;
            p.skyHorizon += Vector3(0.8f, 0.8f, 0.9f) * k;
            p.skyZenith += Vector3(0.4f, 0.4f, 0.5f) * k;
            p.fogColor += Vector3(0.5f, 0.5f, 0.6f) * k;
        }
        m_Lighting->ApplyPreset(p);
    }

    if (m_Grass)
    {
        const float gust = (m_Kind == Kind::Sandstorm) ? 2.2f : 1.4f;
        m_Grass->GetSettings().windStrength = m_BaseGrassWind * (1.0f + gust * m_Intensity);
        m_Grass->GetSettings().windSpeed = m_BaseGrassSpeed * (1.0f + 0.8f * m_Intensity);
    }
    UpdateVfx(camPos, camForward);
    UpdateAudio(dt);
    UpdateReadability();
}

// ============================================================
// パネル
// ============================================================
void WeatherSystem::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Weather")) return;
    const char* kinds[] = { "None", "Rain", "Sandstorm", "Storm (fog + lightning)" };
    const char* states[] = { "Idle", "Fade in", "Active", "Fade out" };
    ImGui::Text("Kind %s   state %s   intensity %.2f   night %.2f   phase %.2f   events %d",
        kinds[(int)m_Kind], states[(int)m_State], m_Intensity, m_Night, m_Phase, m_EventCount);
    if (m_State == State::Idle) ImGui::Text("Next event at %.0f s (run time)", m_NextEventAt);
    else ImGui::Text("State left %.1f s", m_StateLeft);
    if (ImGui::Button("Trigger Now")) TriggerNow();
    ImGui::SameLine();
    if (ImGui::Button("Stop Now")) StopNow();
    ImGui::SameLine();
    if (ImGui::Button("Lightning")) { m_FlashLeft = 0.14f; AudioSystem::Get().Play("thunder"); }
    ImGui::Checkbox("Drive Lighting", &driveLighting);
    ImGui::SameLine();
    ImGui::Checkbox("Auto Events", &eventsEnabled);
    bool over = timeOverride >= 0.0f;
    if (ImGui::Checkbox("Time Override", &over)) timeOverride = over ? m_Phase : -1.0f;
    if (over) ImGui::SliderFloat("Time (0 day, 1 dusk, 1.1 night)", &timeOverride, 0.0f, 1.2f, "%.2f");
    ImGui::DragFloatRange2("First Event (s)", &firstEventMin, &firstEventMax, 1.0f, 0.0f, 600.0f, "%.0f");
    ImGui::DragFloatRange2("Repeat (s)", &repeatMin, &repeatMax, 1.0f, 30.0f, 900.0f, "%.0f");
    ImGui::DragFloatRange2("Duration (s)", &durationMin, &durationMax, 1.0f, 5.0f, 300.0f, "%.0f");
    ImGui::DragFloat("Fade In", &fadeIn, 0.1f, 0.5f, 60.0f, "%.1f s");
    ImGui::DragFloat("Fade Out", &fadeOut, 0.1f, 0.5f, 60.0f, "%.1f s");
    ImGui::DragFloatRange2("Dusk (phase)", &duskStart, &duskEnd, 0.01f, 0.0f, 1.5f, "%.2f");
    ImGui::DragFloat("Night End (phase)", &nightEnd, 0.01f, 1.0f, 2.0f, "%.2f");
    ImGui::DragFloat("Readability Boost", &readabilityBoost, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloatRange2("Lightning Interval", &lightningMin, &lightningMax, 0.5f, 1.0f, 60.0f, "%.0f s");
}
