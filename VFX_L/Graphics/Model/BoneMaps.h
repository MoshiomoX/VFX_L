// ============================================================
// BoneMaps.h
// 骨名の対応表（別の人形用のアニメを世界空間で付け替える時に使う。SkinnedModel::AddAnimationsFromFile）
//   target = このモデルの骨名、source = アニメのファイルの骨名。
//   先頭の 1 行は腰（平行移動も付け替える骨）。左右の上腕・頭・腰はモデル同士の向き合わせにも使う
//   （source 側の名前で "upperarm_l" / "upperarm_r" / "Head" / "pelvis" を探す）。
//   表に無い骨（ねじれ骨・つま先・顔）は親に付いて動く。
//   行の順番にも意味がある：骨の向き（基準の姿勢の違いを直す時の「骨 → 子」）の子は、
//   その骨の子孫で表の中で最初に出てくる骨（手は中指、腰は Waist、胸は首）
// ============================================================
#pragma once

struct BoneMapEntry
{
    const char* target;
    const char* source;
};

// Reallusion Character Creator（CC3/CC4、CC_Base_*）← Quaternius / UE マネキン風（Universal Animation Library）
// 2026-10-04：Assets/Model/Shadowkin_SF（ユーザーがUnrealの配布から落とした PBR の人形）にレンジャーのアニメを付ける
inline constexpr BoneMapEntry kBoneMapCCFromUE[] = {
    { "CC_Base_Hip",         "pelvis" },     // 腰（平行移動も）
    { "CC_Base_Waist",       "spine_01" },
    { "CC_Base_Spine01",     "spine_02" },
    { "CC_Base_Spine02",     "spine_03" },
    { "CC_Base_NeckTwist01", "neck_01" },
    { "CC_Base_Head",        "Head" },

    { "CC_Base_L_Clavicle",  "clavicle_l" },
    { "CC_Base_L_Upperarm",  "upperarm_l" },
    { "CC_Base_L_Forearm",   "lowerarm_l" },
    { "CC_Base_L_Hand",      "hand_l" },
    { "CC_Base_L_Mid1",      "middle_01_l" },    // 手の向き = 中指の付け根の方（指の中で最初）
    { "CC_Base_L_Mid2",      "middle_02_l" },
    { "CC_Base_L_Mid3",      "middle_03_l" },
    { "CC_Base_L_Thumb1",    "thumb_01_l" },
    { "CC_Base_L_Thumb2",    "thumb_02_l" },
    { "CC_Base_L_Thumb3",    "thumb_03_l" },
    { "CC_Base_L_Index1",    "index_01_l" },
    { "CC_Base_L_Index2",    "index_02_l" },
    { "CC_Base_L_Index3",    "index_03_l" },
    { "CC_Base_L_Ring1",     "ring_01_l" },
    { "CC_Base_L_Ring2",     "ring_02_l" },
    { "CC_Base_L_Ring3",     "ring_03_l" },
    { "CC_Base_L_Pinky1",    "pinky_01_l" },
    { "CC_Base_L_Pinky2",    "pinky_02_l" },
    { "CC_Base_L_Pinky3",    "pinky_03_l" },

    { "CC_Base_R_Clavicle",  "clavicle_r" },
    { "CC_Base_R_Upperarm",  "upperarm_r" },
    { "CC_Base_R_Forearm",   "lowerarm_r" },
    { "CC_Base_R_Hand",      "hand_r" },
    { "CC_Base_R_Mid1",      "middle_01_r" },
    { "CC_Base_R_Mid2",      "middle_02_r" },
    { "CC_Base_R_Mid3",      "middle_03_r" },
    { "CC_Base_R_Thumb1",    "thumb_01_r" },
    { "CC_Base_R_Thumb2",    "thumb_02_r" },
    { "CC_Base_R_Thumb3",    "thumb_03_r" },
    { "CC_Base_R_Index1",    "index_01_r" },
    { "CC_Base_R_Index2",    "index_02_r" },
    { "CC_Base_R_Index3",    "index_03_r" },
    { "CC_Base_R_Ring1",     "ring_01_r" },
    { "CC_Base_R_Ring2",     "ring_02_r" },
    { "CC_Base_R_Ring3",     "ring_03_r" },
    { "CC_Base_R_Pinky1",    "pinky_01_r" },
    { "CC_Base_R_Pinky2",    "pinky_02_r" },
    { "CC_Base_R_Pinky3",    "pinky_03_r" },

    { "CC_Base_L_Thigh",     "thigh_l" },
    { "CC_Base_L_Calf",      "calf_l" },
    { "CC_Base_L_Foot",      "foot_l" },
    { "CC_Base_L_ToeBase",   "ball_l" },
    { "CC_Base_R_Thigh",     "thigh_r" },
    { "CC_Base_R_Calf",      "calf_r" },
    { "CC_Base_R_Foot",      "foot_r" },
    { "CC_Base_R_ToeBase",   "ball_r" },
};
