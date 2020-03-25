#include "Aimbot.h"
#include "Glow.h"
#include "../utils/Logger.h"

#include "../utils/Math.h"
#include "../utils/Wrappers.h"
#include "../utils/minitrace.h"


#define SMOOTH_TYPE 0
#define SMOOTH_TYPE_FAST 0

#define val 0.5f

static void RecoilCompensation(const QAngle &viewAngle, QAngle &angle) {
    QAngle recoil = localPlayer.aimPunch;

    angle -= recoil;
}

float randFloat(float min, float max) {

    return min + static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / (max - min)));
}

static void ApplyErrorToPoint(Vector *point, float margin = 5.0f) { // applying error to angle causes issues on long range
    Vector error;
    error->x = randFloat(-1, 1);
    error->y = randFloat(-1, 1);
    //error->z = randFloat(-1, 1);
    error *= margin;

    *point += error;
}

static void SwayCompensation(const QAngle &viewAngle, QAngle &angle) {
    QAngle dynamic = localPlayer.swayAngles;
    QAngle sway = dynamic - viewAngle;
    sway.Normalize();

    angle -= sway;
}

static void SpreadCompensation(uintptr_t weapon) { // needs fix
    process->Write<float>(weapon + 0x141c, -1.0f);
    process->Write<float>(weapon + 0x1420, -1.0f);
}

void Smooth(QAngle &angle, QAngle &viewAngle) {
    float smooth = std::min(0.99f, val);

    QAngle delta = angle - viewAngle;
    delta.Normalize();
    Math::Clamp(delta);

    if (delta.Length() < 0.1f)
        return;


    if (SMOOTH_TYPE == SMOOTH_TYPE_FAST) {
        float coefficient = (1.0f - smooth) / delta.Length() * 4.0f;
        coefficient = powf(coefficient, 2.0f) * 10.0f;
        coefficient = std::max(0.05f, coefficient);
        coefficient = std::min(1.0f, coefficient);

        delta.v = delta.v * coefficient;
    } else {
        delta.v = delta.v * (1.0f - smooth);
        if (delta.Length() < 2.0f) {
            delta.v = delta.v + (delta.v * delta.Length());
        }
    }

    delta.Normalize();
    angle = viewAngle + delta;
}

void VelocityPrediction(CBaseEntity *entity, uintptr_t weapon, float distance, float bulletVelocity, Vector &result) {
    // divided into two parts: charge rifle (no bullet drop) and other weapons (not done yet)
    Vector enemyVelocity = entity->velocity;

    float projectileGravityScale = process->Read<float>(weapon + 0x1D34);

    float time = distance / bulletVelocity;
    if (time == INFINITY || time == NAN) {
        time = 0;
    }

    //Logger::Log("time: %f\n", time);
    if (time < globalVars.intervalPerTick) {
        //Logger::Log("setting to 0: %f\n", globalVars.intervalPerTick);
        time = 0.0f;
    }

    result->x += time * enemyVelocity->x;
    result->y += time * enemyVelocity->y;

    // v25 = (((*(weapon + 0x1C98) * *(gravity + 0x1A)) * 0.5) * (*(weapon + 0x1B2C) / *(weapon + 0x1C90))) / *(weapon + 0x1C90)
    //result->z += (enemyVelocity->z * time) + ((projectileGravityScale * 750.0f * 0.5 * powf(time, 2.0f)) / bulletVel); // Game prediction

    result->z += ((enemyVelocity->z * time) + ((projectileGravityScale * 750.0f) * 0.5 * powf(time, 2.0f)));
    result->z -= 1.0f;

    ApplyErrorToPoint(&result); // maybe apply error on relative head position?
}

void Aimbot::Aimbot() {
    static Vector prevPosition[101];
    MTR_SCOPED_TRACE("Aimbot", "Run");

    static int lastEntity = -1;
    static int lastEntityIndex = -1;
    static uintptr_t plastEntity = 0;
    if (!localPlayer)
        return;

    static int iterations = 0;

    if (!pressedKeys[KEY_LEFTALT] && clientState.m_signonState == SIGNONSTATE_INGAMEAPEX) {

        // if we cannot run aimbot and we arent speedhacking reset fakelag
        //if (!(pressedKeys & KEY_ALT))
        //process->Write<double>(clientStateAddr + OFFSET_OF(&CClientState::m_nextCmdTime), 0.0);
        iterations++;
        if (iterations > 5) {
            aimbotEntity = 0;
            lastEntity = -1;
            return;
        }
    } else if (pressedKeys[KEY_LEFTALT]) {
        iterations = 0;
    }

    QAngle localAngles = localPlayer.viewAngles;
    Vector localEye = localPlayer.eyePos;

    uintptr_t weapon = GetActiveWeapon(localPlayer);

    float bulletVel = process->Read<float>(weapon + 0x1D2C);
    if (bulletVel == 1.0f) { // 1.0f is fists.
        //Logger::Log("Not aimbotting on fists\n");
        return;
    }

    CBaseEntity *closestEnt = nullptr;
    float closest = __FLT_MAX__;
    float closestDist = __FLT_MAX__;
    Vector closestHeadPos;
    int closestID;
    if (lastEntity > validEntities.size())
        lastEntity = -1;

    if (lastEntity != -1) {
        CBaseEntity &tmp = entities[validEntities[lastEntity]];

        if (tmp && plastEntity && tmp.GetBaseClass().address != plastEntity) {
            tmp.Update(plastEntity);
        }

        if (!tmp
            || tmp == localPlayer
            || tmp.GetTeamNum() == localPlayer.GetTeamNum()
            || tmp.GetBleedoutState() != 0
            || tmp.GetLifestate() != 0
            || !tmp.GetPlayerState()) {
            lastEntity = -1;
        }
    }

    for (size_t entID = 0; entID < validEntities.size(); entID++) {
        CBaseEntity &entity = entities[validEntities[entID]];
        if (!entity) {
            continue;
        }

        if (entity == localPlayer
            || entity.GetTeamNum() == localPlayer.GetTeamNum()
            || entity.GetBleedoutState() != 0
            || entity.GetLifestate() != 0
            || !entity.GetPlayerState()) {
            continue;
        }

        Vector headpos = GetBonePos(entity, 12, entity.origin);
        float dist = localEye.DistTo(headpos);
        float distFactor = Math::DistanceFOV(localAngles, QAngle(headpos - localEye), dist);
        float angleFov = Math::AngleFOV(localAngles, QAngle(headpos - localEye));

        if (angleFov > 45.0f) {
            continue;
        }

        //float distFactor = Math::AngleFOV(localAngles, QAngle(headpos - localEye));
        if (distFactor < closest && (lastEntity == -1 || entID == lastEntity)) {
            closest = distFactor;
            closestEnt = &entity;
            plastEntity = closestEnt->GetBaseClass().address;
            lastEntityIndex = closestEnt->index;
            closestDist = dist;
            closestHeadPos = headpos;
            closestID = entID;
        }
    }

    if (lastEntity != -1) {
        CBaseEntity &tmp = entities[validEntities[lastEntity]];
        closestEnt = &tmp;
    }

    if (!closestEnt) {
        //Logger::Log("Couldn't find an ent to shoot\n");
        return;
    }
    lastEntity = closestID;

    aimbotEntity = closestEnt->GetBaseClass().address;

    VelocityPrediction(closestEnt, weapon, closestDist, bulletVel, closestHeadPos);

    for (size_t entID = 0; entID < validEntities.size(); entID++) {
        CBaseEntity &entity = entities[validEntities[entID]];
        if (!entity
            || entity == localPlayer
            || entity.GetTeamNum() == localPlayer.GetTeamNum()
            || entity.GetBleedoutState() != 0
            || entity.GetLifestate() != 0
            || !entity.GetPlayerState()) {
            continue;
        }
        prevPosition[entID] = entity.origin;
    }

//#define SILENT_AIM

#ifdef SILENT_AIM
    // if we can not fire, dont try to do silent aim (since the shot will be delayed, and aimbot will not work correctly - maybe account for tihs later?)
    if (process->Read<float>(weapon + 0x7B0) > globalVars.curtime)
        return;

    if (netChan.m_chokedCommands < 2) {
        process->Write<double>(clientStateAddr + OFFSET_OF(&CClientState::m_nextCmdTime), std::numeric_limits<double>::max());
    }
    else {
        int32_t commandNr= process->Read<int32_t>(clientStateAddr + OFFSET_OF(&CClientState::m_lastUsedCommandNr));
        int32_t targetCommand = (commandNr - 1) % 300;

        CUserCmd userCmd = process->Read<CUserCmd>(userCmdArr + targetCommand * sizeof(CUserCmd));

        // manipulate usercmd here
        QAngle oldAngle = userCmd.m_viewAngles;

        QAngle aimAngle(closestHeadPos - userCmd.m_eyePos);
        if (aimAngle.IsZero() || !aimAngle.IsValid())
            return;

        //SwayCompensation(oldAngle, aimAngle, commandNr);

        aimAngle.Normalize();
        Math::Clamp(aimAngle);

        Math::CorrectMovement(&userCmd, oldAngle, userCmd.m_forwardmove, userCmd.m_sidemove);

        userCmd.m_viewAngles = aimAngle;
        userCmd.m_tickCount = globalVars.tickCount;
        userCmd.m_buttons |= IN_ATTACK;

        process->Write<CUserCmd>(userCmdArr + targetCommand * sizeof(CUserCmd), userCmd);
        process->Write<CUserCmd>(verifiedUserCmdArr + targetCommand * sizeof(CVerifiedUserCmd), userCmd);

        process->Write<double>(clientStateAddr + OFFSET_OF(&CClientState::m_nextCmdTime), 0.0);
    }

#else
    QAngle aimAngle(closestHeadPos - localEye);

    if ((aimAngle->x == 0 && aimAngle->y == 0 && aimAngle->z == 0) || !aimAngle.IsValid()) {
        return;
    }

    //SpreadCompensation(weapon); // $wag

    SwayCompensation(localAngles, aimAngle);
    Smooth(aimAngle,
           localAngles); // seems like they introduced a server-side fair-fight like anti-cheat with season4 which bans u for not being nice, so lets at least be a bit human

    aimAngle.Normalize();
    Math::Clamp(aimAngle);
    localPlayer.viewAngles = aimAngle;

    //process->Write<double>(clientStateAddr + OFFSET_OF(&CClientState::m_nextCmdTime), 0.0);

#endif

}
