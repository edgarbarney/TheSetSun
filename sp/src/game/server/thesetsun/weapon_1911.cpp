//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		1911 - hand gun, based on HL2 357
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#include "soundent.h"
#include "game.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "te_effect_dispatch.h"
#include "rumble_shared.h"
#include "gamestats.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// CWeapon1911
//-----------------------------------------------------------------------------

#ifdef MAPBASE
extern acttable_t* GetPistolActtable();
extern int GetPistolActtableCount();
#endif

class CWeapon1911 : public CBaseHLCombatWeapon
{
	DECLARE_CLASS(CWeapon1911, CBaseHLCombatWeapon);
public:

	CWeapon1911(void);

	void	PrimaryAttack(void);

	void	MeleeAttack();

	void	Operator_HandleAnimEvent(animevent_t* pEvent, CBaseCombatCharacter* pOperator);

	virtual WeaponClass_t WeaponClassify() override { return WeaponClass_t::WEPCLASS_HANDGUN; }

	float	WeaponAutoAimScale() { return 0.6f; }

#ifdef MAPBASE
	int		CapabilitiesGet(void) { return bits_CAP_WEAPON_RANGE_ATTACK1; }
	Activity	GetPrimaryAttackActivity(void);

	virtual int	GetMinBurst() { return 1; }
	virtual int	GetMaxBurst() { return 1; }
	virtual float	GetMinRestTime(void) { return 1.0f; }
	virtual float	GetMaxRestTime(void) { return 2.5f; }

	virtual float GetFireRate(void) { return 1.0f; }

	virtual const Vector& GetBulletSpread(void)
	{
		static Vector cone = VECTOR_CONE_15DEGREES;
		if (!GetOwner() || !GetOwner()->IsNPC())
			return cone;

		static Vector AllyCone = VECTOR_CONE_2DEGREES;
		static Vector NPCCone = VECTOR_CONE_5DEGREES;

		if (GetOwner()->MyNPCPointer()->IsPlayerAlly())
		{
			// Based on 357 ally behaviour, 357 allies should be cooler
			return AllyCone;
		}

		return NPCCone;
	}

	void	FireNPCPrimaryAttack(CBaseCombatCharacter* pOperator, Vector& vecShootOrigin, Vector& vecShootDir);
	void	Operator_ForceNPCFire(CBaseCombatCharacter* pOperator, bool bSecondary);

	virtual acttable_t* GetBackupActivityList() { return GetPistolActtable(); }
	virtual int				GetBackupActivityListCount() { return GetPistolActtableCount(); }
#endif

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
#ifdef MAPBASE
	DECLARE_ACTTABLE();
#endif
};

LINK_ENTITY_TO_CLASS(weapon_1911, CWeapon1911);

PRECACHE_WEAPON_REGISTER(weapon_1911);

IMPLEMENT_SERVERCLASS_ST(CWeapon1911, DT_Weapon1911)
END_SEND_TABLE()

BEGIN_DATADESC(CWeapon1911)
END_DATADESC()

#ifdef MAPBASE
acttable_t	CWeapon1911::m_acttable[] =
{
#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_IDLE,						ACT_IDLE_REVOLVER,					true },
	{ ACT_IDLE_ANGRY,				ACT_IDLE_ANGRY_REVOLVER,				true },
	{ ACT_RANGE_ATTACK1,			ACT_RANGE_ATTACK_REVOLVER,			true },
	{ ACT_RELOAD,					ACT_RELOAD_REVOLVER,					true },
	{ ACT_WALK_AIM,					ACT_WALK_AIM_REVOLVER,				true },
	{ ACT_RUN_AIM,					ACT_RUN_AIM_REVOLVER,				true },
	{ ACT_GESTURE_RANGE_ATTACK1,	ACT_GESTURE_RANGE_ATTACK_REVOLVER,	true },
	{ ACT_RELOAD_LOW,				ACT_RELOAD_REVOLVER_LOW,				false },
	{ ACT_RANGE_ATTACK1_LOW,		ACT_RANGE_ATTACK_REVOLVER_LOW,		false },
	{ ACT_COVER_LOW,				ACT_COVER_REVOLVER_LOW,				false },
	{ ACT_RANGE_AIM_LOW,			ACT_RANGE_AIM_REVOLVER_LOW,			false },
	{ ACT_GESTURE_RELOAD,			ACT_GESTURE_RELOAD_REVOLVER,			false },
	{ ACT_WALK,						ACT_WALK_REVOLVER,					true },
	{ ACT_RUN,						ACT_RUN_REVOLVER,					true },
#else
	{ ACT_IDLE,						ACT_IDLE_PISTOL,				true },
	{ ACT_IDLE_ANGRY,				ACT_IDLE_ANGRY_PISTOL,			true },
	{ ACT_RANGE_ATTACK1,			ACT_RANGE_ATTACK_PISTOL,		true },
	{ ACT_RELOAD,					ACT_RELOAD_PISTOL,				true },
	{ ACT_WALK_AIM,					ACT_WALK_AIM_PISTOL,			true },
	{ ACT_RUN_AIM,					ACT_RUN_AIM_PISTOL,				true },
	{ ACT_GESTURE_RANGE_ATTACK1,	ACT_GESTURE_RANGE_ATTACK_PISTOL,true },
	{ ACT_RELOAD_LOW,				ACT_RELOAD_PISTOL_LOW,			false },
	{ ACT_RANGE_ATTACK1_LOW,		ACT_RANGE_ATTACK_PISTOL_LOW,	false },
	{ ACT_COVER_LOW,				ACT_COVER_PISTOL_LOW,			false },
	{ ACT_RANGE_AIM_LOW,			ACT_RANGE_AIM_PISTOL_LOW,		false },
	{ ACT_GESTURE_RELOAD,			ACT_GESTURE_RELOAD_PISTOL,		false },
	{ ACT_WALK,						ACT_WALK_PISTOL,				false },
	{ ACT_RUN,						ACT_RUN_PISTOL,					false },
#endif

	// 
	// Activities ported from weapon_alyxgun below
	// 

	// Readiness activities (not aiming)
#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_IDLE_RELAXED,				ACT_IDLE_PISTOL_RELAXED,		false },//never aims
	{ ACT_IDLE_STIMULATED,			ACT_IDLE_PISTOL_STIMULATED,		false },
#else
	{ ACT_IDLE_RELAXED,				ACT_IDLE_PISTOL,				false },//never aims
	{ ACT_IDLE_STIMULATED,			ACT_IDLE_STIMULATED,			false },
#endif
	{ ACT_IDLE_AGITATED,			ACT_IDLE_ANGRY_PISTOL,			false },//always aims
	{ ACT_IDLE_STEALTH,				ACT_IDLE_STEALTH_PISTOL,		false },

#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_WALK_RELAXED,				ACT_WALK_PISTOL_RELAXED,		false },//never aims
	{ ACT_WALK_STIMULATED,			ACT_WALK_PISTOL_STIMULATED,		false },
#else
	{ ACT_WALK_RELAXED,				ACT_WALK,						false },//never aims
	{ ACT_WALK_STIMULATED,			ACT_WALK_STIMULATED,			false },
#endif
	{ ACT_WALK_AGITATED,			ACT_WALK_AIM_PISTOL,			false },//always aims
	{ ACT_WALK_STEALTH,				ACT_WALK_STEALTH_PISTOL,		false },

#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_RUN_RELAXED,				ACT_RUN_PISTOL_RELAXED,			false },//never aims
	{ ACT_RUN_STIMULATED,			ACT_RUN_PISTOL_STIMULATED,		false },
#else
	{ ACT_RUN_RELAXED,				ACT_RUN,						false },//never aims
	{ ACT_RUN_STIMULATED,			ACT_RUN_STIMULATED,				false },
#endif
	{ ACT_RUN_AGITATED,				ACT_RUN_AIM_PISTOL,				false },//always aims
	{ ACT_RUN_STEALTH,				ACT_RUN_STEALTH_PISTOL,			false },

	// Readiness activities (aiming)
	{ ACT_IDLE_AIM_RELAXED,			ACT_IDLE_PISTOL,				false },//never aims	
	{ ACT_IDLE_AIM_STIMULATED,		ACT_IDLE_ANGRY_PISTOL,			false },
	{ ACT_IDLE_AIM_AGITATED,		ACT_IDLE_ANGRY_PISTOL,			false },//always aims
	{ ACT_IDLE_AIM_STEALTH,			ACT_IDLE_STEALTH_PISTOL,		false },

	{ ACT_WALK_AIM_RELAXED,			ACT_WALK,						false },//never aims
	{ ACT_WALK_AIM_STIMULATED,		ACT_WALK_AIM_PISTOL,			false },
	{ ACT_WALK_AIM_AGITATED,		ACT_WALK_AIM_PISTOL,			false },//always aims
	{ ACT_WALK_AIM_STEALTH,			ACT_WALK_AIM_STEALTH_PISTOL,	false },//always aims

	{ ACT_RUN_AIM_RELAXED,			ACT_RUN,						false },//never aims
	{ ACT_RUN_AIM_STIMULATED,		ACT_RUN_AIM_PISTOL,				false },
	{ ACT_RUN_AIM_AGITATED,			ACT_RUN_AIM_PISTOL,				false },//always aims
	{ ACT_RUN_AIM_STEALTH,			ACT_RUN_AIM_STEALTH_PISTOL,		false },//always aims
	//End readiness activities

	// Crouch activities
	{ ACT_CROUCHIDLE_STIMULATED,	ACT_CROUCHIDLE_STIMULATED,		false },
	{ ACT_CROUCHIDLE_AIM_STIMULATED,ACT_RANGE_AIM_PISTOL_LOW,		false },//always aims
	{ ACT_CROUCHIDLE_AGITATED,		ACT_RANGE_AIM_PISTOL_LOW,		false },//always aims

	// Readiness translations
	{ ACT_READINESS_RELAXED_TO_STIMULATED, ACT_READINESS_PISTOL_RELAXED_TO_STIMULATED, false },
	{ ACT_READINESS_RELAXED_TO_STIMULATED_WALK, ACT_READINESS_PISTOL_RELAXED_TO_STIMULATED_WALK, false },
	{ ACT_READINESS_AGITATED_TO_STIMULATED, ACT_READINESS_PISTOL_AGITATED_TO_STIMULATED, false },
	{ ACT_READINESS_STIMULATED_TO_RELAXED, ACT_READINESS_PISTOL_STIMULATED_TO_RELAXED, false },

#if EXPANDED_HL2_COVER_ACTIVITIES
	{ ACT_RANGE_AIM_MED,			ACT_RANGE_AIM_REVOLVER_MED,			false },
	{ ACT_RANGE_ATTACK1_MED,		ACT_RANGE_ATTACK_REVOLVER_MED,		false },
#endif

#ifdef MAPBASE
	// HL2:DM activities (for third-person animations in SP)
#if EXPANDED_HL2DM_ACTIVITIES
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_REVOLVER,                    false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_REVOLVER,                    false },
	{ ACT_HL2MP_WALK,					ACT_HL2MP_WALK_REVOLVER,                    false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_REVOLVER,            false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_REVOLVER,            false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_REVOLVER,    false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK2,	ACT_HL2MP_GESTURE_RANGE_ATTACK2_REVOLVER,    false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_REVOLVER,        false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_REVOLVER,                    false },
#else
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_PISTOL,                    false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_PISTOL,                    false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_PISTOL,            false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_PISTOL,            false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PISTOL,    false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_PISTOL,        false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_PISTOL,                    false },
#endif
#endif
};


IMPLEMENT_ACTTABLE(CWeapon1911);

// Allows Weapon_BackupActivity() to access the 1911's activity table.
acttable_t* Get1911Acttable()
{
	return CWeapon1911::m_acttable;
}

int Get1911ActtableCount()
{
	return ARRAYSIZE(CWeapon1911::m_acttable);
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CWeapon1911::CWeapon1911(void)
{
	m_bReloadsSingly = false;
	m_bFiresUnderwater = false;

#ifdef MAPBASE
	m_fMinRange1 = 24;
	m_fMaxRange1 = 1000;
	m_fMinRange2 = 24;
	m_fMaxRange2 = 200;
#endif

	m_vecADSOrigin.GetForModify() = Vector(0.0f, 0.0f, 0.0f);
	m_angADSAngles.GetForModify() = QAngle(0.0f, 0.0f, 0.0f);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeapon1911::Operator_HandleAnimEvent(animevent_t* pEvent, CBaseCombatCharacter* pOperator)
{
	switch (pEvent->event)
	{
#ifdef MAPBASE
	case EVENT_WEAPON_PISTOL_FIRE:
	{
		Vector vecShootOrigin, vecShootDir;
		vecShootOrigin = pOperator->Weapon_ShootPosition();

		CAI_BaseNPC* npc = pOperator->MyNPCPointer();
		ASSERT(npc != NULL);

		vecShootDir = npc->GetActualShootTrajectory(vecShootOrigin);

		FireNPCPrimaryAttack(pOperator, vecShootOrigin, vecShootDir);
	}
	break;
	default:
		BaseClass::Operator_HandleAnimEvent(pEvent, pOperator);
		break;
#endif
	}
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeapon1911::FireNPCPrimaryAttack(CBaseCombatCharacter* pOperator, Vector& vecShootOrigin, Vector& vecShootDir)
{
	CSoundEnt::InsertSound(SOUND_COMBAT | SOUND_CONTEXT_GUNFIRE, pOperator->GetAbsOrigin(), SOUNDENT_VOLUME_PISTOL, 0.2, pOperator, SOUNDENT_CHANNEL_WEAPON, pOperator->GetEnemy());

	WeaponSound(SINGLE_NPC);
	pOperator->FireBullets(1, vecShootOrigin, vecShootDir, VECTOR_CONE_PRECALCULATED, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 1);
	pOperator->DoMuzzleFlash();
	m_iClip1 = m_iClip1 - 1;
}

//-----------------------------------------------------------------------------
// Purpose: Some things need this. (e.g. the new Force(X)Fire inputs or blindfire actbusy)
//-----------------------------------------------------------------------------
void CWeapon1911::Operator_ForceNPCFire(CBaseCombatCharacter* pOperator, bool bSecondary)
{
	// Ensure we have enough rounds in the clip
	m_iClip1++;

	Vector vecShootOrigin, vecShootDir;
	QAngle	angShootDir;
	GetAttachment(LookupAttachment("muzzle"), vecShootOrigin, angShootDir);
	AngleVectors(angShootDir, &vecShootDir);
	FireNPCPrimaryAttack(pOperator, vecShootOrigin, vecShootDir);
}
#endif

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeapon1911::PrimaryAttack(void)
{
	// Only the player fires this way so we can cast
	CBasePlayer* pPlayer = ToBasePlayer(GetOwner());

	if (!pPlayer)
	{
		return;
	}

	if (m_iClip1 <= 0)
	{
		if (!m_bFireOnEmpty)
		{
			Reload();
		}
		else
		{
			WeaponSound(EMPTY);
			m_flNextPrimaryAttack = 0.15;
		}

		return;
	}

	m_iPrimaryAttacks++;
	gamestats->Event_WeaponFired(pPlayer, true, GetClassname());

	WeaponSound(SINGLE);
	pPlayer->DoMuzzleFlash();

	SendWeaponAnim(GetPrimaryAttackActivity());
	pPlayer->SetAnimation(PLAYER_ATTACK1);

	m_flNextPrimaryAttack = gpGlobals->curtime + 0.2f;
	m_flNextSecondaryAttack = gpGlobals->curtime + 0.2f;
	m_flNextMeleeAttack = gpGlobals->curtime + 0.2f;

	m_iClip1--;

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAiming = pPlayer->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT);

	pPlayer->FireBullets(1, vecSrc, vecAiming, vec3_origin, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0);

	pPlayer->SetMuzzleFlashTime(gpGlobals->curtime + 0.5);

	//Disorient the player
	QAngle angles = pPlayer->GetLocalAngles();

	angles.x += random->RandomInt(-1, 1);
	angles.y += random->RandomInt(-1, 1);
	angles.z = 0;

	pPlayer->SnapEyeAngles(angles);

	pPlayer->ViewPunch(QAngle(-1, random->RandomFloat(-2, 2), 0));

	CSoundEnt::InsertSound(SOUND_COMBAT, GetAbsOrigin(), 600, 0.2, GetOwner());

	if (!m_iClip1 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
	{
		// HEV suit - indicate out of ammo condition
		pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
	}
}

// Most of this is copied from crowbar
void CWeapon1911::MeleeAttack()
{
	// TODO: MOVE THESE VARS TO SCRIPT FILES OR SOMETHING
	constexpr int MELEE_HULL_DIM = 6;
	constexpr int MELEE_RANGE = 75.0f;
	const Vector g_bludgeonMins(-MELEE_HULL_DIM, -MELEE_HULL_DIM, -MELEE_HULL_DIM);
	const Vector g_bludgeonMaxs(MELEE_HULL_DIM, MELEE_HULL_DIM, MELEE_HULL_DIM);

	trace_t traceHit;

	// Try a ray
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (!pOwner)
		return;

	pOwner->RumbleEffect(RUMBLE_CROWBAR_SWING, 0, RUMBLE_FLAG_RESTART);

	Vector swingStart = pOwner->Weapon_ShootPosition();
	Vector forward;

	forward = pOwner->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT, MELEE_RANGE);

	Vector swingEnd = swingStart + forward * MELEE_RANGE;
	UTIL_TraceLine(swingStart, swingEnd, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit);
	Activity nHitActivity = (m_iClip1 < 1) ? ACT_VM_MELEEHIT_EMPTY : ACT_VM_MELEEHIT;

	// Like bullets, bludgeon traces have to trace against triggers.
	CTakeDamageInfo triggerInfo(GetOwner(), GetOwner(), cvar->FindVar("sk_plr_dmg_crowbar")->GetFloat(), DMG_CLUB);
	triggerInfo.SetDamagePosition(traceHit.startpos);
	triggerInfo.SetDamageForce(forward);
	TraceAttackToTriggers(triggerInfo, traceHit.startpos, traceHit.endpos, forward);

	if (traceHit.fraction == 1.0)
	{
		float bludgeonHullRadius = 1.732f * MELEE_HULL_DIM;  // hull is +/- 16, so use cuberoot of 2 to determine how big the hull is from center to the corner point

		// Back off by hull "radius"
		swingEnd -= forward * bludgeonHullRadius;

		UTIL_TraceHull(swingStart, swingEnd, g_bludgeonMins, g_bludgeonMaxs, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit);
		if (traceHit.fraction < 1.0 && traceHit.m_pEnt)
		{
			Vector vecToTarget = traceHit.m_pEnt->GetAbsOrigin() - swingStart;
			VectorNormalize(vecToTarget);

			float dot = vecToTarget.Dot(forward);

			// YWB:  Make sure they are sort of facing the guy at least...
			if (dot < 0.70721f)
			{
				// Force amiss
				traceHit.fraction = 1.0f;
			}
			else
			{
				nHitActivity = (m_iClip1 < 1) ? ACT_VM_MELEEHIT_EMPTY : ACT_VM_MELEEHIT;
			}
		}
	}

	m_iMeleeAttacks++;

	//gamestats->Event_WeaponFired(pOwner, !bIsSecondary, GetClassname());

	// -------------------------
	//	Miss
	// -------------------------
	if (traceHit.fraction == 1.0f)
	{
		nHitActivity = (m_iClip1 < 1) ? ACT_VM_MELEEMISS_EMPTY : ACT_VM_MELEEMISS;

		// We want to test the first swing again
		Vector testEnd = swingStart + forward * MELEE_RANGE;

		// Sound has been moved here since we're using the other melee sounds now
		WeaponSound(SINGLE);

		// See if we happened to hit water
		MeleeImpactWater(swingStart, testEnd);
	}
	else
	{
		// Other melee sounds
		if (traceHit.m_pEnt && traceHit.m_pEnt->IsWorld())
			WeaponSound(MELEE_HIT_WORLD);
		else if (traceHit.m_pEnt && !traceHit.m_pEnt->PassesDamageFilter(triggerInfo))
			WeaponSound(MELEE_MISS);
		else
			WeaponSound(MELEE_HIT);

		// =================
		// START HIT
		// =================

		AddViewKick();

		//Make sound for the AI
		CSoundEnt::InsertSound(SOUND_BULLET_IMPACT, traceHit.endpos, 400, 0.2f, pOwner);

		// This isn't great, but it's something for when the crowbar hits.
		pOwner->RumbleEffect(RUMBLE_AR2, 0, RUMBLE_FLAG_RESTART);

		CBaseEntity* pHitEntity = traceHit.m_pEnt;

		//Apply damage to a hit target
		if (pHitEntity != NULL)
		{
			Vector hitDirection;
			pOwner->EyeVectors(&hitDirection, NULL, NULL);
			VectorNormalize(hitDirection);

			CTakeDamageInfo info(GetOwner(), GetOwner(), cvar->FindVar("sk_plr_dmg_crowbar")->GetFloat(), DMG_CLUB);

			if (pOwner && pHitEntity->IsNPC())
			{
				// If bonking an NPC, adjust damage.
				info.AdjustPlayerDamageInflictedForSkillLevel();
			}

			CalculateMeleeDamageForce(&info, hitDirection, traceHit.endpos);

			pHitEntity->DispatchTraceAttack(info, hitDirection, &traceHit);
			ApplyMultiDamage();

			// Now hit all triggers along the ray that... 
			TraceAttackToTriggers(info, traceHit.startpos, traceHit.endpos, hitDirection);

			if (ToBaseCombatCharacter(pHitEntity))
			{
				gamestats->Event_WeaponHit(pOwner, false, GetClassname(), info);
			}
		}

		// Apply an impact effect
		MeleeImpactEffect(traceHit);
		
		// =================
		// END HIT
		// =================
	}

	// Send the anim
	SendWeaponAnim(nHitActivity);

	//Setup our next attack times
	m_flNextPrimaryAttack = gpGlobals->curtime + 0.2f;
	m_flNextSecondaryAttack = gpGlobals->curtime + 0.2f;
	m_flNextMeleeAttack = gpGlobals->curtime + 0.2f;

	pOwner->SetAnimation(PLAYER_ATTACK1);

}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
Activity CWeapon1911::GetPrimaryAttackActivity(void)
{
	if (Clip1() <= 1)
		return ACT_VM_PRIMARYATTACK_EMPTY;

	switch (RandomInt(1,4))
	{
	case 1:
	default:
		return ACT_VM_PRIMARYATTACK;
		break;
	case 2:
		return ACT_VM_RECOIL1;
		break;
	case 3:
		return ACT_VM_RECOIL2;
		break;
	case 4:
		return ACT_VM_RECOIL3;
		break;
	}
}
