//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "in_buttons.h"
#include "engine/IEngineSound.h"
#include "ammodef.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "physics_saverestore.h"
#include "datacache/imdlcache.h"
#include "activitylist.h"

// NVNT start extra includes
#include "haptics/haptic_utils.h"
#ifdef CLIENT_DLL
	#include "prediction.h"
#endif
// NVNT end extra includes

#if defined ( TF_DLL ) || defined ( TF_CLIENT_DLL )
#include "tf_shareddefs.h"
#endif

#if !defined( CLIENT_DLL )

// Game DLL Headers
#include "soundent.h"
#include "eventqueue.h"
#include "fmtstr.h"
#include "gameweaponmanager.h"

#ifdef HL2MP
	#include "hl2mp_gamerules.h"
#endif

#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The minimum time a hud hint for a weapon should be on screen. If we switch away before
// this, then teh hud hint counter will be deremented so the hint will be shown again, as
// if it had never been seen. The total display time for a hud hint is specified in client
// script HudAnimations.txt (which I can't read here). 
#define MIN_HUDHINT_DISPLAY_TIME 7.0f

#define HIDEWEAPON_THINK_CONTEXT			"BaseCombatWeapon_HideThink"

extern bool UTIL_ItemCanBeTouchedByPlayer( CBaseEntity *pItem, CBasePlayer *pPlayer );

#if defined ( TF_CLIENT_DLL ) || defined ( TF_DLL )
#ifdef _DEBUG
ConVar tf_weapon_criticals_force_random( "tf_weapon_criticals_force_random", "0", FCVAR_REPLICATED | FCVAR_CHEAT );
#endif // _DEBUG
ConVar tf_weapon_criticals_bucket_cap( "tf_weapon_criticals_bucket_cap", "1000.0", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar tf_weapon_criticals_bucket_bottom( "tf_weapon_criticals_bucket_bottom", "-250.0", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar tf_weapon_criticals_bucket_default( "tf_weapon_criticals_bucket_default", "300.0", FCVAR_REPLICATED | FCVAR_CHEAT );
#endif // TF

CBaseCombatWeapon::CBaseCombatWeapon()
{
	// Constructor must call this
	// CONSTRUCT_PREDICTABLE( CBaseCombatWeapon );

	// Some default values.  There should be set in the particular weapon classes
	m_fMinRange1		= 65;
	m_fMinRange2		= 65;
	m_fMaxRange1		= 1024;
	m_fMaxRange2		= 1024;

	m_bReloadsSingly	= false;

	// Defaults to zero
	m_nViewModelIndex	= 0;

	m_bFlipViewModel	= false;

#if defined( CLIENT_DLL )
	m_iState = m_iOldState = WEAPON_NOT_CARRIED;
	m_iClip1 = -1;
	m_iClip2 = -1;
	m_iPrimaryAmmoType = -1;
	m_iSecondaryAmmoType = -1;
#endif

#if !defined( CLIENT_DLL )
	m_pConstraint = NULL;
	OnBaseCombatWeaponCreated( this );
#endif

	m_hWeaponFileInfo = GetInvalidWeaponInfoHandle();

#if defined( TF_DLL )
	UseClientSideAnimation();
#endif

#if defined ( TF_CLIENT_DLL ) || defined ( TF_DLL )
	m_flCritTokenBucket = tf_weapon_criticals_bucket_default.GetFloat();
	m_nCritChecks = 1;
	m_nCritSeedRequests = 0;
#endif // TF

	m_vecADSOrigin.GetForModify() = Vector(0);
	m_angADSAngles.GetForModify() = QAngle(0, 0, 0);
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CBaseCombatWeapon::~CBaseCombatWeapon( void )
{
#if !defined( CLIENT_DLL )
	//Remove our constraint, if we have one
	if ( m_pConstraint != NULL )
	{
		physenv->DestroyConstraint( m_pConstraint );
		m_pConstraint = NULL;
	}
	OnBaseCombatWeaponDestroyed( this );
#endif
}

void CBaseCombatWeapon::Activate( void )
{
	BaseClass::Activate();

#ifndef CLIENT_DLL
	if ( GetOwnerEntity() )
		return;

	if ( g_pGameRules->IsAllowedToSpawn( this ) == false )
	{
		UTIL_Remove( this );
		return;
	}
#endif

}

void CBaseCombatWeapon::CalculateADS()
{
	static const auto defaultPos = Vector(0);
	static const auto defaultAng = QAngle(0, 0, 0);

	static float lastTime = gpGlobals->curtime;
	static float curAdsTime = 0.0f;
	static float timeDelta = 0.0f;

	timeDelta = gpGlobals->curtime - lastTime;
	lastTime = gpGlobals->curtime;

	if (m_bIsADS)
	{
		curAdsTime += timeDelta * 5.0f;

		m_flCurrentBobScale = GetWpnData().m_flADSBobScale;
		m_flCurrentSwayScale = GetWpnData().m_flADSSwayScale;
		m_flCurrentSwaySpeedScale = GetWpnData().m_flADSSwaySpeedScale;
	}
	else
	{
		curAdsTime -= timeDelta * 5.0f;

		m_flCurrentBobScale = GetWpnData().m_flBobScale;
		m_flCurrentSwayScale = GetWpnData().m_flSwayScale;
		m_flCurrentSwaySpeedScale = GetWpnData().m_flSwaySpeedScale;
	}

	if (curAdsTime > 1.0f)
		curAdsTime = 1.0f;

	if (curAdsTime < 0.0f)
		curAdsTime = 0.0f;

	m_vecADSOrigin.GetForModify() = Lerp(curAdsTime, defaultPos, GetWpnData().m_vecADSPosOffset);
	m_angADSAngles.GetForModify() = Lerp(curAdsTime, defaultAng, GetWpnData().m_angADSAngOffset);

}

void CBaseCombatWeapon::GiveDefaultAmmo( void )
{
	// If I use clips, set my clips to the default
	if ( UsesClipsForAmmo1() )
	{
		m_iClip1 = AutoFiresFullClip() ? 0 : GetDefaultClip1();
	}
	else
	{
		SetPrimaryAmmoCount( GetDefaultClip1() );
		m_iClip1 = WEAPON_NOCLIP;
	}
	if ( UsesClipsForAmmo2() )
	{
		m_iClip2 = GetDefaultClip2();
	}
	else
	{
		SetSecondaryAmmoCount( GetDefaultClip2() );
		m_iClip2 = WEAPON_NOCLIP;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Set mode to world model and start falling to the ground
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::Spawn( void )
{
	Precache();

	BaseClass::Spawn();

	SetSolid( SOLID_BBOX );
	m_flNextEmptySoundTime = 0.0f;

	// Weapons won't show up in trace calls if they are being carried...
	RemoveEFlags( EFL_USE_PARTITION_WHEN_NOT_SOLID );

	m_iState = WEAPON_NOT_CARRIED;
	// Assume 
	m_nViewModelIndex = 0;

#ifdef MAPBASE
	// Don't reset to default ammo if we're supposed to use the keyvalue
	if (!HasSpawnFlags( SF_WEAPON_PRESERVE_AMMO ))
#endif
	GiveDefaultAmmo();

	if ( GetWorldModel() )
	{
#ifdef MAPBASE
		SetModel( (GetDroppedModel() && GetDroppedModel()[0]) ? GetDroppedModel() : GetWorldModel() );
#else
		SetModel( GetWorldModel() );
#endif
	}

#if !defined( CLIENT_DLL )
	if( IsX360() )
	{
		AddEffects( EF_ITEM_BLINK );
	}

	FallInit();
	SetCollisionGroup( COLLISION_GROUP_WEAPON );
	m_takedamage = DAMAGE_EVENTS_ONLY;

	SetBlocksLOS( false );

	// Default to non-removeable, because we don't want the
	// game_weapon_manager entity to remove weapons that have
	// been hand-placed by level designers. We only want to remove
	// weapons that have been dropped by NPC's.
	SetRemoveable( false );
#endif

	// Bloat the box for player pickup
	CollisionProp()->UseTriggerBounds( true, 36 );

	// Use more efficient bbox culling on the client. Otherwise, it'll setup bones for most
	// characters even when they're not in the frustum.
	AddEffects( EF_BONEMERGE_FASTCULL );

	m_iReloadHudHintCount = 0;
	m_iAltFireHudHintCount = 0;
	m_flHudHintMinDisplayTime = 0;
}

//-----------------------------------------------------------------------------
// Purpose: get this game's encryption key for decoding weapon kv files
// Output : virtual const unsigned char
//-----------------------------------------------------------------------------
const unsigned char *CBaseCombatWeapon::GetEncryptionKey( void ) 
{ 
	return g_pGameRules->GetEncryptionKey(); 
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::Precache( void )
{
	// Let's initialise this shite
	m_iWeaponSlot = 1;
	m_iWeaponSlotPosition = 1;

	m_flCurrentViewmodelFOV = GetWpnData().m_flViewmodelFOV;
	m_flCurrentBobScale = GetWpnData().m_flBobScale;
	m_flCurrentSwayScale = GetWpnData().m_flSwayScale;
	m_flCurrentSwaySpeedScale = GetWpnData().m_flSwaySpeedScale;

#if defined( CLIENT_DLL )
	Assert( Q_strlen( GetClassname() ) > 0 );
	// Msg( "Client got %s\n", GetClassname() );
#endif
	m_iPrimaryAmmoType = m_iSecondaryAmmoType = -1;

	// Add this weapon to the weapon registry, and get our index into it
	// Get weapon data from script file
#ifdef MAPBASE
	// Allow custom scripts to be loaded on a map-by-map basis
	if ( ReadCustomWeaponDataFromFileForSlot( filesystem, GetWeaponScriptName(), &m_hWeaponFileInfo, GetEncryptionKey() ) || 
		ReadWeaponDataFromFileForSlot( filesystem, GetWeaponScriptName(), &m_hWeaponFileInfo, GetEncryptionKey() ) )
#else
	if ( ReadWeaponDataFromFileForSlot( filesystem, GetClassname(), &m_hWeaponFileInfo, GetEncryptionKey() ) )
#endif
	{
		// Get the ammo indexes for the ammo's specified in the data file
		if ( GetWpnData().szAmmo1[0] )
		{
			m_iPrimaryAmmoType = GetAmmoDef()->Index( GetWpnData().szAmmo1 );
			if (m_iPrimaryAmmoType == -1)
			{
				Msg("ERROR: Weapon (%s) using undefined primary ammo type (%s)\n",GetClassname(), GetWpnData().szAmmo1);
			}
 #if defined ( TF_DLL ) || defined ( TF_CLIENT_DLL )
			// Ammo override
			int iModUseMetalOverride = 0;
			CALL_ATTRIB_HOOK_INT( iModUseMetalOverride, mod_use_metal_ammo_type );
			if ( iModUseMetalOverride )
			{
				m_iPrimaryAmmoType = (int)TF_AMMO_METAL;
			}
#endif
 		}
		if ( GetWpnData().szAmmo2[0] )
		{
			m_iSecondaryAmmoType = GetAmmoDef()->Index( GetWpnData().szAmmo2 );
			if (m_iSecondaryAmmoType == -1)
			{
				Msg("ERROR: Weapon (%s) using undefined secondary ammo type (%s)\n",GetClassname(),GetWpnData().szAmmo2);
			}

		}
#if defined( CLIENT_DLL )
		gWR.LoadWeaponSprites( GetWeaponFileInfoHandle() );
#endif
		// Precache models (preload to avoid hitch)
		m_iViewModelIndex = 0;
		m_iWorldModelIndex = 0;
		if ( GetViewModel() && GetViewModel()[0] )
		{
			m_iViewModelIndex = CBaseEntity::PrecacheModel( GetViewModel() );
		}
		if ( GetWorldModel() && GetWorldModel()[0] )
		{
			m_iWorldModelIndex = CBaseEntity::PrecacheModel( GetWorldModel() );
		}
#ifdef MAPBASE
		m_iDroppedModelIndex = 0;
		if ( GetDroppedModel() && GetDroppedModel()[0] )
		{
			m_iDroppedModelIndex = CBaseEntity::PrecacheModel( GetDroppedModel() );
		}
		else
		{
			// Use the world model index
			m_iDroppedModelIndex = m_iWorldModelIndex;
		}
#endif

		// Precache sounds, too
		for ( int i = 0; i < NUM_SHOOT_SOUND_TYPES; ++i )
		{
			const char *shootsound = GetShootSound( i );
			if ( shootsound && shootsound[0] )
			{
				CBaseEntity::PrecacheScriptSound( shootsound );
			}
		}
	}
	else
	{
		// Couldn't read data file, remove myself
		Warning( "Error reading weapon data file for: %s\n", GetClassname() );
	//	Remove( );	//don't remove, this gets released soon!
	}
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: Sets ammo based on mapper value
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SetAmmoFromMapper( float flAmmo, bool bSecondary )
{
	int iFinalAmmo;
	if (flAmmo > 0.0f && flAmmo < 1.0f)
	{
		// Ratio from max ammo
		iFinalAmmo = ((float)(!bSecondary ? GetMaxClip1() : GetMaxClip2()) * flAmmo);
	}
	else
	{
		// Actual ammo value
		iFinalAmmo = (int)flAmmo;
	}

	!bSecondary ?
		m_iClip1 = iFinalAmmo :
		m_iClip2 = iFinalAmmo;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::KeyValue( const char *szKeyName, const char *szValue )
{
	if ( FStrEq(szKeyName, "SetAmmo1") )
	{
		SetAmmoFromMapper(atof(szValue));
	}
	if ( FStrEq(szKeyName, "SetAmmo2") )
	{
		SetAmmoFromMapper(atof(szValue), true);
	}
	else if ( FStrEq(szKeyName, "spawnflags") )
	{
		m_spawnflags = atoi(szValue);
#ifndef CLIENT_DLL
		// Some spawnflags have to be on the client right now
		if (m_spawnflags != 0)
			DispatchUpdateTransmitState();
#endif
	}
	else
		return BaseClass::KeyValue( szKeyName, szValue );

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::GetKeyValue( const char *szKeyName, char *szValue, int iMaxLen )
{
	return BaseClass::GetKeyValue(szKeyName, szValue, iMaxLen);
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Get my data in the file weapon info array
//-----------------------------------------------------------------------------
const FileWeaponInfo_t &CBaseCombatWeapon::GetWpnData( void ) const
{
	return *GetFileWeaponInfoFromHandle( m_hWeaponFileInfo );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CBaseCombatWeapon::GetViewModel( int /*viewmodelindex = 0 -- this is ignored in the base class here*/ ) const
{
	return GetWpnData().szViewModel;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CBaseCombatWeapon::GetWorldModel( void ) const
{
	return GetWpnData().szWorldModel;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CBaseCombatWeapon::GetAnimPrefix( void ) const
{
	return GetWpnData().szAnimationPrefix;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : char const
//-----------------------------------------------------------------------------
const char *CBaseCombatWeapon::GetPrintName( void ) const
{
	return GetWpnData().szPrintName;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetMaxClip1( void ) const
{
#if defined ( TF_DLL ) || defined ( TF_CLIENT_DLL )
	int iModMaxClipOverride = 0;
	CALL_ATTRIB_HOOK_INT( iModMaxClipOverride, mod_max_primary_clip_override );
	if ( iModMaxClipOverride != 0 )
		return iModMaxClipOverride;
#endif

	return GetWpnData().iMaxClip1;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetMaxClip2( void ) const
{
	return GetWpnData().iMaxClip2;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetDefaultClip1( void ) const
{
	return GetWpnData().iDefaultClip1;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetDefaultClip2( void ) const
{
	return GetWpnData().iDefaultClip2;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::UsesClipsForAmmo1( void ) const
{
	return ( GetMaxClip1() != WEAPON_NOCLIP );
}

bool CBaseCombatWeapon::IsMeleeWeapon() const
{
	return GetWpnData().m_bMeleeWeapon;
}

#ifdef MAPBASE
float CBaseCombatWeapon::GetViewmodelFOVOverride() const
{
	return m_flCurrentViewmodelFOV;
}

float CBaseCombatWeapon::GetBobScale() const
{
	return m_flCurrentBobScale;
}

float CBaseCombatWeapon::GetSwayScale() const
{
	return m_flCurrentSwayScale;
}

float CBaseCombatWeapon::GetSwaySpeedScale() const
{
	return m_flCurrentSwaySpeedScale;
}

const char *CBaseCombatWeapon::GetDroppedModel() const
{
	return GetWpnData().szDroppedModel;
}

bool CBaseCombatWeapon::UsesHands() const
{
	return GetWpnData().m_bUsesHands;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::UsesClipsForAmmo2( void ) const
{
	return ( GetMaxClip2() != WEAPON_NOCLIP );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetWeight( void ) const
{
	return GetWpnData().iWeight;
}

//-----------------------------------------------------------------------------
// Purpose: Whether this weapon can be autoswitched to when the player runs out
//			of ammo in their current weapon or they pick this weapon up.
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::AllowsAutoSwitchTo( void ) const
{
	return GetWpnData().bAutoSwitchTo;
}

//-----------------------------------------------------------------------------
// Purpose: Whether this weapon can be autoswitched away from when the player
//			runs out of ammo in this weapon or picks up another weapon or ammo.
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::AllowsAutoSwitchFrom( void ) const
{
	ConVar* pAutoSwitch = cvar->FindVar("cl_autoswitch");

	if (!pAutoSwitch->GetBool())
		return false;

	return GetWpnData().bAutoSwitchFrom;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetWeaponFlags( void ) const
{
	return GetWpnData().iFlags;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetSlot( void ) const
{
	return m_iWeaponSlot;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SetSlot(int slotToSet)
{
	m_iWeaponSlot = slotToSet;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetPosition( void ) const
{
	return m_iWeaponSlotPosition;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SetPosition(int posToSet)
{
	m_iWeaponSlotPosition = posToSet;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CBaseCombatWeapon::GetName( void ) const
{
	return GetWpnData().szClassName;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudTexture const *CBaseCombatWeapon::GetSpriteActive( void ) const
{
	return GetWpnData().iconActive;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudTexture const *CBaseCombatWeapon::GetSpriteInactive( void ) const
{
	return GetWpnData().iconInactive;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudTexture const *CBaseCombatWeapon::GetSpriteAmmo( void ) const
{
	return GetWpnData().iconAmmo;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudTexture const *CBaseCombatWeapon::GetSpriteAmmo2( void ) const
{
	return GetWpnData().iconAmmo2;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudTexture const *CBaseCombatWeapon::GetSpriteCrosshair( void ) const
{
	return GetWpnData().iconCrosshair;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudTexture const *CBaseCombatWeapon::GetSpriteAutoaim( void ) const
{
	return GetWpnData().iconAutoaim;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudTexture const *CBaseCombatWeapon::GetSpriteZoomedCrosshair( void ) const
{
	return GetWpnData().iconZoomedCrosshair;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudTexture const *CBaseCombatWeapon::GetSpriteZoomedAutoaim( void ) const
{
	return GetWpnData().iconZoomedAutoaim;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CBaseCombatWeapon::GetShootSound( int iIndex ) const
{
	return GetWpnData().aShootSounds[ iIndex ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetRumbleEffect() const
{
	return GetWpnData().iRumbleEffect;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseCombatCharacter	*CBaseCombatWeapon::GetOwner() const
{
	return ToBaseCombatCharacter( m_hOwner.Get() );
}	

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : BaseCombatCharacter - 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SetOwner( CBaseCombatCharacter *owner )
{
	if ( !owner )
	{ 
#ifndef CLIENT_DLL
		// Make sure the weapon updates its state when it's removed from the player
		// We have to force an active state change, because it's being dropped and won't call UpdateClientData()
		int iOldState = m_iState;
		m_iState = WEAPON_NOT_CARRIED;
		OnActiveStateChanged( iOldState );
#endif

		// make sure we clear out our HideThink if we have one pending
		SetContextThink( NULL, 0, HIDEWEAPON_THINK_CONTEXT );
	}

	m_hOwner = owner;
	
#ifndef CLIENT_DLL
	DispatchUpdateTransmitState();
#else
	UpdateVisibility();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Return false if this weapon won't let the player switch away from it
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::IsAllowedToSwitch( void )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Return true if this weapon can be selected via the weapon selection
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::CanBeSelected( void )
{
	if ( !VisibleInWeaponSelection() )
		return false;

	return HasAmmo();
}

//-----------------------------------------------------------------------------
// Purpose: Return true if this weapon has some ammo
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::HasAmmo( void )
{
	// Weapons with no ammo types can always be selected
	if ( m_iPrimaryAmmoType == -1 && m_iSecondaryAmmoType == -1  )
		return true;
	if ( GetWeaponFlags() & ITEM_FLAG_SELECTONEMPTY )
		return true;

	CBasePlayer *player = ToBasePlayer( GetOwner() );
	if ( !player )
		return false;
	return ( m_iClip1 > 0 || player->GetAmmoCount( m_iPrimaryAmmoType ) || m_iClip2 > 0 || player->GetAmmoCount( m_iSecondaryAmmoType ) );
}

//-----------------------------------------------------------------------------
// Purpose: Return true if this weapon should be seen, and hence be selectable, in the weapon selection
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::VisibleInWeaponSelection( void )
{
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::HasWeaponIdleTimeElapsed( void )
{
	if ( gpGlobals->curtime > m_flTimeWeaponIdle )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : time - 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SetWeaponIdleTime( float time )
{
	m_flTimeWeaponIdle = time;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CBaseCombatWeapon::GetWeaponIdleTime( void )
{
	return m_flTimeWeaponIdle;
}

//-----------------------------------------------------------------------------
// Purpose: Drop/throw the weapon with the given velocity.
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::Drop( const Vector &vecVelocity )
{
#if !defined( CLIENT_DLL )

	// Once somebody drops a gun, it's fair game for removal when/if
	// a game_weapon_manager does a cleanup on surplus weapons in the
	// world.
	SetRemoveable( true );
	WeaponManager_AmmoMod( this );

	//If it was dropped then there's no need to respawn it.
	AddSpawnFlags( SF_NORESPAWN );

	StopAnimation();
	StopFollowingEntity( );
	SetMoveType( MOVETYPE_FLYGRAVITY );
	// clear follow stuff, setup for collision
	SetGravity(1.0);
	m_iState = WEAPON_NOT_CARRIED;
	RemoveEffects( EF_NODRAW );
	FallInit();
	SetGroundEntity( NULL );
	SetThink( &CBaseCombatWeapon::SetPickupUse );
	SetTouch(NULL);

	if( hl2_episodic.GetBool() )
	{
		RemoveSpawnFlags( SF_WEAPON_NO_PLAYER_PICKUP );
	}

	IPhysicsObject *pObj = VPhysicsGetObject();
	if ( pObj != NULL )
	{
		AngularImpulse	angImp( 200, 200, 200 );
		pObj->AddVelocity( &vecVelocity, &angImp );
	}
	else
	{
		SetAbsVelocity( vecVelocity );
	}

	CBaseEntity *pOwner = GetOwnerEntity();

	SetNextThink( gpGlobals->curtime + 1.0f );
	SetOwnerEntity( NULL );
	SetOwner( NULL );

#ifdef MAPBASE
	m_bInReload = false;

	m_OnDropped.FireOutput(pOwner, this);
#endif

	// If we're not allowing to spawn due to the gamerules,
	// remove myself when I'm dropped by an NPC.
	if ( pOwner && pOwner->IsNPC() )
	{
		if ( g_pGameRules->IsAllowedToSpawn( this ) == false )
		{
			UTIL_Remove( this );
			return;
		}
	}
#endif
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPicker - 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::OnPickedUp( CBaseCombatCharacter *pNewOwner )
{
#if !defined( CLIENT_DLL )
	RemoveEffects( EF_ITEM_BLINK );

	if( pNewOwner->IsPlayer() )
	{
		m_OnPlayerPickup.FireOutput(pNewOwner, this);

		// Play the pickup sound for 1st-person observers
		CRecipientFilter filter;
		for ( int i=1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = UTIL_PlayerByIndex(i);
			if ( player && !player->IsAlive() && player->GetObserverMode() == OBS_MODE_IN_EYE )
			{
				filter.AddRecipient( player );
			}
		}
		if ( filter.GetRecipientCount() )
		{
			CBaseEntity::EmitSound( filter, pNewOwner->entindex(), "Player.PickupWeapon" );
		}

		// Robin: We don't want to delete weapons the player has picked up, so 
		// clear the name of the weapon. This prevents wildcards that are meant 
		// to find NPCs finding weapons dropped by the NPCs as well.
#ifdef MAPBASE
		// Level designers might want some weapons to preserve their original names, however.
		if ( !HasSpawnFlags(SF_WEAPON_PRESERVE_NAME) )
#endif
		SetName( NULL_STRING );
	}
	else
	{
		m_OnNPCPickup.FireOutput(pNewOwner, this);
	}

#ifdef HL2MP
	HL2MPRules()->RemoveLevelDesignerPlacedObject( this );
#endif

	// Someone picked me up, so make it so that I can't be removed.
	SetRemoveable( false );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &vecTracerSrc - 
//			&tr - 
//			iTracerType - 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType )
{
	CBaseEntity *pOwner = GetOwner();

	if ( pOwner == NULL )
	{
		BaseClass::MakeTracer( vecTracerSrc, tr, iTracerType );
		return;
	}

	const char *pszTracerName = GetTracerType();

	Vector vNewSrc = vecTracerSrc;
	int iEntIndex = pOwner->entindex();

	if ( g_pGameRules->IsMultiplayer() )
	{
		iEntIndex = entindex();
	}

	int iAttachment = GetTracerAttachment();

	switch ( iTracerType )
	{
	case TRACER_LINE:
		UTIL_Tracer( vNewSrc, tr.endpos, iEntIndex, iAttachment, 0.0f, true, pszTracerName );
		break;

	case TRACER_LINE_AND_WHIZ:
		UTIL_Tracer( vNewSrc, tr.endpos, iEntIndex, iAttachment, 0.0f, true, pszTracerName );
		break;
	}
}

void CBaseCombatWeapon::GiveTo( CBaseEntity *pOther )
{
	DefaultUse( pOther, pOther, USE_TYPE::USE_ON, 0);
}

//-----------------------------------------------------------------------------
// Purpose: Default use function for player picking up a weapon (not AI)
// Input  : pOther - the entity that touched me
// Output :
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::DefaultUse(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value)
{
#if !defined( CLIENT_DLL )
	// Can't pick up dissolving weapons
	if (IsDissolving())
		return;

	// if it's not a player, ignore
	CBasePlayer* pPlayer = ToBasePlayer(pActivator);
	if (!pPlayer)
		return;

	if (UTIL_ItemCanBeTouchedByPlayer(this, pPlayer))
	{
		// This makes sure the player could potentially take the object
		// before firing the cache interaction output. That doesn't mean
		// the player WILL end up taking the object, but cache interactions
		// are fired as soon as you prove you have found the object, not
		// when you finally acquire it.
		m_OnCacheInteraction.FireOutput(pActivator, this);
	}

	if (HasSpawnFlags(SF_WEAPON_NO_PLAYER_PICKUP))
		return;

	if (pPlayer->PickupWeapon(this))
	{
		OnPickedUp(pPlayer);
	}
#endif
}

//---------------------------------------------------------
// It's OK for base classes to override this completely 
// without calling up. (sjb)
//---------------------------------------------------------
bool CBaseCombatWeapon::ShouldDisplayAltFireHUDHint()
{
	if( m_iAltFireHudHintCount >= WEAPON_RELOAD_HUD_HINT_COUNT )
		return false;

	if( UsesSecondaryAmmo() && HasSecondaryAmmo() )
	{
		return true;
	}

	if( !UsesSecondaryAmmo() && HasPrimaryAmmo() )
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::DisplayAltFireHudHint()
{
#if !defined( CLIENT_DLL )
	CFmtStr hint;
	hint.sprintf( "#valve_hint_alt_%s", GetClassname() );
	UTIL_HudHintText( GetOwner(), hint.Access() );
	m_iAltFireHudHintCount++;
	m_bAltFireHudHintDisplayed = true;
	m_flHudHintMinDisplayTime = gpGlobals->curtime + MIN_HUDHINT_DISPLAY_TIME;
#endif//CLIENT_DLL
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::RescindAltFireHudHint()
{
#if !defined( CLIENT_DLL )
	Assert(m_bAltFireHudHintDisplayed);
	
	UTIL_HudHintText( GetOwner(), "" );
	--m_iAltFireHudHintCount;
	m_bAltFireHudHintDisplayed = false;
#endif//CLIENT_DLL
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::ShouldDisplayReloadHUDHint()
{
	if( m_iReloadHudHintCount >= WEAPON_RELOAD_HUD_HINT_COUNT )
		return false;

	CBaseCombatCharacter *pOwner = GetOwner();

	if( pOwner != NULL && pOwner->IsPlayer() && UsesClipsForAmmo1() && m_iClip1 < (GetMaxClip1() / 2) )
	{
		// I'm owned by a player, I use clips, I have less then half a clip loaded. Now, does the player have more ammo?
		if ( pOwner )
		{
			if ( pOwner->GetAmmoCount( m_iPrimaryAmmoType ) > 0 ) 
				return true;
		}
	}

	return false;
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::DisplayReloadHudHint()
{
#if !defined( CLIENT_DLL )
	UTIL_HudHintText( GetOwner(), "valve_hint_reload" );
	m_iReloadHudHintCount++;
	m_bReloadHudHintDisplayed = true;
	m_flHudHintMinDisplayTime = gpGlobals->curtime + MIN_HUDHINT_DISPLAY_TIME;
#endif//CLIENT_DLL
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::RescindReloadHudHint()
{
#if !defined( CLIENT_DLL )
	Assert(m_bReloadHudHintDisplayed);

	UTIL_HudHintText( GetOwner(), "" );
	--m_iReloadHudHintCount;
	m_bReloadHudHintDisplayed = false;
#endif//CLIENT_DLL
}


void CBaseCombatWeapon::SetPickupUse( void )
{
#if !defined( CLIENT_DLL )
	SetUse(&CBaseCombatWeapon::DefaultUse);

	if ( gpGlobals->maxClients > 1 )
	{
		if ( GetSpawnFlags() & SF_NORESPAWN )
		{
			SetThink( &CBaseEntity::SUB_Remove );
			SetNextThink( gpGlobals->curtime + 30.0f );
		}
	}

#endif
}


#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
WeaponClass_t CBaseCombatWeapon::WeaponClassify()
{
	// For now, check how we map our "angry idle" activity.
	// The function is virtual, so derived weapons can override this.
	Activity idleact = ActivityOverride(ACT_IDLE_ANGRY, NULL);
	switch (idleact)
	{
#if EXPANDED_HL2_WEAPON_ACTIVITIES
	case ACT_IDLE_ANGRY_REVOLVER:
#endif
	case ACT_IDLE_ANGRY_PISTOL:		return WEPCLASS_HANDGUN;
#if EXPANDED_HL2_WEAPON_ACTIVITIES
	case ACT_IDLE_ANGRY_CROSSBOW:	// For now, crossbows are rifles
#endif
#if EXPANDED_HL2_UNUSED_WEAPON_ACTIVITIES
	case ACT_IDLE_ANGRY_AR1:
	case ACT_IDLE_ANGRY_SMG2:
	case ACT_IDLE_ANGRY_SNIPER_RIFLE:
#endif
	case ACT_IDLE_ANGRY_SMG1:
	case ACT_IDLE_ANGRY_AR2:		return WEPCLASS_RIFLE;
	case ACT_IDLE_ANGRY_SHOTGUN:	return WEPCLASS_SHOTGUN;
	case ACT_IDLE_ANGRY_RPG:		return WEPCLASS_HEAVY;

	case ACT_IDLE_ANGRY_MELEE:		return WEPCLASS_MELEE;
	}
	return WEPCLASS_INVALID;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
WeaponClass_t CBaseCombatWeapon::WeaponClassFromString(const char *str)
{
	if (FStrEq(str, "WEPCLASS_HANDGUN"))
		return WEPCLASS_HANDGUN;
	else if (FStrEq(str, "WEPCLASS_RIFLE"))
		return WEPCLASS_RIFLE;
	else if (FStrEq(str, "WEPCLASS_SHOTGUN"))
		return WEPCLASS_SHOTGUN;
	else if (FStrEq(str, "WEPCLASS_HEAY"))
		return WEPCLASS_HEAVY;

	else if (FStrEq(str, "WEPCLASS_MELEE"))
		return WEPCLASS_MELEE;

	return WEPCLASS_INVALID;
}

#ifdef HL2_DLL
extern acttable_t *GetSMG1Acttable();
extern int GetSMG1ActtableCount();

extern acttable_t *GetAR2Acttable();
extern int GetAR2ActtableCount();

extern acttable_t *GetShotgunActtable();
extern int GetShotgunActtableCount();

extern acttable_t *GetPistolActtable();
extern int GetPistolActtableCount();

extern acttable_t *Get357Acttable();
extern int Get357ActtableCount();
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::SupportsBackupActivity(Activity activity)
{
	// Derived classes should override this.

#ifdef HL2_DLL
	// Melee users should not use SMG animations for missing activities.
	if (IsMeleeWeapon() && GetBackupActivityList() == GetSMG1Acttable())
		return false;
#endif

	return true;
}

acttable_t *CBaseCombatWeapon::GetBackupActivityList()
{
	return NULL;
}

int CBaseCombatWeapon::GetBackupActivityListCount()
{
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
acttable_t *CBaseCombatWeapon::GetDefaultBackupActivityList( acttable_t *pTable, int &actCount )
{
#ifdef HL2_DLL
	// Ensure this isn't already a default backup activity list
	if (pTable == GetSMG1Acttable() || pTable == GetPistolActtable())
		return NULL;

	// Use a backup table based on what ACT_IDLE_ANGRY is translated to
	Activity actTranslated = ACT_INVALID;
	for ( int i = 0; i < actCount; i++, pTable++ )
	{
		if ( pTable->baseAct == ACT_IDLE_ANGRY )
		{
			actTranslated = (Activity)pTable->weaponAct;
			break;
		}
	}

	if (actTranslated == ACT_INVALID)
		return NULL;

	switch (actTranslated)
	{
#if EXPANDED_HL2_WEAPON_ACTIVITIES
		case ACT_IDLE_ANGRY_REVOLVER:
#endif
		case ACT_IDLE_ANGRY_PISTOL:
			{
				actCount = GetPistolActtableCount();
				return GetPistolActtable();
			}
#if EXPANDED_HL2_WEAPON_ACTIVITIES
		case ACT_IDLE_ANGRY_CROSSBOW:	// For now, crossbows are rifles
#endif
#if EXPANDED_HL2_UNUSED_WEAPON_ACTIVITIES
		case ACT_IDLE_ANGRY_AR1:
		case ACT_IDLE_ANGRY_SMG2:
		case ACT_IDLE_ANGRY_SNIPER_RIFLE:
#endif
		case ACT_IDLE_ANGRY_SMG1:
		case ACT_IDLE_ANGRY_AR2:
		case ACT_IDLE_ANGRY_SHOTGUN:
		case ACT_IDLE_ANGRY_RPG:
			{
				actCount = GetSMG1ActtableCount();
				return GetSMG1Acttable();
			}
	}
#endif

	actCount = 0;
	return NULL;
}
#endif


//-----------------------------------------------------------------------------
// Purpose: Become a child of the owner (MOVETYPE_FOLLOW)
//			disables collisions, touch functions, thinking
// Input  : *pOwner - new owner/operator
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::Equip( CBaseCombatCharacter *pOwner )
{
	// Attach the weapon to an owner
	SetAbsVelocity( vec3_origin );
	RemoveSolidFlags( FSOLID_TRIGGER );
	FollowEntity( pOwner );
	SetOwner( pOwner );
	SetOwnerEntity( pOwner );

	// Break any constraint I might have to the world.
	RemoveEffects( EF_ITEM_BLINK );

#if !defined( CLIENT_DLL )
	if ( m_pConstraint != NULL )
	{
		RemoveSpawnFlags( SF_WEAPON_START_CONSTRAINED );
		physenv->DestroyConstraint( m_pConstraint );
		m_pConstraint = NULL;
	}
#endif

#ifdef MAPBASE
	// Ammo may be overridden to 0, in which case we shouldn't autoswitch
	if (m_iClip1 <= 0 && m_iClip2 <= 0)
		AddSpawnFlags(SF_WEAPON_NO_AUTO_SWITCH_WHEN_EMPTY);
#endif


	m_flNextPrimaryAttack		= gpGlobals->curtime;
	m_flNextSecondaryAttack		= gpGlobals->curtime;
	SetTouch( NULL );
	SetThink( NULL );
#if !defined( CLIENT_DLL )
	VPhysicsDestroyObject();
#endif

	if ( pOwner->IsPlayer() )
	{
		SetModel( GetViewModel() );
	}
	else
	{
		// Make the weapon ready as soon as any NPC picks it up.
		m_flNextPrimaryAttack = gpGlobals->curtime;
		m_flNextSecondaryAttack = gpGlobals->curtime;
		SetModel( GetWorldModel() );
	}
}

void CBaseCombatWeapon::SetActivity( Activity act, float duration ) 
{ 
	//Adrian: Oh man...
#if !defined( CLIENT_DLL ) && (defined( HL2MP ) || defined( PORTAL ))
	SetModel( GetWorldModel() );
#endif
	
	int sequence = SelectWeightedSequence( act ); 
	
	// FORCE IDLE on sequences we don't have (which should be many)
	if ( sequence == ACTIVITY_NOT_AVAILABLE )
		sequence = SelectWeightedSequence( ACT_VM_IDLE );

	//Adrian: Oh man again...
#if !defined( CLIENT_DLL ) && (defined( HL2MP ) || defined( PORTAL ))
	SetModel( GetViewModel() );
#endif

	if ( sequence != ACTIVITY_NOT_AVAILABLE )
	{
		SetSequence( sequence );
		SetActivity( act ); 
		SetCycle( 0 );
		ResetSequenceInfo( );

		if ( duration > 0 )
		{
			// FIXME: does this even make sense in non-shoot animations?
			m_flPlaybackRate = SequenceDuration( sequence ) / duration;
			m_flPlaybackRate = MIN( m_flPlaybackRate, 12.0);  // FIXME; magic number!, network encoding range
		}
		else
		{
			m_flPlaybackRate = 1.0;
		}
	}
}

//====================================================================================
// WEAPON CLIENT HANDLING
//====================================================================================
int CBaseCombatWeapon::UpdateClientData( CBasePlayer *pPlayer )
{
	int iNewState = WEAPON_IS_CARRIED_BY_PLAYER;

	if ( pPlayer->GetActiveWeapon() == this )
	{
		if ( pPlayer->m_fOnTarget ) 
		{
			iNewState = WEAPON_IS_ONTARGET;
		}
		else
		{
			iNewState = WEAPON_IS_ACTIVE;
		}
	}
	else
	{
		iNewState = WEAPON_IS_CARRIED_BY_PLAYER;
	}

	if ( m_iState != iNewState )
	{
		int iOldState = m_iState;
		m_iState = iNewState;
		OnActiveStateChanged( iOldState );
	}
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : index - 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SetViewModelIndex( int index )
{
	Assert( index >= 0 && index < MAX_VIEWMODELS );
	m_nViewModelIndex = index;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iActivity - 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SendViewModelAnim( int nSequence )
{
#if defined( CLIENT_DLL )
	if ( !IsPredicted() )
		return;
#endif
	
	if ( nSequence < 0 )
		return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	
	if ( pOwner == NULL )
		return;
	
	CBaseViewModel *vm = pOwner->GetViewModel( m_nViewModelIndex, false );
	
	if ( vm == NULL )
		return;

	SetViewModel();
	Assert( vm->ViewModelIndex() == m_nViewModelIndex );
	vm->SendViewModelMatchingSequence( nSequence );
}

float CBaseCombatWeapon::GetViewModelSequenceDuration()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
	{
		Assert( false );
		return 0;
	}
	
	CBaseViewModel *vm = pOwner->GetViewModel( m_nViewModelIndex );
	if ( vm == NULL )
	{
		Assert( false );
		return 0;
	}

	SetViewModel();
	Assert( vm->ViewModelIndex() == m_nViewModelIndex );
	return vm->SequenceDuration();
}

bool CBaseCombatWeapon::IsViewModelSequenceFinished( void )
{
	// These are not valid activities and always complete immediately
	if ( GetActivity() == ACT_RESET || GetActivity() == ACT_INVALID )
		return true;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
	{
		Assert( false );
		return false;
	}
	
	CBaseViewModel *vm = pOwner->GetViewModel( m_nViewModelIndex );
	if ( vm == NULL )
	{
		Assert( false );
		return false;
	}

	return vm->IsSequenceFinished();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SetViewModel()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;
	CBaseViewModel *vm = pOwner->GetViewModel( m_nViewModelIndex, false );
	if ( vm == NULL )
		return;
	Assert( vm->ViewModelIndex() == m_nViewModelIndex );
	vm->SetWeaponModel( GetViewModel( m_nViewModelIndex ), this );
}

//-----------------------------------------------------------------------------
// Purpose: Set the desired activity for the weapon and its viewmodel counterpart
// Input  : iActivity - activity to play
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::SendWeaponAnim( int iActivity )
{
#ifdef USES_ECON_ITEMS
	iActivity = TranslateViewmodelHandActivity( (Activity)iActivity );
#endif		
	// NVNT notify the haptics system of this weapons new activity
#ifdef WIN32
#ifdef CLIENT_DLL
	if ( prediction->InPrediction() && prediction->IsFirstTimePredicted() )
#endif
#ifndef _X360
		HapticSendWeaponAnim(this,iActivity);
#endif
#endif
	//For now, just set the ideal activity and be done with it
	return SetIdealActivity( (Activity) iActivity );
}

//====================================================================================
// WEAPON SELECTION
//====================================================================================

//-----------------------------------------------------------------------------
// Purpose: Returns true if the weapon currently has ammo or doesn't need ammo
// Output :
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::HasAnyAmmo( void )
{
	// If I don't use ammo of any kind, I can always fire
	if ( !UsesPrimaryAmmo() && !UsesSecondaryAmmo() )
		return true;

	// Otherwise, I need ammo of either type
	return ( HasPrimaryAmmo() || HasSecondaryAmmo() );
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if the weapon currently has ammo or doesn't need ammo
// Output :
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::HasPrimaryAmmo( void )
{
	// If I use a clip, and have some ammo in it, then I have ammo
	if ( UsesClipsForAmmo1() )
	{
		if ( m_iClip1 > 0 )
			return true;
	}

	// Otherwise, I have ammo if I have some in my ammo counts
	CBaseCombatCharacter		*pOwner = GetOwner();
	if ( pOwner )
	{
		if ( pOwner->GetAmmoCount( m_iPrimaryAmmoType ) > 0 ) 
			return true;
	}
	else
	{
		// No owner, so return how much primary ammo I have along with me.
		if( GetPrimaryAmmoCount() > 0 )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if the weapon currently has ammo or doesn't need ammo
// Output :
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::HasSecondaryAmmo( void )
{
	// If I use a clip, and have some ammo in it, then I have ammo
	if ( UsesClipsForAmmo2() )
	{
		if ( m_iClip2 > 0 )
			return true;
	}

	// Otherwise, I have ammo if I have some in my ammo counts
	CBaseCombatCharacter		*pOwner = GetOwner();
	if ( pOwner )
	{
		if ( pOwner->GetAmmoCount( m_iSecondaryAmmoType ) > 0 ) 
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: returns true if the weapon actually uses primary ammo
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::UsesPrimaryAmmo( void )
{
	if ( m_iPrimaryAmmoType < 0 )
		return false;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: returns true if the weapon actually uses secondary ammo
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::UsesSecondaryAmmo( void )
{
	if ( m_iSecondaryAmmoType < 0 )
		return false;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Show/hide weapon and corresponding view model if any
// Input  : visible - 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::SetWeaponVisible( bool visible )
{
	CBaseViewModel *vm = NULL;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner )
	{
		vm = pOwner->GetViewModel( m_nViewModelIndex );
	}

	if ( visible )
	{
		RemoveEffects( EF_NODRAW );
		if ( vm )
		{
			vm->RemoveEffects( EF_NODRAW );
		}
	}
	else
	{
		AddEffects( EF_NODRAW );
		if ( vm )
		{
			vm->AddEffects( EF_NODRAW );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::IsWeaponVisible( void )
{
	CBaseViewModel *vm = NULL;
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner )
	{
		vm = pOwner->GetViewModel( m_nViewModelIndex );
		if ( vm )
			return ( !vm->IsEffectActive(EF_NODRAW) );
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: If the current weapon has more ammo, reload it. Otherwise, switch 
//			to the next best weapon we've got. Returns true if it took any action.
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::ReloadOrSwitchWeapons( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	Assert( pOwner );
	
	m_bFireOnEmpty = false;

	ConVar* pAutoReload = cvar->FindVar("cl_autoreload");
	ConVar* pAutoSwitch = cvar->FindVar("cl_autoswitch");

	// If we don't have any ammo, switch to the next best weapon
	if (pAutoSwitch->GetBool() && !HasAnyAmmo() && m_flNextPrimaryAttack < gpGlobals->curtime && m_flNextSecondaryAttack < gpGlobals->curtime )
	{
		// weapon isn't useable, switch.
#ifdef MAPBASE
		// Ammo might be overridden to 0, in which case we shouldn't do this
		if ( ( (GetWeaponFlags() & ITEM_FLAG_NOAUTOSWITCHEMPTY) == false ) && !HasSpawnFlags(SF_WEAPON_NO_AUTO_SWITCH_WHEN_EMPTY) && ( g_pGameRules->SwitchToNextBestWeapon( pOwner, this ) ) )
#else
		if ( ( (GetWeaponFlags() & ITEM_FLAG_NOAUTOSWITCHEMPTY) == false ) && ( g_pGameRules->SwitchToNextBestWeapon( pOwner, this ) ) )
#endif
		{
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.3;
			return true;
		}
	}
	else if (pAutoReload->GetBool())
	{
		// Weapon is useable. Reload if empty and weapon has waited as long as it has to after firing
		if ( UsesClipsForAmmo1() && !AutoFiresFullClip() && 
			 (m_iClip1 == 0) && 
			 (GetWeaponFlags() & ITEM_FLAG_NOAUTORELOAD) == false && 
			 m_flNextPrimaryAttack < gpGlobals->curtime && 
			 m_flNextSecondaryAttack < gpGlobals->curtime )
		{
			// if we're successfully reloading, we're done
			if ( Reload() )
				return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *szViewModel - 
//			*szWeaponModel - 
//			iActivity - 
//			*szAnimExt - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::DefaultDeploy( char *szViewModel, char *szWeaponModel, int iActivity, char *szAnimExt )
{
	// Msg( "deploy %s at %f\n", GetClassname(), gpGlobals->curtime );

	// Weapons that don't autoswitch away when they run out of ammo 
	// can still be deployed when they have no ammo.
	if ( !HasAnyAmmo() && AllowsAutoSwitchFrom() )
		return false;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner )
	{
		// Dead men deploy no weapons
		if ( pOwner->IsAlive() == false )
			return false;

		pOwner->SetAnimationExtension( szAnimExt );

		SetViewModel();
		SendWeaponAnim( iActivity );
		
#ifdef MAPBASE
		pOwner->SetAnimation( PLAYER_UNHOLSTER );
#endif

		pOwner->SetNextAttack( gpGlobals->curtime + SequenceDuration() );
	}

	// Can't shoot again until we've finished deploying
	m_flNextPrimaryAttack	= gpGlobals->curtime + SequenceDuration();
	m_flNextSecondaryAttack	= gpGlobals->curtime + SequenceDuration();
	m_flHudHintMinDisplayTime = 0;

	m_bAltFireHudHintDisplayed = false;
	m_bReloadHudHintDisplayed = false;
	m_flHudHintPollTime = gpGlobals->curtime + 5.0f;
	
	WeaponSound( DEPLOY );

	SetWeaponVisible( true );

/*

This code is disabled for now, because moving through the weapons in the carousel 
selects and deploys each weapon as you pass it. (sjb)

*/

	SetContextThink( NULL, 0, HIDEWEAPON_THINK_CONTEXT );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::Deploy( )
{
	MDLCACHE_CRITICAL_SECTION();
	return DefaultDeploy( (char*)GetViewModel(), (char*)GetWorldModel(), GetDrawActivity(), (char*)GetAnimPrefix() );
}

Activity CBaseCombatWeapon::GetDrawActivity( void )
{
	return ACT_VM_DRAW;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::Holster( CBaseCombatWeapon *pSwitchingTo )
{ 
	MDLCACHE_CRITICAL_SECTION();

	// cancel any reload in progress.
	m_bInReload = false; 
	m_bFiringWholeClip = false;

	// kill any think functions
	SetThink(NULL);

	// Send holster animation
	SendWeaponAnim( ACT_VM_HOLSTER );

	// Some weapon's don't have holster anims yet, so detect that
	float flSequenceDuration = 0;
	if ( GetActivity() == ACT_VM_HOLSTER )
	{
		flSequenceDuration = SequenceDuration();
	}

	CBaseCombatCharacter *pOwner = GetOwner();
	if (pOwner)
	{
		pOwner->SetNextAttack( gpGlobals->curtime + flSequenceDuration );

#ifdef MAPBASE
		if (IsWeaponVisible() && pOwner->IsPlayer())
			static_cast<CBasePlayer*>(pOwner)->SetAnimation( PLAYER_HOLSTER );
#endif
	}

	// If we don't have a holster anim, hide immediately to avoid timing issues
	if ( !flSequenceDuration )
	{
		SetWeaponVisible( false );
	}
	else
	{
		// Hide the weapon when the holster animation's finished
		SetContextThink( &CBaseCombatWeapon::HideThink, gpGlobals->curtime + flSequenceDuration, HIDEWEAPON_THINK_CONTEXT );
	}

	// if we were displaying a hud hint, squelch it.
	if (m_flHudHintMinDisplayTime && gpGlobals->curtime < m_flHudHintMinDisplayTime)
	{
		if( m_bAltFireHudHintDisplayed )
			RescindAltFireHudHint();

		if( m_bReloadHudHintDisplayed )
			RescindReloadHudHint();
	}

#ifdef MAPBASE
	if (HasSpawnFlags(SF_WEAPON_NO_AUTO_SWITCH_WHEN_EMPTY))
		RemoveSpawnFlags(SF_WEAPON_NO_AUTO_SWITCH_WHEN_EMPTY);
#endif

	return true;
}

#ifdef CLIENT_DLL

	void CBaseCombatWeapon::BoneMergeFastCullBloat( Vector &localMins, Vector &localMaxs, const Vector &thisEntityMins, const Vector &thisEntityMaxs ) const
	{
		// The default behavior pushes it out by BONEMERGE_FASTCULL_BBOX_EXPAND in all directions, but we can do better
		// since we know the weapon will never point behind him.

		localMaxs.x += 20;	// Leaves some space in front for long weapons.
		
		localMins.y -= 20;	// Fatten it to his left and right since he can rotate that way.
		localMaxs.y += 20;	

		localMaxs.z += 15;	// Leave some space at the top.
	}

#else

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputSetAmmo1( inputdata_t &inputdata )
{
	SetAmmoFromMapper(inputdata.value.Float());
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputSetAmmo2( inputdata_t &inputdata )
{
	SetAmmoFromMapper(inputdata.value.Float(), true);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputGiveDefaultAmmo( inputdata_t &inputdata )
{
	GiveDefaultAmmo();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputEnablePlayerPickup( inputdata_t &inputdata )
{
	RemoveSpawnFlags(SF_WEAPON_NO_PLAYER_PICKUP);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputDisablePlayerPickup( inputdata_t &inputdata )
{
	AddSpawnFlags(SF_WEAPON_NO_PLAYER_PICKUP);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputEnableNPCPickup( inputdata_t &inputdata )
{
	RemoveSpawnFlags(SF_WEAPON_NO_NPC_PICKUP);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputDisableNPCPickup( inputdata_t &inputdata )
{
	AddSpawnFlags(SF_WEAPON_NO_NPC_PICKUP);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputBreakConstraint( inputdata_t &inputdata )
{
	if ( m_pConstraint != NULL )
	{
		physenv->DestroyConstraint( m_pConstraint );
		m_pConstraint = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputForceFire( inputdata_t &inputdata, bool bSecondary )
{
	CBaseCombatCharacter *pOperator = GetOwner();

	if (!pOperator)
	{
		// No owner. This means they want us to fire while possibly on the floor independent of any NPC...the madmapper!
		pOperator = ToBaseCombatCharacter(inputdata.pActivator);
		if (pOperator && pOperator->IsNPC())
		{
			// Use this guy, I guess
			Operator_ForceNPCFire(pOperator, bSecondary);
		}
		else
		{
			// Well...I learned this trick from ent_info. If you have any better ideas, be my guest.
			pOperator = CreateEntityByName("generic_actor")->MyCombatCharacterPointer();
			pOperator->SetAbsOrigin(GetAbsOrigin());
			pOperator->SetAbsAngles(GetAbsAngles());
			SetOwnerEntity(pOperator);

			Operator_ForceNPCFire(pOperator, bSecondary);

			UTIL_RemoveImmediate(pOperator);
		}
	}
	else if (pOperator->IsPlayer())
	{
		// Owner exists and is a player.
		bSecondary ? SecondaryAttack() : PrimaryAttack();
	}
	else
	{
		// Owner exists and is a NPC.
		Operator_ForceNPCFire(pOperator, bSecondary);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputForcePrimaryFire( inputdata_t &inputdata )
{
	InputForceFire(inputdata, false);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputForceSecondaryFire( inputdata_t &inputdata )
{
	InputForceFire(inputdata, true);
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::InputHideWeapon( inputdata_t &inputdata )
{
	// Only hide if we're still the active weapon. If we're not the active weapon
	if ( GetOwner() && GetOwner()->GetActiveWeapon() == this )
	{
		SetWeaponVisible( false );
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::HideThink( void )
{
	// Only hide if we're still the active weapon. If we're not the active weapon
	if ( GetOwner() && GetOwner()->GetActiveWeapon() == this )
	{
		SetWeaponVisible( false );
	}
}

bool CBaseCombatWeapon::CanReload( void )
{
	if ( AutoFiresFullClip() && m_bFiringWholeClip )
	{
		return false;
	}

	return true;
}

#if defined ( TF_CLIENT_DLL ) || defined ( TF_DLL )
//-----------------------------------------------------------------------------
// Purpose: Anti-hack
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::AddToCritBucket( float flAmount )
{
	float flCap = tf_weapon_criticals_bucket_cap.GetFloat();

	// Regulate crit frequency to reduce client-side seed hacking
	if ( m_flCritTokenBucket < flCap )
	{
		// Treat raw damage as the resource by which we add or subtract from the bucket
		m_flCritTokenBucket += flAmount;
		m_flCritTokenBucket = Min( m_flCritTokenBucket, flCap );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Anti-hack
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::IsAllowedToWithdrawFromCritBucket( float flDamage )
{
	// Note: If we're in this block of code, the assumption is that the
	// seed said we should grant a random crit.  If allowed, the cost
	// will be deducted here.

	// Track each seed request - in cases where a player is hacking, we'll 
	// see a silly ratio.
	m_nCritSeedRequests++;

	// Adjust token cost based on the ratio of requests vs granted, except
	// melee, which crits much more than ranged (as high as 60% chance)
	float flMult = ( IsMeleeWeapon() ) ? 0.5f : RemapValClamped( ( (float)m_nCritSeedRequests / (float)m_nCritChecks ), 0.1f, 1.f, 1.f, 3.f );

	// Would this take us below our limit?
	float flCost = ( flDamage * TF_DAMAGE_CRIT_MULTIPLIER ) * flMult;
	if ( flCost > m_flCritTokenBucket )
		return false;

	// Withdraw
	RemoveFromCritBucket( flCost );

	float flBottom = tf_weapon_criticals_bucket_bottom.GetFloat();
	if ( m_flCritTokenBucket < flBottom )
		m_flCritTokenBucket = flBottom;

	return true;
}
#endif // TF_DLL

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::ItemPreFrame( void )
{
	MaintainIdealActivity();

#ifndef CLIENT_DLL
#ifndef HL2_EPISODIC
	if ( IsX360() )
#endif
	{
		// If we haven't displayed the hint enough times yet, it's time to try to 
		// display the hint, and the player is not standing still, try to show a hud hint.
		// If the player IS standing still, assume they could change away from this weapon at
		// any second.
		if( (!m_bAltFireHudHintDisplayed || !m_bReloadHudHintDisplayed) && gpGlobals->curtime > m_flHudHintMinDisplayTime && gpGlobals->curtime > m_flHudHintPollTime && GetOwner() && GetOwner()->IsPlayer() )
		{
			CBasePlayer *pPlayer = (CBasePlayer*)(GetOwner());

			if( pPlayer && pPlayer->GetStickDist() > 0.0f )
			{
				// If the player is moving, they're unlikely to switch away from the current weapon
				// the moment this weapon displays its HUD hint.
				if( ShouldDisplayReloadHUDHint() )
				{
					DisplayReloadHudHint();
				}
				else if( ShouldDisplayAltFireHUDHint() )
				{
					DisplayAltFireHudHint();
				}
			}
			else
			{
				m_flHudHintPollTime = gpGlobals->curtime + 2.0f;
			}
		}
	}
#endif
}

//====================================================================================
// WEAPON BEHAVIOUR
//====================================================================================
void CBaseCombatWeapon::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if (!pOwner)
		return;

	UpdateAutoFire();

	//Track the duration of the fire
	//FIXME: Check for IN_ATTACK2 as well?
	//FIXME: What if we're calling ItemBusyFrame?
	m_fFireDuration = ( pOwner->m_nButtons & IN_ATTACK ) ? ( m_fFireDuration + gpGlobals->frametime ) : 0.0f;

	if ( UsesClipsForAmmo1() )
	{
		CheckReload();
	}

	bool bFired = false;

	CalculateADS();

	if (pOwner->m_nButtons & IN_ADS)
	{
		m_bIsADS = true;
	}
	else
	{
		m_bIsADS = false;
	}

	// Secondary attack has priority
	if ((pOwner->m_nButtons & IN_ATTACK2) && (m_flNextSecondaryAttack <= gpGlobals->curtime))
	{
#ifdef MAPBASE
		if (pOwner->HasSpawnFlags(SF_PLAYER_SUPPRESS_FIRING))
		{
			// Don't do anything, just cancel the whole function
			return;
		}
		else
#endif
		if (UsesSecondaryAmmo() && pOwner->GetAmmoCount(m_iSecondaryAmmoType)<=0 )
		{
			if (m_flNextEmptySoundTime < gpGlobals->curtime)
			{
				WeaponSound(EMPTY);
				m_flNextSecondaryAttack = m_flNextEmptySoundTime = gpGlobals->curtime + 0.5;
			}
		}
		else if (pOwner->GetWaterLevel() == 3 && m_bAltFiresUnderwater == false)
		{
			// This weapon doesn't fire underwater
			WeaponSound(EMPTY);
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.2;
			return;
		}
		else
		{
			// FIXME: This isn't necessarily true if the weapon doesn't have a secondary fire!
			// For instance, the crossbow doesn't have a 'real' secondary fire, but it still 
			// stops the crossbow from firing on the 360 if the player chooses to hold down their
			// zoom button. (sjb) Orange Box 7/25/2007
#if !defined(CLIENT_DLL)
			if( !IsX360() || !ClassMatches("weapon_crossbow") )
#endif
			{
				bFired = ShouldBlockPrimaryFire();
			}

			SecondaryAttack();

			// Secondary ammo doesn't have a reload animation
			if ( UsesClipsForAmmo2() )
			{
				// reload clip2 if empty
				if (m_iClip2 < 1)
				{
					pOwner->RemoveAmmo( 1, m_iSecondaryAmmoType );
					m_iClip2 = m_iClip2 + 1;
				}
			}
		}
	}
	
	if ( !bFired && (pOwner->m_nButtons & IN_ATTACK) && (m_flNextPrimaryAttack <= gpGlobals->curtime))
	{
#ifdef MAPBASE
		if (pOwner->HasSpawnFlags( SF_PLAYER_SUPPRESS_FIRING ))
		{
			// Don't do anything, just cancel the whole function
			return;
		}
		else
#endif
		// Clip empty? Or out of ammo on a no-clip weapon?
		if ( !IsMeleeWeapon() &&  
			(( UsesClipsForAmmo1() && m_iClip1 <= 0) || ( !UsesClipsForAmmo1() && pOwner->GetAmmoCount(m_iPrimaryAmmoType)<=0 )) )
		{
			HandleFireOnEmpty();
		}
		else if (pOwner->GetWaterLevel() == 3 && m_bFiresUnderwater == false)
		{
			// This weapon doesn't fire underwater
			WeaponSound(EMPTY);
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.2;
			return;
		}
		else
		{
			//NOTENOTE: There is a bug with this code with regards to the way machine guns catch the leading edge trigger
			//			on the player hitting the attack key.  It relies on the gun catching that case in the same frame.
			//			However, because the player can also be doing a secondary attack, the edge trigger may be missed.
			//			We really need to hold onto the edge trigger and only clear the condition when the gun has fired its
			//			first shot.  Right now that's too much of an architecture change -- jdw
			
			// If the firing button was just pressed, or the alt-fire just released, reset the firing time
			if ( ( pOwner->m_afButtonPressed & IN_ATTACK ) || ( pOwner->m_afButtonReleased & IN_ATTACK2 ) )
			{
				 m_flNextPrimaryAttack = gpGlobals->curtime;
			}

			PrimaryAttack();

			if ( AutoFiresFullClip() )
			{
				m_bFiringWholeClip = true;
			}

#ifdef CLIENT_DLL
			pOwner->SetFiredWeapon( true );
#endif
		}
	}

	// -----------------------
	//  Reload pressed / Clip Empty
	// -----------------------
	if ( ( pOwner->m_nButtons & IN_RELOAD ) && UsesClipsForAmmo1() && !m_bInReload ) 
	{
		// reload when reload is pressed, or if no buttons are down and weapon is empty.
		Reload();
		m_fFireDuration = 0.0f;
	}

	// -----------------------
	//  No buttons down
	// -----------------------
	if (!((pOwner->m_nButtons & IN_ATTACK) || (pOwner->m_nButtons & IN_ATTACK2) || (CanReload() && pOwner->m_nButtons & IN_RELOAD)))
	{
		// no fire buttons down or reloading
		if ( !ReloadOrSwitchWeapons() && ( m_bInReload == false ) )
		{
			WeaponIdle();
		}
	}
}

void CBaseCombatWeapon::HandleFireOnEmpty()
{
	// If we're already firing on empty, reload if we can
	if ( m_bFireOnEmpty )
	{
		ReloadOrSwitchWeapons();
		m_fFireDuration = 0.0f;
	}
	else
	{
		if (m_flNextEmptySoundTime < gpGlobals->curtime)
		{
			WeaponSound(EMPTY);
			m_flNextEmptySoundTime = gpGlobals->curtime + 0.5;
		}
		m_bFireOnEmpty = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called each frame by the player PostThink, if the player's not ready to attack yet
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::ItemBusyFrame( void )
{
	UpdateAutoFire();
}

//-----------------------------------------------------------------------------
// Purpose: Base class default for getting bullet type
// Input  :
// Output :
//-----------------------------------------------------------------------------
int CBaseCombatWeapon::GetBulletType( void )
{
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Base class default for getting spread
// Input  :
// Output :
//-----------------------------------------------------------------------------
const Vector& CBaseCombatWeapon::GetBulletSpread( void )
{
	static Vector cone = VECTOR_CONE_15DEGREES;
	return cone;
}

//-----------------------------------------------------------------------------
const WeaponProficiencyInfo_t *CBaseCombatWeapon::GetProficiencyValues()
{
	static WeaponProficiencyInfo_t defaultWeaponProficiencyTable[] =
	{
		{ 1.0, 1.0	},
		{ 1.0, 1.0	},
		{ 1.0, 1.0	},
		{ 1.0, 1.0	},
		{ 1.0, 1.0	},
	};

	COMPILE_TIME_ASSERT( ARRAYSIZE(defaultWeaponProficiencyTable) == WEAPON_PROFICIENCY_PERFECT + 1);
	return defaultWeaponProficiencyTable;
}

//-----------------------------------------------------------------------------
// Purpose: Base class default for getting firerate
// Input  :
// Output :
//-----------------------------------------------------------------------------
float CBaseCombatWeapon::GetFireRate( void )
{
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Base class default for playing shoot sound
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::WeaponSound( WeaponSound_t sound_type, float soundtime /* = 0.0f */ )
{
	// If we have some sounds from the weapon classname.txt file, play a random one of them
	const char *shootsound = GetShootSound( sound_type );
	if ( !shootsound || !shootsound[0] )
		return;

	CSoundParameters params;
	
	if ( !GetParametersForSound( shootsound, params, NULL ) )
		return;

	if ( params.play_to_owner_only )
	{
		// Am I only to play to my owner?
		if ( GetOwner() && GetOwner()->IsPlayer() )
		{
			CSingleUserRecipientFilter filter( ToBasePlayer( GetOwner() ) );
			if ( IsPredicted() && CBaseEntity::GetPredictionPlayer() )
			{
				filter.UsePredictionRules();
			}
			EmitSound( filter, GetOwner()->entindex(), shootsound, NULL, soundtime );
		}
	}
	else
	{
		// Play weapon sound from the owner
		if ( GetOwner() )
		{
			CPASAttenuationFilter filter( GetOwner(), params.soundlevel );
			if ( IsPredicted() && CBaseEntity::GetPredictionPlayer() )
			{
				filter.UsePredictionRules();
			}
			EmitSound( filter, GetOwner()->entindex(), shootsound, NULL, soundtime ); 

#if !defined( CLIENT_DLL )
			if( sound_type == EMPTY )
			{
				CSoundEnt::InsertSound( SOUND_COMBAT, GetOwner()->GetAbsOrigin(), SOUNDENT_VOLUME_EMPTY, 0.2, GetOwner() );
			}
#endif
		}
		// If no owner play from the weapon (this is used for thrown items)
		else
		{
			CPASAttenuationFilter filter( this, params.soundlevel );
			if ( IsPredicted() && CBaseEntity::GetPredictionPlayer() )
			{
				filter.UsePredictionRules();
			}
			EmitSound( filter, entindex(), shootsound, NULL, soundtime ); 
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Stop a sound played by this weapon.
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::StopWeaponSound( WeaponSound_t sound_type )
{
	//if ( IsPredicted() )
	//	return;

	// If we have some sounds from the weapon classname.txt file, play a random one of them
	const char *shootsound = GetShootSound( sound_type );
	if ( !shootsound || !shootsound[0] )
		return;
	
	CSoundParameters params;
	if ( !GetParametersForSound( shootsound, params, NULL ) )
		return;

	// Am I only to play to my owner?
	if ( params.play_to_owner_only )
	{
		if ( GetOwner() )
		{
			StopSound( GetOwner()->entindex(), shootsound );
		}
	}
	else
	{
		// Play weapon sound from the owner
		if ( GetOwner() )
		{
			StopSound( GetOwner()->entindex(), shootsound );
		}
		// If no owner play from the weapon (this is used for thrown items)
		else
		{
			StopSound( entindex(), shootsound );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::DefaultReload( int iClipSize1, int iClipSize2, int iActivity )
{
	CBaseCombatCharacter *pOwner = GetOwner();
	if (!pOwner)
		return false;

	// If I don't have any spare ammo, I can't reload
	if ( pOwner->GetAmmoCount(m_iPrimaryAmmoType) <= 0 )
		return false;

	bool bReload = false;

	// If you don't have clips, then don't try to reload them.
	if ( UsesClipsForAmmo1() )
	{
		// need to reload primary clip?
		int primary	= MIN(iClipSize1 - m_iClip1, pOwner->GetAmmoCount(m_iPrimaryAmmoType));
		if ( primary != 0 )
		{
			bReload = true;
		}
	}

	if ( UsesClipsForAmmo2() )
	{
		// need to reload secondary clip?
		int secondary = MIN(iClipSize2 - m_iClip2, pOwner->GetAmmoCount(m_iSecondaryAmmoType));
		if ( secondary != 0 )
		{
			bReload = true;
		}
	}

	if ( !bReload )
		return false;

#ifdef CLIENT_DLL
	// Play reload
	WeaponSound( RELOAD );
#endif
	SendWeaponAnim( iActivity );

	// Play the player's reload animation
	if ( pOwner->IsPlayer() )
	{
		( ( CBasePlayer * )pOwner)->SetAnimation( PLAYER_RELOAD );
	}

	MDLCACHE_CRITICAL_SECTION();
	float flSequenceEndTime = gpGlobals->curtime + SequenceDuration();
	pOwner->SetNextAttack( flSequenceEndTime );
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = flSequenceEndTime;

	m_bInReload = true;

	return true;
}

bool CBaseCombatWeapon::ReloadsSingly( void ) const
{
#if defined ( TF_DLL ) || defined ( TF_CLIENT_DLL )
	float fHasReload = 1.0f;
	CALL_ATTRIB_HOOK_FLOAT( fHasReload, mod_no_reload_display_only );
	if ( fHasReload != 1.0f )
	{
		return false;
	}

	int iWeaponMod = 0;
	CALL_ATTRIB_HOOK_INT( iWeaponMod, set_scattergun_no_reload_single );
	if ( iWeaponMod == 1 )
	{
		return false;
	}
#endif // TF_DLL || TF_CLIENT_DLL

	return m_bReloadsSingly;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::Reload( void )
{
	return DefaultReload( GetMaxClip1(), GetMaxClip2(), (m_iClip1 < 1) ? ACT_VM_RELOAD_EMPTY : ACT_VM_RELOAD );
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::Reload_NPC( bool bPlaySound )
{
	if (bPlaySound)
		WeaponSound( RELOAD_NPC );

	if (UsesClipsForAmmo1())
	{
		m_iClip1 = GetMaxClip1();
	}
	else
	{
		// For weapons which don't use clips, give the owner ammo.
		if (GetOwner())
			GetOwner()->SetAmmoCount( GetDefaultClip1(), m_iPrimaryAmmoType );
	}
}
#endif

//=========================================================
void CBaseCombatWeapon::WeaponIdle( void )
{
	//Idle again if we've finished
	if ( HasWeaponIdleTimeElapsed() )
	{
		SendWeaponAnim((m_iClip1 < 1) ? ACT_VM_IDLE_EMPTY : ACT_VM_IDLE);
	}
}


//=========================================================
Activity CBaseCombatWeapon::GetPrimaryAttackActivity( void )
{
	return ACT_VM_PRIMARYATTACK;
}

//=========================================================
Activity CBaseCombatWeapon::GetSecondaryAttackActivity( void )
{
	return ACT_VM_SECONDARYATTACK;
}

//-----------------------------------------------------------------------------
// Purpose: Adds in view kick and weapon accuracy degradation effect
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::AddViewKick( void )
{
	//NOTENOTE: By default, weapon will not kick up (defined per weapon)
}

//-----------------------------------------------------------------------------
// Purpose: Get the string to print death notices with
//-----------------------------------------------------------------------------
char *CBaseCombatWeapon::GetDeathNoticeName( void )
{
#if !defined( CLIENT_DLL )
	return (char*)STRING( m_iszName );
#else
	return "GetDeathNoticeName not implemented on client yet";
#endif
}

//====================================================================================
// WEAPON RELOAD TYPES
//====================================================================================
void CBaseCombatWeapon::CheckReload( void )
{
	if ( m_bReloadsSingly )
	{
		CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
		if ( !pOwner )
			return;

		if ((m_bInReload) && (m_flNextPrimaryAttack <= gpGlobals->curtime))
		{
			if ( pOwner->m_nButtons & (IN_ATTACK | IN_ATTACK2) && m_iClip1 > 0 )
			{
				m_bInReload = false;
				return;
			}

			// If out of ammo end reload
			if (pOwner->GetAmmoCount(m_iPrimaryAmmoType) <=0)
			{
				FinishReload();
				return;
			}
			// If clip not full reload again
			else if (m_iClip1 < GetMaxClip1())
			{
				// Add them to the clip
				m_iClip1 += 1;
				pOwner->RemoveAmmo( 1, m_iPrimaryAmmoType );

				Reload();
				return;
			}
			// Clip full, stop reloading
			else
			{
				FinishReload();
				m_flNextPrimaryAttack	= gpGlobals->curtime;
				m_flNextSecondaryAttack = gpGlobals->curtime;
				return;
			}
		}
	}
	else
	{
		if ( (m_bInReload) && (m_flNextPrimaryAttack <= gpGlobals->curtime))
		{
			FinishReload();
			m_flNextPrimaryAttack	= gpGlobals->curtime;
			m_flNextSecondaryAttack = gpGlobals->curtime;
			m_bInReload = false;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Reload has finished.
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::FinishReload( void )
{
	CBaseCombatCharacter *pOwner = GetOwner();

	if (pOwner)
	{
		// If I use primary clips, reload primary
		if ( UsesClipsForAmmo1() )
		{
			int primary	= MIN( GetMaxClip1() - m_iClip1, pOwner->GetAmmoCount(m_iPrimaryAmmoType));	
			m_iClip1 += primary;
			pOwner->RemoveAmmo( primary, m_iPrimaryAmmoType);
		}

		// If I use secondary clips, reload secondary
		if ( UsesClipsForAmmo2() )
		{
			int secondary = MIN( GetMaxClip2() - m_iClip2, pOwner->GetAmmoCount(m_iSecondaryAmmoType));
			m_iClip2 += secondary;
			pOwner->RemoveAmmo( secondary, m_iSecondaryAmmoType );
		}

		if ( m_bReloadsSingly )
		{
			m_bInReload = false;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Abort any reload we have in progress
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::AbortReload( void )
{
#ifdef CLIENT_DLL
	StopWeaponSound( RELOAD ); 
#endif
	m_bInReload = false;
}

void CBaseCombatWeapon::UpdateAutoFire( void )
{
	if ( !AutoFiresFullClip() )
		return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	if ( m_iClip1 == 0 )
	{
		// Ready to reload again
		m_bFiringWholeClip = false;
	}

	if ( m_bFiringWholeClip )
	{
		// If it's firing the clip don't let them repress attack to reload
		pOwner->m_nButtons &= ~IN_ATTACK;
	}

	// Don't use the regular reload key
	if ( pOwner->m_nButtons & IN_RELOAD )
	{
		pOwner->m_nButtons &= ~IN_RELOAD;
	}

	// Try to fire if there's ammo in the clip and we're not holding the button
	bool bReleaseClip = m_iClip1 > 0 && !( pOwner->m_nButtons & IN_ATTACK );

	if ( !bReleaseClip )
	{
		if ( CanReload() && ( pOwner->m_nButtons & IN_ATTACK ) )
		{
			// Convert the attack key into the reload key
			pOwner->m_nButtons |= IN_RELOAD;
		}

		// Don't allow attack button if we're not attacking
		pOwner->m_nButtons &= ~IN_ATTACK;
	}
	else
	{
		// Fake the attack key
		pOwner->m_nButtons |= IN_ATTACK;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Primary fire button attack
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::PrimaryAttack( void )
{
	// If my clip is empty (and I use clips) start reload
	if ( UsesClipsForAmmo1() && !m_iClip1 ) 
	{
		Reload();
		return;
	}

	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if (!pPlayer)
	{
		return;
	}

	pPlayer->DoMuzzleFlash();

	SendWeaponAnim( GetPrimaryAttackActivity() );

	// player "shoot" animation
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	FireBulletsInfo_t info;
	info.m_vecSrc	 = pPlayer->Weapon_ShootPosition( );
	
	info.m_vecDirShooting = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );

	// To make the firing framerate independent, we may have to fire more than one bullet here on low-framerate systems, 
	// especially if the weapon we're firing has a really fast rate of fire.
	info.m_iShots = 0;
	float fireRate = GetFireRate();

	while ( m_flNextPrimaryAttack <= gpGlobals->curtime )
	{
		// MUST call sound before removing a round from the clip of a CMachineGun
		WeaponSound(SINGLE, m_flNextPrimaryAttack);
		m_flNextPrimaryAttack = m_flNextPrimaryAttack + fireRate;
		info.m_iShots++;
		if ( !fireRate )
			break;
	}

	// Make sure we don't fire more than the amount in the clip
	if ( UsesClipsForAmmo1() )
	{
		info.m_iShots = MIN( info.m_iShots, m_iClip1 );
		m_iClip1 -= info.m_iShots;
	}
	else
	{
		info.m_iShots = MIN( info.m_iShots, pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) );
		pPlayer->RemoveAmmo( info.m_iShots, m_iPrimaryAmmoType );
	}

	info.m_flDistance = MAX_TRACE_LENGTH;
	info.m_iAmmoType = m_iPrimaryAmmoType;
	info.m_iTracerFreq = 2;

#if !defined( CLIENT_DLL )
	// Fire the bullets
	info.m_vecSpread = pPlayer->GetAttackSpread( this );
#else
	//!!!HACKHACK - what does the client want this function for? 
	info.m_vecSpread = GetActiveWeapon()->GetBulletSpread();
#endif // CLIENT_DLL

	pPlayer->FireBullets( info );

	if (!m_iClip1 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
	{
		// HEV suit - indicate out of ammo condition
		pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0); 
	}

	//Add our view kick in
	AddViewKick();
}

//-----------------------------------------------------------------------------
// Purpose: Called every frame to check if the weapon is going through transition animations
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::MaintainIdealActivity( void )
{
	// Must be transitioning
	if ( GetActivity() != ACT_TRANSITION )
		return;

	// Must not be at our ideal already 
	if ( ( GetActivity() == m_IdealActivity ) && ( GetSequence() == m_nIdealSequence ) )
		return;
	
	// Must be finished with the current animation
	if ( IsViewModelSequenceFinished() == false )
		return;

	// Move to the next animation towards our ideal
	SendWeaponAnim( m_IdealActivity );
}

//-----------------------------------------------------------------------------
// Purpose: Sets the ideal activity for the weapon to be in, allowing for transitional animations inbetween
// Input  : ideal - activity to end up at, ideally
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::SetIdealActivity( Activity ideal )
{
	MDLCACHE_CRITICAL_SECTION();
	int	idealSequence = SelectWeightedSequence( ideal );

	if ( idealSequence == -1 )
		return false;

	//Take the new activity
	m_IdealActivity	 = ideal;
	m_nIdealSequence = idealSequence;

	//Find the next sequence in the potential chain of sequences leading to our ideal one
	int nextSequence = FindTransitionSequence( GetSequence(), m_nIdealSequence, NULL );

	// Don't use transitions when we're deploying
	if ( ideal != ACT_VM_DRAW && IsWeaponVisible() && nextSequence != m_nIdealSequence )
	{
		//Set our activity to the next transitional animation
		SetActivity( ACT_TRANSITION );
		SetSequence( nextSequence );	
		SendViewModelAnim( nextSequence );
	}
	else
	{
		//Set our activity to the ideal
		SetActivity( m_IdealActivity );
		SetSequence( m_nIdealSequence );	
		SendViewModelAnim( m_nIdealSequence );
	}

	//Set the next time the weapon will idle
	SetWeaponIdleTime( gpGlobals->curtime + SequenceDuration() );
	return true;
}

//-----------------------------------------------------------------------------
// Returns information about the various control panels
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::GetControlPanelInfo( int nPanelIndex, const char *&pPanelName )
{
	pPanelName = NULL;
}

//-----------------------------------------------------------------------------
// Returns information about the various control panels
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::GetControlPanelClassName( int nPanelIndex, const char *&pPanelName )
{
	pPanelName = "vgui_screen";
}


//-----------------------------------------------------------------------------
// Locking a weapon is an exclusive action. If you lock a weapon, that means 
// you are preventing others from doing so for themselves.
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::Lock( float lockTime, CBaseEntity *pLocker )
{
	m_flUnlockTime = gpGlobals->curtime + lockTime;
	m_hLocker.Set( pLocker );
}

//-----------------------------------------------------------------------------
// If I'm still locked for a period of time, tell everyone except the person
// that locked me that I'm not available. 
//-----------------------------------------------------------------------------
bool CBaseCombatWeapon::IsLocked( CBaseEntity *pAsker )
{
	return ( m_flUnlockTime > gpGlobals->curtime && m_hLocker != pAsker );
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
Activity CBaseCombatWeapon::ActivityOverride( Activity baseAct, bool *pRequired )
{
	acttable_t *pTable = ActivityList();
	int actCount = ActivityListCount();

	for ( int i = 0; i < actCount; i++, pTable++ )
	{
		if ( baseAct == pTable->baseAct )
		{
			if (pRequired)
			{
				*pRequired = pTable->required;
			}
			return (Activity)pTable->weaponAct;
		}
	}
	return baseAct;
}

#ifdef MAPBASE_VSCRIPT
//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
HSCRIPT CBaseCombatWeapon::ScriptGetOwner()
{
	return ToHScript( GetOwner() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseCombatWeapon::ScriptSetOwner( HSCRIPT owner )
{
	return SetOwner( ToEnt( owner ) ? ToEnt( owner )->MyCombatCharacterPointer() : NULL );
}
#endif

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDmgAccumulator::CDmgAccumulator( void )
{
#ifdef GAME_DLL
	SetDefLessFunc( m_TargetsDmgInfo );
#endif // GAME_DLL

	m_bActive = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDmgAccumulator::~CDmgAccumulator()
{
	// Did a weapon get deleted while aggregating CTakeDamageInfo events?
	Assert( !m_bActive );
}

#ifdef GAME_DLL
//-----------------------------------------------------------------------------
// Collect trace attacks for weapons that fire multiple bullets per attack that also penetrate
//-----------------------------------------------------------------------------
void CDmgAccumulator::AccumulateMultiDamage( const CTakeDamageInfo &info, CBaseEntity *pEntity )
{
	if ( !pEntity )
		return;

	Assert( m_bActive );

#if defined( GAME_DLL )
	int iIndex = m_TargetsDmgInfo.Find( pEntity->entindex() );
	if ( iIndex == m_TargetsDmgInfo.InvalidIndex() )
	{
		m_TargetsDmgInfo.Insert( pEntity->entindex(), info );
	}
	else
	{
		CTakeDamageInfo *pInfo = &m_TargetsDmgInfo[iIndex];
		if ( pInfo )
		{
			// Update
			m_TargetsDmgInfo[iIndex].AddDamageType( info.GetDamageType() );
			m_TargetsDmgInfo[iIndex].SetDamage( pInfo->GetDamage() + info.GetDamage() );
			m_TargetsDmgInfo[iIndex].SetDamageForce( pInfo->GetDamageForce() + info.GetDamageForce() );
			m_TargetsDmgInfo[iIndex].SetDamagePosition( info.GetDamagePosition() );
			m_TargetsDmgInfo[iIndex].SetReportedPosition( info.GetReportedPosition() );
			m_TargetsDmgInfo[iIndex].SetMaxDamage( MAX( pInfo->GetMaxDamage(), info.GetDamage() ) );
			m_TargetsDmgInfo[iIndex].SetAmmoType( info.GetAmmoType() );
		}

	}
#endif	// GAME_DLL
}

//-----------------------------------------------------------------------------
// Purpose: Send aggregate info
//-----------------------------------------------------------------------------
void CDmgAccumulator::Process( void )
{
	FOR_EACH_MAP( m_TargetsDmgInfo, i )
	{
		CBaseEntity *pEntity = UTIL_EntityByIndex( m_TargetsDmgInfo.Key( i ) );
		if ( pEntity )
		{
			AddMultiDamage( m_TargetsDmgInfo[i], pEntity );
		}
	}

	m_bActive = false;
	m_TargetsDmgInfo.Purge();
}
#endif // GAME_DLL

#if defined( CLIENT_DLL )

BEGIN_PREDICTION_DATA( CBaseCombatWeapon )

	DEFINE_PRED_FIELD( m_nNextThinkTick, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	// Networked
	DEFINE_PRED_FIELD( m_hOwner, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
	// DEFINE_FIELD( m_hWeaponFileInfo, FIELD_SHORT ),
	DEFINE_PRED_FIELD( m_iState, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),			 
	DEFINE_PRED_FIELD( m_iViewModelIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD( m_iWorldModelIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD_TOL( m_flNextPrimaryAttack, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, TD_MSECTOLERANCE ),	
	DEFINE_PRED_FIELD_TOL( m_flNextSecondaryAttack, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, TD_MSECTOLERANCE ),
	DEFINE_PRED_FIELD_TOL( m_flTimeWeaponIdle, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, TD_MSECTOLERANCE ),

	DEFINE_PRED_FIELD( m_iPrimaryAmmoType, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_iSecondaryAmmoType, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_iClip1, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),			
	DEFINE_PRED_FIELD( m_iClip2, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),			

	DEFINE_PRED_FIELD( m_nViewModelIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),

	DEFINE_PRED_FIELD(m_iWeaponSlot, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_iWeaponSlotPosition, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),

	DEFINE_PRED_FIELD(m_flCurrentViewmodelFOV, FIELD_FLOAT, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_flCurrentBobScale, FIELD_FLOAT, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_flCurrentSwayScale, FIELD_FLOAT, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_flCurrentSwaySpeedScale, FIELD_FLOAT, FTYPEDESC_INSENDTABLE),

	// Not networked

	DEFINE_PRED_FIELD( m_flTimeWeaponIdle, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_FIELD( m_bInReload, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bFireOnEmpty, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bFiringWholeClip, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flNextEmptySoundTime, FIELD_FLOAT ),
	DEFINE_FIELD( m_Activity, FIELD_INTEGER ),
	DEFINE_FIELD( m_fFireDuration, FIELD_FLOAT ),
	DEFINE_FIELD( m_iszName, FIELD_INTEGER ),		
	DEFINE_FIELD( m_bFiresUnderwater, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bAltFiresUnderwater, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fMinRange1, FIELD_FLOAT ),		
	DEFINE_FIELD( m_fMinRange2, FIELD_FLOAT ),		
	DEFINE_FIELD( m_fMaxRange1, FIELD_FLOAT ),		
	DEFINE_FIELD( m_fMaxRange2, FIELD_FLOAT ),		
	DEFINE_FIELD( m_bReloadsSingly, FIELD_BOOLEAN ),	
	DEFINE_FIELD( m_bRemoveable, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iPrimaryAmmoCount, FIELD_INTEGER ),
	DEFINE_FIELD( m_iSecondaryAmmoCount, FIELD_INTEGER ),

	//DEFINE_PHYSPTR( m_pConstraint ),

	// DEFINE_FIELD( m_iOldState, FIELD_INTEGER ),
	// DEFINE_FIELD( m_bJustRestored, FIELD_BOOLEAN ),

	// DEFINE_FIELD( m_OnPlayerPickup, COutputEvent ),
	// DEFINE_FIELD( m_pConstraint, FIELD_INTEGER ),

END_PREDICTION_DATA()

#endif	// ! CLIENT_DLL

// Special hack since we're aliasing the name C_BaseCombatWeapon with a macro on the client
IMPLEMENT_NETWORKCLASS_ALIASED( BaseCombatWeapon, DT_BaseCombatWeapon )

#ifdef MAPBASE_VSCRIPT

// Don't allow client to use Set functions.
// They will only cause visual discrepancies,
// and will be reverted on the next update from the server.
#ifdef GAME_DLL
#define DEFINE_SCRIPTFUNC_SV( p1, p2 ) DEFINE_SCRIPTFUNC( p1, p2 )
#define DEFINE_SCRIPTFUNC_NAMED_SV( p1, p2, p3 ) DEFINE_SCRIPTFUNC_NAMED( p1, p2, p3 )

#define DEFINE_SCRIPTFUNC_CL( p1, p2 )
#define DEFINE_SCRIPTFUNC_NAMED_CL( p1, p2, p3 )
#else
#define DEFINE_SCRIPTFUNC_SV( p1, p2 )
#define DEFINE_SCRIPTFUNC_NAMED_SV( p1, p2, p3 )

#define DEFINE_SCRIPTFUNC_CL( p1, p2 ) DEFINE_SCRIPTFUNC( p1, p2 )
#define DEFINE_SCRIPTFUNC_NAMED_CL( p1, p2, p3 ) DEFINE_SCRIPTFUNC_NAMED( p1, p2, p3 )
#endif

BEGIN_ENT_SCRIPTDESC( CBaseCombatWeapon, CBaseAnimating, "The base class for all equippable weapons." )

	DEFINE_SCRIPTFUNC_NAMED( ScriptGetOwner, "GetOwner", "Get the weapon's owner." )
	DEFINE_SCRIPTFUNC_NAMED_SV( ScriptSetOwner, "SetOwner", "Set the weapon's owner." )

	DEFINE_SCRIPTFUNC( Clip1, "Get the weapon's current primary ammo." )
	DEFINE_SCRIPTFUNC( Clip2, "Get the weapon's current secondary ammo." )
	DEFINE_SCRIPTFUNC_NAMED_SV( ScriptSetClip1, "SetClip1", "Set the weapon's current primary ammo." )
	DEFINE_SCRIPTFUNC_NAMED_SV( ScriptSetClip2, "SetClip2", "Set the weapon's current secondary ammo." )
	DEFINE_SCRIPTFUNC( GetMaxClip1, "Get the weapon's maximum primary ammo." )
	DEFINE_SCRIPTFUNC( GetMaxClip2, "Get the weapon's maximum secondary ammo." )
	DEFINE_SCRIPTFUNC( GetDefaultClip1, "Get the weapon's default primary ammo." )
	DEFINE_SCRIPTFUNC( GetDefaultClip2, "Get the weapon's default secondary ammo." )

	DEFINE_SCRIPTFUNC( HasAnyAmmo, "Check if the weapon currently has ammo or doesn't need ammo." )
	DEFINE_SCRIPTFUNC( HasPrimaryAmmo, "Check if the weapon currently has ammo or doesn't need primary ammo." )
	DEFINE_SCRIPTFUNC( HasSecondaryAmmo, "Check if the weapon currently has ammo or doesn't need secondary ammo." )
	DEFINE_SCRIPTFUNC( UsesPrimaryAmmo, "Check if the weapon uses primary ammo." )
	DEFINE_SCRIPTFUNC( UsesSecondaryAmmo, "Check if the weapon uses secondary ammo." )
	DEFINE_SCRIPTFUNC_SV( GiveDefaultAmmo, "Fill the weapon back up to default ammo." )

	DEFINE_SCRIPTFUNC( UsesClipsForAmmo1, "Check if the weapon uses clips for primary ammo." )
	DEFINE_SCRIPTFUNC( UsesClipsForAmmo2, "Check if the weapon uses clips for secondary ammo." )

	DEFINE_SCRIPTFUNC( GetPrimaryAmmoType, "Get the weapon's primary ammo type." )
	DEFINE_SCRIPTFUNC( GetSecondaryAmmoType, "Get the weapon's secondary ammo type." )

	DEFINE_SCRIPTFUNC( GetSubType, "Get the weapon's subtype." )
	DEFINE_SCRIPTFUNC_SV( SetSubType, "Set the weapon's subtype." )

	DEFINE_SCRIPTFUNC( GetFireRate, "Get the weapon's firing rate." )
	DEFINE_SCRIPTFUNC( AddViewKick, "Applies the weapon's view kick." )

	DEFINE_SCRIPTFUNC( GetWorldModel, "Get the weapon's world model." )
	DEFINE_SCRIPTFUNC( GetViewModel, "Get the weapon's view model." )
	DEFINE_SCRIPTFUNC( GetDroppedModel, "Get the weapon's unique dropped model if it has one." )

	DEFINE_SCRIPTFUNC( GetWeight, "Get the weapon's weight." )
	DEFINE_SCRIPTFUNC( GetPrintName, "" )

	DEFINE_SCRIPTFUNC_CL( GetSlot, "" )
	DEFINE_SCRIPTFUNC_CL( GetPosition, "" )

	DEFINE_SCRIPTFUNC( CanBePickedUpByNPCs, "Check if the weapon can be picked up by NPCs." )

	DEFINE_SCRIPTFUNC_SV( CapabilitiesGet, "Get the capabilities the weapon currently possesses." )

	DEFINE_SCRIPTFUNC( HasWeaponIdleTimeElapsed, "Returns true if the idle time has elapsed." )
	DEFINE_SCRIPTFUNC( GetWeaponIdleTime, "Returns the next time WeaponIdle() will run." )
	DEFINE_SCRIPTFUNC_SV( SetWeaponIdleTime, "Sets the next time WeaponIdle() will run." )

	DEFINE_SCRIPTFUNC_NAMED( ScriptWeaponClassify, "WeaponClassify", "Returns the weapon's classify class from the WEPCLASS_ constant group" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptWeaponSound, "WeaponSound", "Plays one of the weapon's sounds." )

	DEFINE_SCRIPTFUNC_NAMED( ScriptGetBulletSpread, "GetBulletSpread", "Returns the weapon's default bullet spread." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetBulletSpreadForProficiency, "GetBulletSpreadForProficiency", "Returns the weapon's bullet spread for the specified proficiency level." )

	DEFINE_SCRIPTFUNC_NAMED( ScriptGetPrimaryAttackActivity, "GetPrimaryAttackActivity", "Returns the weapon's primary attack activity." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetSecondaryAttackActivity, "GetSecondaryAttackActivity", "Returns the weapon's secondary attack activity." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptGetDrawActivity, "GetDrawActivity", "Returns the weapon's draw activity." )
	DEFINE_SCRIPTFUNC( GetDefaultAnimSpeed, "Returns the weapon's default animation speed." )
	DEFINE_SCRIPTFUNC( SendWeaponAnim, "Sends a weapon animation." )
	DEFINE_SCRIPTFUNC( GetViewModelSequenceDuration, "Gets the sequence duration of the current view model animation." )
	DEFINE_SCRIPTFUNC( IsViewModelSequenceFinished, "Returns true if the current view model animation is finished." )

	DEFINE_SCRIPTFUNC( FiresUnderwater, "Returns true if this weapon can fire underwater." )
	DEFINE_SCRIPTFUNC_SV( SetFiresUnderwater, "Sets whether this weapon can fire underwater." )
	DEFINE_SCRIPTFUNC( AltFiresUnderwater, "Returns true if this weapon can alt-fire underwater." )
	DEFINE_SCRIPTFUNC_SV( SetAltFiresUnderwater, "Sets whether this weapon can alt-fire underwater." )
	DEFINE_SCRIPTFUNC( MinRange1, "Returns the closest this weapon can be used." )
	DEFINE_SCRIPTFUNC_SV( SetMinRange1, "Sets the closest this weapon can be used." )
	DEFINE_SCRIPTFUNC( MinRange2, "Returns the closest this weapon can be used." )
	DEFINE_SCRIPTFUNC_SV( SetMinRange2, "Sets the closest this weapon can be used." )
	DEFINE_SCRIPTFUNC( ReloadsSingly, "Returns true if this weapon reloads 1 round at a time." )
	DEFINE_SCRIPTFUNC_SV( SetReloadsSingly, "Sets whether this weapon reloads 1 round at a time." )
	DEFINE_SCRIPTFUNC( FireDuration, "Returns the amount of time that the weapon has sustained firing." )
	DEFINE_SCRIPTFUNC_SV( SetFireDuration, "Sets the amount of time that the weapon has sustained firing." )

	DEFINE_SCRIPTFUNC( NextPrimaryAttack, "Returns the next time PrimaryAttack() will run when the player is pressing +ATTACK." )
	DEFINE_SCRIPTFUNC_SV( SetNextPrimaryAttack, "Sets the next time PrimaryAttack() will run when the player is pressing +ATTACK." )
	DEFINE_SCRIPTFUNC( NextSecondaryAttack, "Returns the next time SecondaryAttack() will run when the player is pressing +ATTACK2." )
	DEFINE_SCRIPTFUNC_SV( SetNextSecondaryAttack, "Sets the next time SecondaryAttack() will run when the player is pressing +ATTACK2." )

END_SCRIPTDESC();
#endif

#if !defined( CLIENT_DLL )
//-----------------------------------------------------------------------------
// Purpose: Save Data for Base Weapon object
//-----------------------------------------------------------------------------// 
BEGIN_DATADESC( CBaseCombatWeapon )

	DEFINE_FIELD( m_flNextPrimaryAttack, FIELD_TIME ),
	DEFINE_FIELD( m_flNextSecondaryAttack, FIELD_TIME ),
	DEFINE_FIELD( m_flTimeWeaponIdle, FIELD_TIME ),

	DEFINE_FIELD( m_bInReload, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bFireOnEmpty, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hOwner, FIELD_EHANDLE ),

	DEFINE_FIELD( m_iState, FIELD_INTEGER ),
	DEFINE_FIELD( m_iszName, FIELD_STRING ),
	DEFINE_FIELD( m_iPrimaryAmmoType, FIELD_INTEGER ),
	DEFINE_FIELD( m_iSecondaryAmmoType, FIELD_INTEGER ),
	DEFINE_FIELD( m_iClip1, FIELD_INTEGER ),
	DEFINE_FIELD( m_iClip2, FIELD_INTEGER ),
	DEFINE_FIELD( m_bFiresUnderwater, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bAltFiresUnderwater, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fMinRange1, FIELD_FLOAT ),
	DEFINE_FIELD( m_fMinRange2, FIELD_FLOAT ),
	DEFINE_FIELD( m_fMaxRange1, FIELD_FLOAT ),
	DEFINE_FIELD( m_fMaxRange2, FIELD_FLOAT ),

	DEFINE_FIELD( m_iPrimaryAmmoCount, FIELD_INTEGER ),
	DEFINE_FIELD( m_iSecondaryAmmoCount, FIELD_INTEGER ),

	DEFINE_FIELD( m_nViewModelIndex, FIELD_INTEGER ),

	DEFINE_FIELD(m_iWeaponSlot, FIELD_INTEGER),
	DEFINE_FIELD(m_iWeaponSlotPosition, FIELD_INTEGER),

	DEFINE_FIELD(m_flCurrentViewmodelFOV, FIELD_FLOAT),
	DEFINE_FIELD(m_flCurrentBobScale, FIELD_FLOAT),
	DEFINE_FIELD(m_flCurrentSwayScale, FIELD_FLOAT),
	DEFINE_FIELD(m_flCurrentSwaySpeedScale, FIELD_FLOAT),

// don't save these, init to 0 and regenerate
//	DEFINE_FIELD( m_flNextEmptySoundTime, FIELD_TIME ),
//	DEFINE_FIELD( m_Activity, FIELD_INTEGER ),
 	DEFINE_FIELD( m_nIdealSequence, FIELD_INTEGER ),
	DEFINE_FIELD( m_IdealActivity, FIELD_INTEGER ),

	DEFINE_FIELD( m_fFireDuration, FIELD_FLOAT ),

	DEFINE_FIELD( m_bReloadsSingly, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iSubType, FIELD_INTEGER ),
 	DEFINE_FIELD( m_bRemoveable, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_flUnlockTime,		FIELD_TIME ),
	DEFINE_FIELD( m_hLocker,			FIELD_EHANDLE ),

	//	DEFINE_FIELD( m_iViewModelIndex, FIELD_INTEGER ),
	//	DEFINE_FIELD( m_iWorldModelIndex, FIELD_INTEGER ),
	//  DEFINE_FIELD( m_hWeaponFileInfo, ???? ),

	DEFINE_PHYSPTR( m_pConstraint ),

	DEFINE_FIELD( m_iReloadHudHintCount,	FIELD_INTEGER ),
	DEFINE_FIELD( m_iAltFireHudHintCount,	FIELD_INTEGER ),
	DEFINE_FIELD( m_bReloadHudHintDisplayed, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bAltFireHudHintDisplayed, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flHudHintPollTime, FIELD_TIME ),
	DEFINE_FIELD( m_flHudHintMinDisplayTime, FIELD_TIME ),

	// Just to quiet classcheck.. this field exists only on the client
//	DEFINE_FIELD( m_iOldState, FIELD_INTEGER ),
//	DEFINE_FIELD( m_bJustRestored, FIELD_BOOLEAN ),

	// Function pointers
	DEFINE_ENTITYFUNC( DefaultUse ),
	DEFINE_THINKFUNC( FallThink ),
	DEFINE_THINKFUNC( Materialize ),
	DEFINE_THINKFUNC( AttemptToMaterialize ),
	DEFINE_THINKFUNC( DestroyItem ),
	DEFINE_THINKFUNC( SetPickupUse ),

	DEFINE_THINKFUNC( HideThink ),
	DEFINE_INPUTFUNC( FIELD_VOID, "HideWeapon", InputHideWeapon ),

#ifdef MAPBASE
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetAmmo1", InputSetAmmo1 ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetAmmo2", InputSetAmmo2 ),
	DEFINE_INPUTFUNC( FIELD_VOID, "GiveDefaultAmmo", InputGiveDefaultAmmo ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnablePlayerPickup", InputEnablePlayerPickup ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisablePlayerPickup", InputDisablePlayerPickup ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableNPCPickup", InputEnableNPCPickup ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableNPCPickup", InputDisableNPCPickup ),
	DEFINE_INPUTFUNC( FIELD_VOID, "BreakConstraint", InputBreakConstraint ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ForcePrimaryFire", InputForcePrimaryFire ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ForceSecondaryFire", InputForceSecondaryFire ),
#endif

	// Outputs
	DEFINE_OUTPUT( m_OnPlayerUse, "OnPlayerUse"),
	DEFINE_OUTPUT( m_OnPlayerPickup, "OnPlayerPickup"),
	DEFINE_OUTPUT( m_OnNPCPickup, "OnNPCPickup"),
	DEFINE_OUTPUT( m_OnCacheInteraction, "OnCacheInteraction" ),
#ifdef MAPBASE
	DEFINE_OUTPUT( m_OnDropped, "OnDropped" ),
#endif

END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: Only send to local player if this weapon is the active weapon
// Input  : *pStruct - 
//			*pVarData - 
//			*pRecipients - 
//			objectID - 
// Output : void*
//-----------------------------------------------------------------------------
void* SendProxy_SendActiveLocalWeaponDataTable( const SendProp *pProp, const void *pStruct, const void *pVarData, CSendProxyRecipients *pRecipients, int objectID )
{
	// Get the weapon entity
	CBaseCombatWeapon *pWeapon = (CBaseCombatWeapon*)pVarData;
	if ( pWeapon )
	{
		// Only send this chunk of data to the player carrying this weapon
		CBasePlayer *pPlayer = ToBasePlayer( pWeapon->GetOwner() );
		if ( pPlayer /*&& pPlayer->GetActiveWeapon() == pWeapon*/ )
		{
			pRecipients->SetOnly( pPlayer->GetClientIndex() );
			return (void*)pVarData;
		}
	}
	
	return NULL;
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER( SendProxy_SendActiveLocalWeaponDataTable );

//-----------------------------------------------------------------------------
// Purpose: Only send the LocalWeaponData to the player carrying the weapon
//-----------------------------------------------------------------------------
void* SendProxy_SendLocalWeaponDataTable( const SendProp *pProp, const void *pStruct, const void *pVarData, CSendProxyRecipients *pRecipients, int objectID )
{
	// Get the weapon entity
	CBaseCombatWeapon *pWeapon = (CBaseCombatWeapon*)pVarData;
	if ( pWeapon )
	{
		// Only send this chunk of data to the player carrying this weapon
		CBasePlayer *pPlayer = ToBasePlayer( pWeapon->GetOwner() );
		if ( pPlayer )
		{
			pRecipients->SetOnly( pPlayer->GetClientIndex() );
			return (void*)pVarData;
		}
#ifdef MAPBASE
		else if (pWeapon->HasSpawnFlags( SF_WEAPON_PRESERVE_AMMO ))
		{
			// Ammo values are sent to the client using this proxy.
			// Preserved ammo values from the server need to be sent to the client ASAP to avoid HUD issues, etc.
			// I've tried many nasty hacks, but this is the one that works well enough and there's not much else we could do.
			pRecipients->SetAllRecipients();
			return (void*)pVarData;
		}
#endif
	}
	
	return NULL;
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER( SendProxy_SendLocalWeaponDataTable );

//-----------------------------------------------------------------------------
// Purpose: Only send to non-local players
//-----------------------------------------------------------------------------
void* SendProxy_SendNonLocalWeaponDataTable( const SendProp *pProp, const void *pStruct, const void *pVarData, CSendProxyRecipients *pRecipients, int objectID )
{
	pRecipients->SetAllRecipients();

	CBaseCombatWeapon *pWeapon = (CBaseCombatWeapon*)pVarData;
	if ( pWeapon )
	{
		CBasePlayer *pPlayer = ToBasePlayer( pWeapon->GetOwner() );
		if ( pPlayer )
		{
			pRecipients->ClearRecipient( pPlayer->GetClientIndex() );
			return ( void * )pVarData;
		}
	}

	return NULL;
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER( SendProxy_SendNonLocalWeaponDataTable );

#endif

#if PREDICTION_ERROR_CHECK_LEVEL > 1
#define SendPropTime SendPropFloat
#define RecvPropTime RecvPropFloat
#endif

//-----------------------------------------------------------------------------
// Purpose: Propagation data for weapons. Only sent when a player's holding it.
//-----------------------------------------------------------------------------
BEGIN_NETWORK_TABLE_NOBASE( CBaseCombatWeapon, DT_LocalActiveWeaponData )
#if !defined( CLIENT_DLL )
	SendPropTime( SENDINFO( m_flNextPrimaryAttack ) ),
	SendPropTime( SENDINFO( m_flNextSecondaryAttack ) ),
	SendPropInt( SENDINFO( m_nNextThinkTick ) ),
	SendPropTime( SENDINFO( m_flTimeWeaponIdle ) ),

#if defined( TF_DLL )
	SendPropExclude( "DT_AnimTimeMustBeFirst" , "m_flAnimTime" ),
#endif

#else
	RecvPropTime( RECVINFO( m_flNextPrimaryAttack ) ),
	RecvPropTime( RECVINFO( m_flNextSecondaryAttack ) ),
	RecvPropInt( RECVINFO( m_nNextThinkTick ) ),
	RecvPropTime( RECVINFO( m_flTimeWeaponIdle ) ),
#endif
END_NETWORK_TABLE()

//-----------------------------------------------------------------------------
// Purpose: Propagation data for weapons. Only sent when a player's holding it.
//-----------------------------------------------------------------------------
BEGIN_NETWORK_TABLE_NOBASE( CBaseCombatWeapon, DT_LocalWeaponData )
#if !defined( CLIENT_DLL )
	SendPropIntWithMinusOneFlag( SENDINFO(m_iClip1 ), 8 ),
	SendPropIntWithMinusOneFlag( SENDINFO(m_iClip2 ), 8 ),
	SendPropInt( SENDINFO(m_iPrimaryAmmoType ), 8 ),
	SendPropInt( SENDINFO(m_iSecondaryAmmoType ), 8 ),

	SendPropInt( SENDINFO( m_nViewModelIndex ), VIEWMODEL_INDEX_BITS, SPROP_UNSIGNED ),

	SendPropInt( SENDINFO( m_bFlipViewModel ) ),

	SendPropInt(SENDINFO(m_iWeaponSlot)),
	SendPropInt(SENDINFO(m_iWeaponSlotPosition)),

	SendPropBool(SENDINFO(m_bIsADS)),

	SendPropInt(SENDINFO(m_flCurrentViewmodelFOV)),
	SendPropInt(SENDINFO(m_flCurrentBobScale)),
	SendPropInt(SENDINFO(m_flCurrentSwayScale)),
	SendPropInt(SENDINFO(m_flCurrentSwaySpeedScale)),

#if defined( TF_DLL )
	SendPropExclude( "DT_AnimTimeMustBeFirst" , "m_flAnimTime" ),
#endif

#else
	RecvPropIntWithMinusOneFlag( RECVINFO(m_iClip1 )),
	RecvPropIntWithMinusOneFlag( RECVINFO(m_iClip2 )),
	RecvPropInt( RECVINFO(m_iPrimaryAmmoType )),
	RecvPropInt( RECVINFO(m_iSecondaryAmmoType )),

	RecvPropInt( RECVINFO( m_nViewModelIndex ) ),

	RecvPropBool( RECVINFO( m_bFlipViewModel ) ),

	RecvPropInt(RECVINFO(m_iWeaponSlot)),
	RecvPropInt(RECVINFO(m_iWeaponSlotPosition)),

	RecvPropBool(RECVINFO(m_bIsADS)),

	RecvPropInt(RECVINFO(m_flCurrentViewmodelFOV)),
	RecvPropInt(RECVINFO(m_flCurrentBobScale)),
	RecvPropInt(RECVINFO(m_flCurrentSwayScale)),
	RecvPropInt(RECVINFO(m_flCurrentSwaySpeedScale)),

#endif
END_NETWORK_TABLE()

BEGIN_NETWORK_TABLE(CBaseCombatWeapon, DT_BaseCombatWeapon)
#if !defined( CLIENT_DLL )
	SendPropDataTable("LocalWeaponData", 0, &REFERENCE_SEND_TABLE(DT_LocalWeaponData), SendProxy_SendLocalWeaponDataTable ),
	SendPropDataTable("LocalActiveWeaponData", 0, &REFERENCE_SEND_TABLE(DT_LocalActiveWeaponData), SendProxy_SendActiveLocalWeaponDataTable ),
	SendPropModelIndex( SENDINFO(m_iViewModelIndex) ),
	SendPropModelIndex( SENDINFO(m_iWorldModelIndex) ),
#ifdef MAPBASE
	SendPropModelIndex( SENDINFO(m_iDroppedModelIndex) ),
#endif
	SendPropInt( SENDINFO(m_iState ), 8, SPROP_UNSIGNED ),
	SendPropEHandle( SENDINFO(m_hOwner) ),

#ifdef MAPBASE
	SendPropInt( SENDINFO(m_spawnflags), 8, SPROP_UNSIGNED ),
#endif
	SendPropVector(SENDINFO(m_vecADSOrigin), -1, SPROP_COORD),
	SendPropQAngles(SENDINFO(m_angADSAngles), 13),

#else
	RecvPropDataTable("LocalWeaponData", 0, 0, &REFERENCE_RECV_TABLE(DT_LocalWeaponData)),
	RecvPropDataTable("LocalActiveWeaponData", 0, 0, &REFERENCE_RECV_TABLE(DT_LocalActiveWeaponData)),
	RecvPropInt( RECVINFO(m_iViewModelIndex)),
	RecvPropInt( RECVINFO(m_iWorldModelIndex)),
#ifdef MAPBASE
	RecvPropInt( RECVINFO(m_iDroppedModelIndex) ),
#endif
	RecvPropInt( RECVINFO(m_iState )),
	RecvPropEHandle( RECVINFO(m_hOwner ) ),

#ifdef MAPBASE
	RecvPropInt( RECVINFO( m_spawnflags ) ),
#endif
	RecvPropVector(RECVINFO(m_vecADSOrigin)),
	RecvPropQAngles(RECVINFO(m_angADSAngles)),

#endif
END_NETWORK_TABLE()
