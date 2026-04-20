#include "particle_service.h"

namespace WhiteoutDex::particle {

namespace {

ImVector DefaultFog(const Vector3f&) {
    // Opaque white: CombineColors leaves baseColor unchanged.
    return {255, 255, 255, 255};
}

} // namespace

ParticleService::ParticleService()
    : fogSampler_(&DefaultFog)
{}

ParticleService::~ParticleService() = default;

void ParticleService::AddPlaneEmitter(ModelId model, int emitterId,
                                       std::unique_ptr<PlaneEmitter> emitter) {
    std::lock_guard<std::mutex> lock(mutex_);
    emitters_[{model, emitterId}] = std::move(emitter);
}

void ParticleService::RemoveModel(ModelId model) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = emitters_.begin(); it != emitters_.end(); ) {
        if (it->first.model == model) it = emitters_.erase(it);
        else ++it;
    }
}

void ParticleService::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    emitters_.clear();
}

PlaneEmitter* ParticleService::GetEmitter(ModelId model, int emitterId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = emitters_.find({model, emitterId});
    return (it != emitters_.end()) ? it->second.get() : nullptr;
}

int ParticleService::EmitterCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(emitters_.size());
}

int ParticleService::TotalParticleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int total = 0;
    for (const auto& [k, e] : emitters_) {
        total += e->TotalAlive();
    }
    return total;
}

bool ParticleService::HasEmittersForModel(ModelId model) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [k, e] : emitters_) {
        if (k.model == model) return true;
    }
    return false;
}

void ParticleService::Simulate(float dt) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [k, e] : emitters_) {
        e->Update(dt);
    }
}

void ParticleService::BuildGeometry(const Matrix44f& worldToView,
                                     std::vector<Vertex>& outVertices,
                                     std::vector<EmitterDrawList>& outDrawLists) const {
    std::lock_guard<std::mutex> lock(mutex_);

    BuildGeometryInput in{};
    in.worldToView = &worldToView;
    in.fogEnabled  = fogEnabled_;
    in.fogSampler  = fogSampler_;

    for (const auto& [k, e] : emitters_) {
        int vcount = BuildEmitterGeometry(*e, in, outVertices);
        if (vcount > 0) {
            outDrawLists.push_back({
                k.model, k.id, vcount, e->PriorityPlane(), e->Material()
            });
        }
    }
}

void ParticleService::SetGlobalScaler(float s) {
    SetGlobalEmissionScaler(s);
}

float ParticleService::GlobalScaler() const {
    return GetGlobalEmissionScaler();
}

void ParticleService::SetFogSampler(FogSampler sampler) {
    std::lock_guard<std::mutex> lock(mutex_);
    fogSampler_ = sampler ? std::move(sampler) : FogSampler(&DefaultFog);
}

} // namespace WhiteoutDex::particle
