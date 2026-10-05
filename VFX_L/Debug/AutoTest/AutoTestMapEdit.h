// ============================================================
// AutoTestMapEdit.h
// TEMP-TEST: VFXL_MAPEDIT_AUTOTEST=1（F6 の地図エディタ Scene/MapEditMode を、マウス無しで一通り動かす）
// ============================================================
#pragma once

class MapEditMode;
class FlyCamera;

namespace MapEditAutoTest
{
    bool Enabled();
    void Update(MapEditMode& edit, FlyCamera& camera, float dt);
}
