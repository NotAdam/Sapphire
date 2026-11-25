-- Encounters of Sastasha (converted from the EncounterSetup blocks in Sastasha.cpp). Data only: no logic.
local BOSS_BGM, FINAL_BOSS_BGM, NORMAL_BGM = 37, 38, 35

return {
  Chopper = encounterDef {
    name = "Chopper",
    timeline = "dungeons/sastasha/Chopper",
    shape = "polygon",
    polygon = "dungeons/sastasha/Chopper",
    bnpcs = { { layoutId = 4035011, hp = 400, type = "enemy", flags = { BNpcFlag.NoRoam }, boss = true } },
    lockoutEntrances = {
      { name = "sgvf_w_lvd_b0250", baseId = 2001504, boundId = 4323996, instanceId = 4036038, state = 4,
        pos = vector.create(94.597588, 26.865030, -68.584061), scale = 1.0, rotation = 0.0, invisibility = 1, remove = "success" },
    },
    lockout = true,
    placeName = 662,
    bgmCombat = BOSS_BGM,
    bgmRestore = NORMAL_BGM,
  },

  Madison = encounterDef {
    name = "Madison",
    timeline = "dungeons/sastasha/Madison",
    shape = "polygon",
    polygon = "dungeons/sastasha/Madison",
    bnpcs = { { layoutId = 3988325, hp = 600, type = "enemy", flags = { BNpcFlag.NoRoam }, boss = true } },
    lockoutEntrances = {
      { name = "unknown_1", baseId = 2001506, boundId = 3653862, instanceId = 4056797, state = 4,
        pos = vector.create(-9.239832, 24.789940, 35.778252), scale = 0.991760, rotation = 0.000048, invisibility = 1, remove = "success" },
    },
    lockoutExits = {
      { name = "Rambadedoor", baseId = 2000225, boundId = 3653865, instanceId = 3281037, state = 4,
        pos = vector.create(-35.299999, 24.000000, 60.799999), scale = 1.0, rotation = -2.007129, invisibility = 0, remove = "none" },
    },
    lockout = true,
    placeName = 663,
    bgmCombat = BOSS_BGM,
    bgmRestore = NORMAL_BGM,
  },

  Madison2 = encounterDef {
    name = "Madison2",
    timeline = "dungeons/sastasha/Madison2",
    shape = "polygon",
    polygon = "dungeons/sastasha/Madison2",
    bnpcs = { { layoutId = 4035056, hp = 800, type = "enemy", flags = { BNpcFlag.NoRoam }, boss = true } },
    lockoutEntrances = {
      { name = "unknown_2", baseId = 2001539, boundId = 3653864, instanceId = 4036041, state = 4,
        pos = vector.create(-158.560898, 8.099012, 214.344803), scale = 0.991760, rotation = 0.000048, invisibility = 1, remove = "success" },
    },
    initEObjs = {
      { name = "unknown_3", baseId = 2000235, boundId = 3656262, instanceId = 3281178, state = 4,
        pos = vector.create(-156.500000, 8.600000, 252.500000), scale = 1.0, rotation = 1.134464, invisibility = 0, remove = "none" },
    },
    lockoutExits = {
      { name = "Rambadedoor_1", baseId = 2000236, boundId = 3655908, instanceId = 3281175, state = 4,
        pos = vector.create(-190.000000, 7.000000, 252.000000), scale = 1.0, rotation = -2.443461, invisibility = 0, remove = "none" },
    },
    lockout = true,
    placeName = 670,
    bgmCombat = BOSS_BGM,
    bgmRestore = NORMAL_BGM,
  },

  Denn = encounterDef {
    name = "Denn",
    timeline = "dungeons/sastasha/Denn",
    shape = "polygon",
    polygon = "dungeons/sastasha/Denn",
    bnpcs = { { layoutId = 3978771, hp = 1000, type = "enemy", flags = { BNpcFlag.NoRoam }, boss = true } },
    lockoutEntrances = {
      { name = "sgpl_s1d1_bosswall", baseId = 2001508, boundId = 4236989, instanceId = 4036045, state = 4,
        pos = vector.create(-303.983612, 5.576412, 276.214111), scale = 1.0, rotation = 0.0, invisibility = 1, remove = "success" },
    },
    initEObjs = {
      { name = "Unnaturalripples", baseId = 2000405, boundId = 3992454, instanceId = 3741845, state = 4,
        pos = vector.create(-301.973206, 6.500000, 300.029388), scale = 0.991789, rotation = 0.000048, invisibility = 0, remove = "none" },
      { name = "Unnaturalripples_1", baseId = 2000406, boundId = 3992452, instanceId = 3741894, state = 4,
        pos = vector.create(-302.037598, 6.500000, 336.047302), scale = 1.0, rotation = 0.0, invisibility = 0, remove = "none" },
      { name = "Unnaturalripples_2", baseId = 2000407, boundId = 3992449, instanceId = 3741895, state = 4,
        pos = vector.create(-338.036499, 6.500000, 300.206512), scale = 0.991789, rotation = 0.000048, invisibility = 0, remove = "none" },
      { name = "Unnaturalripples_3", baseId = 2000408, boundId = 3992453, instanceId = 3741897, state = 4,
        pos = vector.create(-337.929596, 6.500000, 335.975311), scale = 1.0, rotation = 0.0, invisibility = 0, remove = "none" },
    },
    lockout = true,
    placeName = 672,
    bgmCombat = FINAL_BOSS_BGM,
    bgmRestore = NORMAL_BGM,
  },
}
