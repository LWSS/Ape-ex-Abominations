#pragma once
#include "BaseStruct.h"
#include "OffPtr.h"
#include "QAngle.h"
#include "../Netvars.h"
#include "Definitions.h" // so u can use flags
#include "../utils/Logger.h"

#define CBASE_ENTITY_OFFSETS(HANDLER)           \
    HANDLER(int, 0x8, index)                    \
    HANDLER(Vector, 0x140, velocity)            \
    HANDLER(Vector, 0x140, absVelocity)         \
    HANDLER(Vector, 0x14C, origin)              \
    HANDLER(Vector, 0x14C, absOrigin)           \
    HANDLER(int, 0x310, iGlowEnable)            \
    HANDLER(bool, 0x380, bGlowEnable)           \
    HANDLER(float, 0x2FC, glowFarFadeDist)      \
    HANDLER(Vector, 0x1D0, glowCol)             \
    HANDLER(float, 0x2e0, glowDistance)         \
    HANDLER(float, 0x2D0, glowInside1)          \
    HANDLER(float, 0x2D8, glowInside2)          \
    HANDLER(float, 0x2E0, glowInside3)          \
    HANDLER(float, 0x2D4, glowOutline1)         \
    HANDLER(float, 0x2DC, glowOutline2)         \
    HANDLER(float, 0x2E4, glowOutline3)         \
    HANDLER(float, 0x2E8, glowLifetime)         \
    HANDLER(int, 0x3F0, teamNum)                \
    HANDLER(int, 0x1308, id)                    \
    HANDLER(Vector, 0x414, localAngles)         \
    HANDLER(uintptr_t, 0xEE0, boneMatrix)       \
    HANDLER(uintptr_t, 0x1944, activeWeapon)    \
    HANDLER(QAngle, 0x2308 , aimPunch)          \
    HANDLER(QAngle, 0x23C0, swayAngles)         \
    HANDLER(QAngle, 0x23D0, viewAngles)         \
    HANDLER(int, 0x3E0, health)                 \
    HANDLER(Vector, 0x1DA8, eyePos)             \


    //HANDLER(Vector, 0x1C04, eyePos)             \ OLD
    /*HANDLER(Vector, 0x4264, eyePos)             \*/

#define CONSTRUCTOR_HANDLER(type, offset, name) , name(baseClass)
#define DEFINE_HANDLER(type, offset, name) OffPtr<type, offset> name;
#define WRITE_BACK_HANDLER(type, offset, name) name.WriteBack(writeList);

class CBaseEntity
{
  private:
    char rBuf[0x2400];
    ProcessBaseClass baseClass;
    bool isPlayer = false;
  public:

    CBaseEntity(uintptr_t addr = 0)
        : baseClass(rBuf, addr) CBASE_ENTITY_OFFSETS(CONSTRUCTOR_HANDLER)
    {
    }

    const ProcessBaseClass& GetBaseClass()
    {
        return baseClass;
    }

    void Update(uintptr_t newAddress = 0)
    {
        if (newAddress)
            baseClass.address = newAddress;
        process->Read(baseClass.address, rBuf, sizeof(rBuf));
    }

    void SetPlayerState(bool state = true) {
        isPlayer = state;
    }

    bool GetPlayerState() {
        return isPlayer;
    }

    void WriteBack(WriteList& writeList)
    {
        CBASE_ENTITY_OFFSETS(WRITE_BACK_HANDLER);
    }

    inline bool operator==(const CBaseEntity &o)
    {
        return baseClass.address == o.baseClass.address;
    }

    inline bool operator==(uintptr_t addr)
    {
        return baseClass.address == addr;
    }

    inline operator bool() const
    {
        return baseClass.address;
    }

    inline int GetBleedoutState() {
        static uint32_t offset = Netvars::netvars["CPlayer"]["m_bleedoutState"];
        if( !offset ) {
            Logger::Log("Can't find Netvar [\"CPlayer\"][\"m_bleedoutState\"]!\n");
            return -1;
        }
        return process->Read<int>( baseClass.address + offset );
    }

    inline int GetLifestate() {
        static uint32_t offset = Netvars::netvars["CPlayer"]["m_lifeState"];
        if( !offset ) {
            Logger::Log("Can't find Netvar [\"CPlayer\"][\"m_lifeState\"]!\n");
            return -1;
        }
        return process->Read<int>( baseClass.address + offset );
    }

    inline int GetFlags() {
        static uint32_t offset = Netvars::netvars["CPlayer"]["m_fFlags"];
        if( !offset ) {
            Logger::Log("Can't find Netvar [\"CPlayer\"][\"m_fFlags\"]!\n");
            return -1;
        }
        return process->Read<int>( baseClass.address + offset );
    }

    inline int GetTeamNum() {
        static uint32_t offset = Netvars::netvars["CBaseEntity"]["m_iTeamNum"];
        if( !offset ){
            Logger::Log("Can't find Netvar [\"CBaseEntity\"][\"m_iTeamNum\"]!\n");
            return -1;
        }

        return process->Read<int>( baseClass.address + offset );
    }

    CBASE_ENTITY_OFFSETS(DEFINE_HANDLER)
};
