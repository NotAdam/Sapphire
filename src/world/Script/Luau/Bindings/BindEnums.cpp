// Global enum tables. Members are generated from the C++ enums (same names); regenerate when those enums change.

#include "Bindings.h"

#include <Common.h>

#include "Action/CommonAction.h"
#include "Actor/BNpc.h"
#include "Event/EventDefs.h"
#include "Event/EventHandler.h"

namespace Sapphire::Scripting::Bindings
{
  void bindEnums( Bind::Registry& reg )
  {
    {
      Bind::Enum e( reg, "SceneFlags" );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, NONE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, NO_DEFAULT_CAMERA );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, FADE_OUT );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_ENPC );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_EOBJ );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_BNPC );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_OTHER_PC );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_PARTY_PC );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_PARTY_BUDDY );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_GATHERING_POINT );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_AETHERYTE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_TREASURE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, CONDITION_CUTSCENE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, HIDE_UI );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_ALL );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, AUTO_LOC_CAMERA );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, HIDE_HOTBAR );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVINCIBLE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, SILENT_ENTER_TERRI_ENV );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, SILENT_ENTER_TERRI_BGM );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, SILENT_ENTER_TERRI_SE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, SILENT_ENTER_TERRI_ALL );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, DISABLE_SKIP );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, HIDE_FESTIVAL );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, DISABLE_STEALTH );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, ROLLBACK_HIDE_UI );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, LOCK_HUD );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, LOCK_HOTBAR );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, DISABLE_CANCEL_EMOTE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_AOE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_ALLIANCE_PC );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_ALLIANCE_BUDDY );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, INVIS_COMPANION );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, SET_BASE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, SET_INVIS_BASE );
      LUAU_ENUM_VALUE( e, Sapphire::EventFlags, SET_EOBJ_BASE );
    }
    {
      Bind::Enum e( reg, "EventHandlerType" );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Quest );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Warp );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, GatheringPoint );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Shop );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Aetheryte );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, GuildLeveAssignment );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, DefaultTalk );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Crafting );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, CustomTalk );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, CompanyLeveOfficer );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Array );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, CraftLeve );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, GimmickAccessor );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, GimmickBill );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, GimmickRect );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, ChocoboTaxiStand );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Opening );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, ExitRange );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Fish );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, GCShop );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, GuildOrderGuide );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, GuildOrderOfficer );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, ContentNpc );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Story );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, SpecialShop );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, BahamutGuide );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, InstanceContentGuide );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, HousingAethernet );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, SwitchTalk );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, Adventure );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, DailyQuestSupply );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, TripleTriad );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, ICDirector );
      LUAU_ENUM_VALUE( e, Event::EventHandler::EventHandlerType, QuestBattleDirector );
    }
    {
      Bind::Enum e( reg, "BNpcFlag" );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, Immobile );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, TurningDisabled );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, Invincible );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, StayAlive );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, NoDeaggro );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, Untargetable );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, AutoAttackDisabled );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, Invisible );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, NoRoam );
      LUAU_ENUM_VALUE( e, Entity::BNpcFlag, Intermission );
    }
    Bind::Enum( reg, "QuestAvailability" ).value( "Invisible", 0.0 ).value( "Available", 1.0 ).value( "Locked", 2.0 );
    Bind::Enum( reg, "InstanceState" )
      .value( "Created", 0.0 )
      .value( "DutyReset", 1 )
      .value( "DutyInProgress", 2 )
      .value( "DutyFinished", 3 )
      .value( "Terminate", 4 );
    Bind::Enum( reg, "EncounterStatus" )
      .value( "None", "none" )
      .value( "Idle", "idle" )
      .value( "Active", "active" )
      .value( "Success", "success" );

    {
      Bind::Enum e( reg, "ActionSkill" );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Sprint );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Teleport );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Return );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Rampart );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Convalescence );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Awareness );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Flash );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Sentinel );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Provoke );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, TemperedWill );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, FightOrFlight );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Bulwark );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, SkullSunder );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Maim );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, StormsPath );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, StormsEye );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, ButchersBlock );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, HeavyShot );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, StraightShot );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, HawksEye );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, VenomousBite );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, RagingStrikes );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, FlamingArrow );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, MiserysEnd );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, QuellingStrikes );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Swiftsong );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, QuickNock );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Narrage );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Shadowbind );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, BluntArrow );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Bloodletter );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, WideVolley );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, RepellingShot );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Windbite );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, MagesBallad );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, FoeRequiem );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, ArmysPaeon );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, RainOfDeath );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, BattleVoice );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, ClericStance );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Protect );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Fire );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Blizzard );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Thunder );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Transpose );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Triangulate );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, ShadeShift );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, KissOfTheWasp );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Hide );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, SneakAttack );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, KissOfTheViper );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, DeathBlossom );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Glamour );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Dye );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, EmpyrealArrow );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, TheWanderersMinuet );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, IronJaws );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, TheWardensPaean );
      LUAU_ENUM_VALUE( e, World::Action::ActionSkill, Sidewinder );
    }
    {
      Bind::Enum e( reg, "ActionStatus" );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Stun );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Bind );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Heavy );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Blind );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, SprintStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, RampartStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ConvalescenceStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, AwarenessStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, SentinelStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, TemperedWillStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, FightOrFlightStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, BulwarkStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Defiance );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Unchained );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Wrath );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, WrathII );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, WrathIII );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, WrathIV );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Infuriated );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, StraightShotReady );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, HawksEyeStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, VenomousBiteStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, RagingStrikesStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, BarrageStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, WindbiteStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, StraightShotStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, DownpourOfDeath );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, SwiftsongAura );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, SwiftsongStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, MagesBalladAura );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, MagesBalladStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ArmysPaeonAura );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ArmysPaeonStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, FoeRequiemAura );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, FoeRequiemStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, BattleVoiceStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ClericStanceStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ProtectCnjStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ThunderStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ThunderIIStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ThunderIIIStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ThundercloudStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, AstralFireStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, AstralFireIIStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, AstralFireIIIStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, UmbralIceStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, UmbralIceIIStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, UmbralIceIIIStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, TriangulateStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, RainOfDeathStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, FlamingArrowStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, InnerBeast );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, ShadeShiftStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, KissOfTheWaspStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, KissOfTheViperStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, Deliverance );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, TheWanderersMinuetStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, TheWardensPaeanStatus );
      LUAU_ENUM_VALUE( e, World::Action::ActionStatus, HiddenStatus );
    }
    {
      Bind::Enum e( reg, "ParamModifier" );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, None );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Strength );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Dexterity );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Vitality );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Intelligence );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Mind );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Piety );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, HP );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MP );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, TP );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, GP );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CP );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, PhysicalDamage );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MagicDamage );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Delay );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, AdditionalEffect );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, AttackSpeed );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, BlockRate );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, BlockStrength );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Parry );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, AttackPower );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Defense );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Accuracy );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Evasion );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MagicDefense );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CriticalHitPower );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CriticalHitResilience );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CriticalHit );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CriticalHitEvasion );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, SlashingResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, PiercingResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, BluntResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ProjectileResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, AttackMagicPotency );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, HealingMagicPotency );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, EnhancementMagicPotency );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalBonus );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, FireResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, IceResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, WindResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, EarthResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, LightningResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, WaterResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MagicResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Determination );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, SkillSpeed );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, SpellSpeed );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Haste );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Morale );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Enmity );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, EnmityReduction );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CarefulDesynthesis );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, EXPBonus );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Regen );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Refresh );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, TpRefresh );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MainAttribute );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, SecondaryAttribute );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, SlowResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, PetrificationResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ParalysisResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, SilenceResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, BlindResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, PoisonResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, StunResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, SleepResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, BindResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, HeavyResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, DoomResistance );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ReducedDurabilityLoss );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, IncreasedSpiritbondGain );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Craftsmanship );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Control );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Gathering );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, Perception );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, TickHeal );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, TickDamage );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, StrengthPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, DexterityPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, VitalityPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, IntelligencePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MindPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, PietyPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, HPPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MPPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, TPPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, GPPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CPPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, PhysicalDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MagicDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, AttackPowerPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, DefensePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, AccuracyPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, EvasionPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MagicDefensePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CriticalHitPowerPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CriticalHitResiliencePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, CriticalHitPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, EnmityPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, DamageDealtPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, DamageTakenPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, HealingMagicRecoveryPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, SlashingResistancePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, PiercingResistancePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, BluntResistancePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ProjectileResistancePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ParryPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalNoneDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalFireDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalIceDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalWindDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalEarthDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalLightningDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalWaterDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalUnaspectedDamagePercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MpRestRegenSuppressed );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, MpRestRegenPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalNoneMpCostPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalFireMpCostPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalIceMpCostPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalWindMpCostPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalEarthMpCostPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalLightningMpCostPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalWaterMpCostPercent );
      LUAU_ENUM_VALUE( e, Common::ParamModifier, ElementalUnaspectedMpCostPercent );
    }
    {
      Bind::Enum e( reg, "CalcResultType" );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeNone );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeMiss );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeResist );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeDamageHp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeRecoverHp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeCriticalDamageHp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeCriticalRecoverHp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeGuard );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeParry );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeInvalid );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeUneffective );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeNeglect );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeDamageMp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeRecoverMp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeDamageTp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeRecoverTp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeRecoverGp );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeSetStatus );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeSetStatusMe );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeResetStatus );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeResetStatusMe );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeResetBadStatus );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeUneffectiveStatus );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeHalfGoodStatus );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeHateDirect );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeHateIndirection );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeHateTop );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeHateAdd );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeHateMult );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeCombo );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeComboHit );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeCounter );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeDestruct );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeParalysis );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeKnockBack );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeDrawUpChairs );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeSucked );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeCtDrawUpChairs );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeLiveCallback );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeMount );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeArcherDot );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeMasterDot );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeBlessingOfGoddess );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeBadBreath );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeRevival );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypePet );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeBlow );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeStatusResist );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeClearPhysical );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeBNpcState );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeVfx );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeHardCode );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeCalcId );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeClearPvpPoint );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeCheckBarrier );
      LUAU_ENUM_VALUE( e, Common::CalcResultType, TypeReflect );
    }
    {
      Bind::Enum e( reg, "ActionResultFlag" );
      LUAU_ENUM_VALUE( e, Common::ActionResultFlag, None );
      LUAU_ENUM_VALUE( e, Common::ActionResultFlag, Absorbed );
      LUAU_ENUM_VALUE( e, Common::ActionResultFlag, EffectOnSource );
      LUAU_ENUM_VALUE( e, Common::ActionResultFlag, Reflected );
    }
    {
      Bind::Enum e( reg, "StatusEffectFlag" );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, BuffCategory );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, DebuffCategory );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, Permanent );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, IsGaze );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, Transfiguration );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, CanDispel );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, LockActions );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, LockControl );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, LockMovement );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, Invisibilty );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, CanStatusOff );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, FcBuff );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, RemoveOnSuccessfulHit );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, ReplaceSameCaster );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, GroundTarget );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, RemoveOnDeath );
      LUAU_ENUM_VALUE( e, Common::StatusEffectFlag, RemoveOnActionUse );
    }
    {
      Bind::Enum e( reg, "InvincibilityType" );
      LUAU_ENUM_VALUE( e, Common::InvincibilityType, InvincibilityNone );
      LUAU_ENUM_VALUE( e, Common::InvincibilityType, InvincibilityRefill );
      LUAU_ENUM_VALUE( e, Common::InvincibilityType, InvincibilityStayAlive );
      LUAU_ENUM_VALUE( e, Common::InvincibilityType, InvincibilityIgnoreDamage );
    }
    {
      Bind::Enum e( reg, "CurrencyType" );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, Gil );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, StormSeal );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, SerpentSeal );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, FlameSeal );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, TomestonePhilo );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, TomestoneMytho );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, WolfMark );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, TomestoneSold );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, AlliedSeal );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, Mgp );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, TomestonePoet );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, TomestoneLaw );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, TomestoneEso );
      LUAU_ENUM_VALUE( e, Common::CurrencyType, TomestoneLore );
    }
  }
}
