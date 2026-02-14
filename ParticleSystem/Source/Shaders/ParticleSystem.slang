#include "Common.slang"
#include "Math.slang"

// Heavily based on this:
// https://wickedengine.net/2017/11/gpu-based-particle-simulation/

// Demo idea:
// https://www.reddit.com/r/Unity3D/comments/a9nc5o/gpu_particle_force_field_tutorial_link_to_full/

static const int PARTICLE_SIMULATION_WORKGROUP_SIZE = 64;

[shader("compute")]
[numthreads(PARTICLES_INIT_WORKGROUP_SIZE, 1, 1)]
void InitMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint particleIdx = dtid.x;
    if (particleIdx >= MAX_PARTICLES)
    {
        return;
    }

    deadBuffer[particleIdx] = MAX_PARTICLES - 1 - particleIdx;
    // deadBuffer[particleIdx] = particleIdx;

    if (particleIdx == 0)
    {
        particleCountBuffer.Store(PARTICLE_SYSTEM_DEAD_COUNT_OFFSET, MAX_PARTICLES);
        particleCountBuffer.Store(PARTICLE_SYSTEM_ALIVE_1_COUNT_OFFSET, 0);
        particleCountBuffer.Store(PARTICLE_SYSTEM_ALIVE_2_COUNT_OFFSET, 0);
    }
}

[shader("compute")]
[numthreads(PARTICLE_EMITTER_WORKGROUP_SIZE, 1, 1)]
void EmitterMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint emitterIdx = dtid.x;
    if (emitterIdx >= uniformBuffer.emitterCount)
    {
        return;
    }

    Pcg32 pcg32;
    pcg32.Init(uniformBuffer.frameCount, emitterIdx);

    for (uint32_t i = 0; i < uniformBuffer.emitterIterations; ++i)
    {
        int deadCount;
        particleCountBuffer.InterlockedAdd(PARTICLE_SYSTEM_DEAD_COUNT_OFFSET, -1, deadCount);
        if (deadCount < 1)
        {
            return;
        }

        const uint particleIdx = deadBuffer[deadCount - 1];

        const Emitter emitter = emitterBuffer[emitterIdx];

        radiusBuffer[particleIdx] =
            PARTICLE_SIM_RENDER_SCALE * (emitter.radiusMin + pcg32.NextFloat() * emitter.radius);
        positionDistToForceFieldInvBuffer[particleIdx] = float4(
            emitter.position +
                IntervalUnitToSymmetric(pcg32.NextFloat3(), emitter.positionInterval),
            0.0
        );
        velocityMassInvBuffer[particleIdx] = float4(
            emitter.initialVelocity +
                IntervalUnitToSymmetric(pcg32.NextFloat3(), emitter.initialVelocityInterval),
            emitter.massInv
        );
        timeLeftBuffer[particleIdx] = emitter.life;
        emitterIdxBuffer[particleIdx] = uint8_t(emitterIdx);

        uint aliveCount2;
        particleCountBuffer.InterlockedAdd(PARTICLE_SYSTEM_ALIVE_2_COUNT_OFFSET, 1, aliveCount2);

        aliveBuffer1[aliveCount2] = particleIdx;
    }
}

[shader("compute")]
[numthreads(1, 1, 1)]
void BeforeSimulationMain()
{
    const uint aliveCount2 = particleCountBuffer.Load(PARTICLE_SYSTEM_ALIVE_2_COUNT_OFFSET);
    const int deadCount = particleCountBuffer.Load<int>(PARTICLE_SYSTEM_DEAD_COUNT_OFFSET);

    particleCountBuffer.Store(PARTICLE_SYSTEM_DEAD_COUNT_OFFSET, max(0, deadCount));
    particleCountBuffer.Store(PARTICLE_SYSTEM_ALIVE_1_COUNT_OFFSET, aliveCount2);
    particleCountBuffer.Store(PARTICLE_SYSTEM_ALIVE_2_COUNT_OFFSET, 0);

    const uint3 dispatchCount = uint3(
        (aliveCount2 + PARTICLE_SIMULATION_WORKGROUP_SIZE - 1) / PARTICLE_SIMULATION_WORKGROUP_SIZE,
        1,
        1
    );
    indirectBuffer.Store(INDIRECT_DATA_DISPATCH_COUNT_OFFSET, dispatchCount);
}

[shader("compute")]
[numthreads(PARTICLE_SIMULATION_WORKGROUP_SIZE, 1, 1)]
void SimulationMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint aliveCount = particleCountUniformBuffer.aliveCount1;

    if (dtid.x >= aliveCount)
    {
        return;
    }

    const uint particleIdx = aliveBuffer1[dtid.x];

    float4 velocityMassInv = velocityMassInvBuffer[particleIdx];
    float4 positionDistToForceFieldInv = positionDistToForceFieldInvBuffer[particleIdx];
    float timeLeft = timeLeftBuffer[particleIdx];

    const bool isAlive = timeLeft > 0.0;

    const uint aliveSubgroupIdx = WavePrefixCountBits(isAlive);
    const uint aliveSubgroupCount = WaveActiveCountBits(isAlive);
    const uint deadSubgroupIdx = WavePrefixCountBits(!isAlive);
    const uint deadSubgroupCount = WaveActiveCountBits(!isAlive);
    uint aliveIdx2;
    uint deadIdx;
    if (WaveIsFirstLane())
    {
        particleCountBuffer.InterlockedAdd(
            PARTICLE_SYSTEM_DEAD_COUNT_OFFSET,
            deadSubgroupCount,
            deadIdx
        );
        particleCountBuffer.InterlockedAdd(
            PARTICLE_SYSTEM_ALIVE_2_COUNT_OFFSET,
            aliveSubgroupCount,
            aliveIdx2
        );
    }
    aliveIdx2 = WaveReadLaneFirst(aliveIdx2);
    deadIdx = WaveReadLaneFirst(deadIdx);

    if (isAlive)
    {
        aliveIdx2 += aliveSubgroupIdx;
        aliveBuffer2[aliveIdx2] = particleIdx;
    }
    else
    {
        deadIdx += deadSubgroupIdx;
        deadBuffer[deadIdx] = particleIdx;
        return;
    }

    const SphereForceField forceField = uniformBuffer.forceField;
    const uint particleSimIterations = uniformBuffer.particleSimIterations;

    for (uint i = 0; i < particleSimIterations; ++i)
    {
        const float3 forceFieldToParticle = positionDistToForceFieldInv.xyz - forceField.position;
        const float lenForceFieldToParticle = length(forceFieldToParticle);

        const float forceMag = forceField.constForce /
            (velocityMassInv.w * lenForceFieldToParticle * lenForceFieldToParticle);
        const float3 forceFieldToParticleDir = forceFieldToParticle / lenForceFieldToParticle;
        const float3 force = -forceMag * forceFieldToParticleDir;

        velocityMassInv.xyz += force * velocityMassInv.w * PARTICLE_SIM_TIME_STEP;

        positionDistToForceFieldInv.xyz += velocityMassInv.xyz * PARTICLE_SIM_TIME_STEP;

        // Checking if it's dead on every iteration is unnecessary,
        // it will be killed on the next frame.
        timeLeft -= PARTICLE_SIM_TIME_STEP;

        // Satisfying a sphere nonpenetration constraint.
        positionDistToForceFieldInv.xyz +=
            max(0.0, forceField.radius - lenForceFieldToParticle) * forceFieldToParticleDir;

        // NOTE: no velocity recalculation to prevent particles bouncing off from a sphere's surface.
    }

    velocityMassInvBuffer[particleIdx] = velocityMassInv;
    // + 0.1 to prevent dividing by 0.
    positionDistToForceFieldInv.w = 1.0 /
        (distance(positionDistToForceFieldInv.xyz, forceField.position) - forceField.radius + 0.1);
    positionDistToForceFieldInvBuffer[particleIdx] = positionDistToForceFieldInv;
    timeLeftBuffer[particleIdx] = timeLeft;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void AfterSimulationMain()
{
    const uint aliveParticleCount = particleCountBuffer.Load(PARTICLE_SYSTEM_ALIVE_2_COUNT_OFFSET);
    VkDrawIndirectCommand cmd;
    cmd.vertexCount = aliveParticleCount;
    cmd.instanceCount = 1;
    cmd.firstVertex = 0;
    cmd.firstInstance = 0;

    indirectBuffer.Store(INDIRECT_DATA_DRAW_CMD_OFFSET, cmd);
}
