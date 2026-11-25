-- Event objects of Sastasha (converted from Sastasha.cpp onInit). Data only: no logic.
-- memo[coralId] is the Bloodymemo variant for that coral; static.<Name> are spawned on create.
return {
  memo = { -- keyed by coral id (Blue, Red, Green)
    [2000213] = eobjDef { name = "Bloodymemo", baseId = 2000212, boundId = 0, instanceId = 4072691, state = 4,
      pos = vector.create(320.812988, 47.862450, -130.776306), scale = 0.600000, rotation = -0.898762, invisibility = 0 },
    [2000214] = eobjDef { name = "Bloodymemo", baseId = 2001548, boundId = 0, instanceId = 4072692, state = 4,
      pos = vector.create(320.812988, 47.862450, -130.776306), scale = 0.600000, rotation = -0.898762, invisibility = 0 },
    [2000215] = eobjDef { name = "Bloodymemo", baseId = 2001549, boundId = 0, instanceId = 4072693, state = 4,
      pos = vector.create(320.812988, 47.862450, -130.776306), scale = 0.600000, rotation = -0.898762, invisibility = 0 },
  },
  static = {
    unknown_0 = eobjDef { name = "unknown_0", baseId = 2000211, boundId = 0, instanceId = 3280858, state = 4, pos = vector.create(367.827087, 47.400051, -226.694305), scale = 4.714991, rotation = 0.000432, invisibility = 0 },
    Bluecoralformation = eobjDef { name = "Bluecoralformation", baseId = 2000213, boundId = 3668215, instanceId = 3280868, state = 4, pos = vector.create(75.869797, 35.101421, -32.537209), scale = 0.930753, rotation = 0.000240, invisibility = 0 },
    Redcoralformation = eobjDef { name = "Redcoralformation", baseId = 2000214, boundId = 3668214, instanceId = 3280879, state = 4, pos = vector.create(88.769371, 31.135691, -40.869640), scale = 0.930753, rotation = 0.000240, invisibility = 0 },
    Greencoralformation = eobjDef { name = "Greencoralformation", baseId = 2000215, boundId = 3668216, instanceId = 3280932, state = 4, pos = vector.create(64.988159, 33.672821, -56.690559), scale = 0.991789, rotation = 0.000048, invisibility = 0 },
    Inconspicuousswitch = eobjDef { name = "Inconspicuousswitch", baseId = 2000216, boundId = 3653858, instanceId = 3280956, state = 4, pos = vector.create(62.907951, 33.969521, -31.172279), scale = 1.000000, rotation = -1.396264, invisibility = 1 },
    Hiddendoor = eobjDef { name = "Hiddendoor", baseId = 2000217, boundId = 3653517, instanceId = 3280959, state = 4, pos = vector.create(59.000000, 32.000000, -35.000000), scale = 1.000000, rotation = -2.007129, invisibility = 0,
      collision = {
        { pos = vector.create(59.048332, 39.016666, -35.022537), rotation = 2.007129, width = 17.600000, height = 15.436666, depth = 1.476608 }
      },
      collisionEnabled = true },
    Rambadedoor = eobjDef { name = "Rambadedoor", baseId = 2000225, boundId = 3653865, instanceId = 3281037, state = 4, pos = vector.create(-35.299999, 24.000000, 60.799999), scale = 1.000000, rotation = -2.007129, invisibility = 0,
      collision = {
        { pos = vector.create(-35.299999, 29.043749, 60.799999), rotation = 2.007129, width = 14.520002, height = 11.591252, depth = 0.600005 }
      },
      collisionEnabled = true },
    Captainsquarters = eobjDef { name = "Captainsquarters", baseId = 2000227, boundId = 3687697, instanceId = 3281045, state = 4, pos = vector.create(-95.044670, 20.513069, 172.039597), scale = 0.991789, rotation = 0.000048, invisibility = 0,
      collision = {
        { pos = vector.create(-95.065819, 23.074062, 172.106201), rotation = -3.141593, width = 7.431962, height = 6.763168, depth = 0.981615 }
      },
      collisionEnabled = true },
    WaveriderGate = eobjDef { name = "WaveriderGate", baseId = 2000231, boundId = 3655909, instanceId = 3281133, state = 4, pos = vector.create(-130.600006, 16.000000, 156.800003), scale = 1.000000, rotation = -2.007129, invisibility = 0,
      collision = {
        { pos = vector.create(-130.600006, 20.793749, 156.800003), rotation = 2.007129, width = 14.520002, height = 11.591252, depth = 0.600005 }
      },
      collisionEnabled = true },
    TheHole = eobjDef { name = "TheHole", baseId = 2000232, boundId = 3656260, instanceId = 3281161, state = 4, pos = vector.create(-36.000000, 16.500000, 163.300003), scale = 1.000000, rotation = 1.047198, invisibility = 0,
      collision = {
        { pos = vector.create(-36.000000, 22.126310, 163.300003), rotation = -2.094395, width = 15.180008, height = 12.375000, depth = 1.050023 }
      },
      collisionEnabled = true },
    Rambadedoor_1 = eobjDef { name = "Rambadedoor_1", baseId = 2000236, boundId = 3655908, instanceId = 3281175, state = 4, pos = vector.create(-190.000000, 7.000000, 252.000000), scale = 1.000000, rotation = -2.443461, invisibility = 0,
      collision = {
        { pos = vector.create(-190.000000, 11.943748, 252.000000), rotation = 2.443461, width = 14.520002, height = 11.591252, depth = 0.600005 }
      },
      collisionEnabled = true },
    unknown_3 = eobjDef { name = "unknown_3", baseId = 2000235, boundId = 3656262, instanceId = 3281178, state = 4, pos = vector.create(-156.500000, 8.600000, 252.500000), scale = 1.000000, rotation = 1.134464, invisibility = 0,
      collision = {
        { pos = vector.create(-156.500000, 14.225000, 252.500000), rotation = -2.007129, width = 15.180008, height = 12.375000, depth = 1.050023 }
      },
      collisionEnabled = true },
    Exit = eobjDef { name = "Exit", baseId = 2000139, boundId = 0, instanceId = 3281180, state = 4, pos = vector.create(-314.279114, 5.630589, 348.735596), scale = 0.900235, rotation = 0.000336, invisibility = 0 },
    -- States -> vf_kai_off (id: 4) vf_kai_on (id: 5) vf_kai_pop (id: 6) close_open (id: 7) open_close (id: 8)
    Giantclam = eobjDef { name = "Giantclam", baseId = 2000222, boundId = 4208408, instanceId = 3284776, state = 4, pos = vector.create(181.170303, 32.104599, -128.069000), scale = 0.991789, rotation = -0.862350, invisibility = 0 },
    WaveriderGatekey = eobjDef { name = "WaveriderGatekey", baseId = 2000255, boundId = 0, instanceId = 3332312, state = 4, pos = vector.create(-95.515343, 20.000000, 177.197800), scale = 1.000000, rotation = 0.000000, invisibility = 0 },
    KeytotheHole = eobjDef { name = "KeytotheHole", baseId = 2000256, boundId = 0, instanceId = 3332313, state = 4, pos = vector.create(-38.076599, 17.232731, 158.839401), scale = 0.991760, rotation = 1.561760, invisibility = 0 },
    -- States -> vf_kai_off (id: 4) vf_kai_on (id: 5) vf_kai_pop (id: 6) close_open (id: 7) open_close (id: 8)
    sgbg_s1d1_p1_shel1 = eobjDef { name = "sgbg_s1d1_p1_shel1", baseId = 2000260, boundId = 4208409, instanceId = 3424283, state = 4, pos = vector.create(166.318695, 30.735420, -128.312103), scale = 0.991789, rotation = 0.481030, invisibility = 0 },
    -- States -> vf_kai_off (id: 4) vf_kai_on (id: 5) vf_kai_pop (id: 6) close_open (id: 7) open_close (id: 8)
    sgbg_s1d1_p1_shel1_1 = eobjDef { name = "sgbg_s1d1_p1_shel1_1", baseId = 2000261, boundId = 4208410, instanceId = 3425471, state = 4, pos = vector.create(158.800598, 28.586321, -76.340927), scale = 0.991789, rotation = 1.471638, invisibility = 0 },
    -- States -> vf_kai_off (id: 4) vf_kai_on (id: 5) vf_kai_pop (id: 6) close_open (id: 7) open_close (id: 8)
    sgbg_s1d1_p1_shel1_2 = eobjDef { name = "sgbg_s1d1_p1_shel1_2", baseId = 2000262, boundId = 4208411, instanceId = 3425472, state = 4, pos = vector.create(125.463402, 29.260550, -51.934608), scale = 0.991789, rotation = -0.375975, invisibility = 0 },
    -- States -> vf_kai_off (id: 4) vf_kai_on (id: 5) vf_kai_pop (id: 6) close_open (id: 7) open_close (id: 8)
    sgbg_s1d1_p1_shel1_3 = eobjDef { name = "sgbg_s1d1_p1_shel1_3", baseId = 2000263, boundId = 4208412, instanceId = 3425473, state = 4, pos = vector.create(126.070198, 28.913260, -99.908722), scale = 1.000000, rotation = 0.020540, invisibility = 0 },
    -- States -> vf_kai_off (id: 4) vf_kai_on (id: 5) vf_kai_pop (id: 6) close_open (id: 7) open_close (id: 8)
    sgbg_s1d1_p1_shel1_4 = eobjDef { name = "sgbg_s1d1_p1_shel1_4", baseId = 2000264, boundId = 4208413, instanceId = 3425474, state = 4, pos = vector.create(97.055313, 27.081551, -70.264381), scale = 0.991789, rotation = -0.618915, invisibility = 0 },
    Unnaturalripples = eobjDef { name = "Unnaturalripples", baseId = 2000405, boundId = 3992454, instanceId = 3741845, state = 4, pos = vector.create(-301.973206, 6.500000, 300.029388), scale = 0.991789, rotation = 0.000048, invisibility = 0 },
    Unnaturalripples_1 = eobjDef { name = "Unnaturalripples_1", baseId = 2000406, boundId = 3992452, instanceId = 3741894, state = 4, pos = vector.create(-302.037598, 6.500000, 336.047302), scale = 1.000000, rotation = 0.000000, invisibility = 0 },
    Unnaturalripples_2 = eobjDef { name = "Unnaturalripples_2", baseId = 2000407, boundId = 3992449, instanceId = 3741895, state = 4, pos = vector.create(-338.036499, 6.500000, 300.206512), scale = 0.991789, rotation = 0.000048, invisibility = 0 },
    Unnaturalripples_3 = eobjDef { name = "Unnaturalripples_3", baseId = 2000408, boundId = 3992453, instanceId = 3741897, state = 4, pos = vector.create(-337.929596, 6.500000, 335.975311), scale = 1.000000, rotation = 0.000000, invisibility = 0 },
    Shortcut = eobjDef { name = "Shortcut", baseId = 2000700, boundId = 0, instanceId = 4033735, state = 4, pos = vector.create(344.705688, 43.781551, -217.365997), scale = 0.991760, rotation = 0.000048, invisibility = 1 },
    -- States -> vf_bextwall_on (id: 3) vf_bextwall_of (id: 4)
    sgvf_w_lvd_b0250 = eobjDef { name = "sgvf_w_lvd_b0250", baseId = 2001504, boundId = 4323996, instanceId = 4036038, state = 4, pos = vector.create(94.597588, 26.865030, -68.584061), scale = 1.000000, rotation = 0.000000, invisibility = 1,
      collision = {
        { pos = vector.create(98.210648, 29.116930, -61.158611), rotation = -0.916448, width = 21.000000, height = 10.000000, depth = 1.500000 }
      } },
    -- States -> vf_line_on (id: 10) vf_line_of (id: 11)
    sgvf_w_lvd_b0249 = eobjDef { name = "sgvf_w_lvd_b0249", baseId = 2001505, boundId = 4323997, instanceId = 4036039, state = 4, pos = vector.create(95.510597, 26.620729, -67.853653), scale = 1.000000, rotation = 0.000000, invisibility = 0 },
    unknown_2 = eobjDef { name = "unknown_2", baseId = 2001539, boundId = 3653864, instanceId = 4036041, state = 4, pos = vector.create(-158.560898, 8.099012, 214.344803), scale = 0.991760, rotation = 0.000048, invisibility = 1,
      collision = {
        { pos = vector.create(-162.986496, 11.158604, 219.803619), rotation = -0.261799, width = 6.177516, height = 7.462764, depth = 1.020000 }
      } },
    -- States -> vf_line_on (id: 12) vf_line_of (id: 13)
    sgvf_w_lvd_b0094_1 = eobjDef { name = "sgvf_w_lvd_b0094_1", baseId = 2001540, boundId = 4056793, instanceId = 4036043, state = 4, pos = vector.create(-163.598602, 8.026373, 214.030106), scale = 0.991760, rotation = 0.000048, invisibility = 0 },
    sgpl_s1d1_bosswall = eobjDef { name = "sgpl_s1d1_bosswall", baseId = 2001508, boundId = 4236989, instanceId = 4036045, state = 4, pos = vector.create(-303.983612, 5.576412, 276.214111), scale = 1.000000, rotation = 0.000000, invisibility = 1,
      collision = {
        { pos = vector.create(-300.141296, 8.076687, 278.205902), rotation = -3.141593, width = 10.500000, height = 8.000000, depth = 1.500000 },
        { pos = vector.create(-345.069397, 7.546691, 278.205902), rotation = -3.141593, width = 21.000000, height = 8.000000, depth = 1.500000 }
      } },
    sgpl_s1d1_bossline = eobjDef { name = "sgpl_s1d1_bossline", baseId = 2001509, boundId = 4236990, instanceId = 4036046, state = 4, pos = vector.create(-305.302002, 5.542851, 275.750885), scale = 1.000000, rotation = 0.000000, invisibility = 0 },
    unknown_1 = eobjDef { name = "unknown_1", baseId = 2001506, boundId = 3653862, instanceId = 4056797, state = 4, pos = vector.create(-9.239832, 24.789940, 35.778252), scale = 0.991760, rotation = 0.000048, invisibility = 0,
      collision = {
        { pos = vector.create(-3.186494, 26.457243, 30.003618), rotation = -0.261799, width = 6.177516, height = 7.462764, depth = 1.020000 }
      } },
    -- States -> vf_line_on (id: 12) vf_line_of (id: 13)
    sgvf_w_lvd_b0094 = eobjDef { name = "sgvf_w_lvd_b0094", baseId = 2001507, boundId = 4035750, instanceId = 4056798, state = 4, pos = vector.create(-2.841087, 23.114571, 38.090420), scale = 0.991760, rotation = 0.000048, invisibility = 0 },
    -- States -> vf_lock_on (id: 11) vf_lock_of (id: 12)
    Entrance = eobjDef { name = "Entrance", baseId = 2000182, boundId = 4096706, instanceId = 4096707, state = 5, pos = vector.create(361.881714, 46.092751, -225.181305), scale = 1.000000, rotation = 0.000000, invisibility = 0,
      collision = {
        { pos = vector.create(359.516113, 47.920158, -228.417206), rotation = 0.000000, width = 2.600000, height = 8.000000, depth = 0.500000 },
        { pos = vector.create(359.525909, 47.920158, -222.417206), rotation = 0.000000, width = 2.600000, height = 8.000000, depth = 0.500000 },
        { pos = vector.create(362.516113, 47.920158, -225.413315), rotation = 1.570451, width = 2.600000, height = 8.000000, depth = 0.500000 },
        { pos = vector.create(356.516113, 47.920158, -225.406235), rotation = 1.570451, width = 2.600000, height = 8.000000, depth = 0.500000 },
        { pos = vector.create(357.416107, 47.920158, -227.517212), rotation = 0.785398, width = 2.800000, height = 8.000000, depth = 0.500000 },
        { pos = vector.create(361.616119, 47.920158, -223.317200), rotation = 0.785398, width = 2.800000, height = 8.000000, depth = 0.500000 },
        { pos = vector.create(361.616119, 47.920158, -227.517212), rotation = -0.785398, width = 2.800000, height = 8.000000, depth = 0.500000 },
        { pos = vector.create(357.416107, 47.920158, -223.317200), rotation = -0.785398, width = 2.800000, height = 8.000000, depth = 0.500000 }
      } },
    Captainsquarterskey = eobjDef { name = "Captainsquarterskey", baseId = 2000250, boundId = 0, instanceId = 4192068, state = 4, pos = vector.create(-100.625000, 15.600010, 137.150696), scale = 1.000000, rotation = 0.000000, invisibility = 0 },
    sgpl_s1d1_sghit_ctrl = eobjDef { name = "sgpl_s1d1_sghit_ctrl", baseId = 2000223, boundId = 4200832, instanceId = 4200772, state = 4, pos = vector.create(-24.018980, 18.475060, 111.404900), scale = 1.000000, rotation = 0.000000, invisibility = 0,
      collision = {
        { pos = vector.create(-11.414590, 22.215639, 108.525803), rotation = 0.000000, width = 18.736109, height = 6.194796, depth = 0.527770 }
      } },
  },
}
